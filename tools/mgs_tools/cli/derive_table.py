#!/usr/bin/env python3
"""Derive code→char mappings by aligning our decompiled strings against
WantedThing's hand-curated .gcl output.

For every stage that both tools decompiled, pair each of our m"..."
strings against the corresponding j"..." string from WantedThing.
Walk them in lock-step: when our side emits `{XXXX}` and theirs emits
a single character, record `0xXXXX → char`.

The output is a Python dict ready to splice into mgs_chars.py. Conflicts
(one code mapped to two different chars across strings) are flagged but
the first-seen mapping wins.
"""
import argparse
import glob
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mgs_tools.gcl.gcx import GcxData
from mgs_tools.gcl.decompile import GclDecomp
from mgs_tools.gcl.writer import GclWriter
from mgs_tools.gcl.glyphs.chars import CHAR_TO_CODE, CODE_TO_CHAR


_OUR_STR_RE = re.compile(r'm"((?:[^"\\]|\\.)*)"')
_THEIR_STR_RE = re.compile(r'j"((?:[^"\\]|\\.)*)"')


def tokenize_ours(payload: str) -> list[tuple[str, object]]:
    """Our payload: ASCII chars, `{XXXX}` 2-byte codes, `\\xNN` escapes,
    plus chars already mapped via mgs_chars.CHAR_TO_CODE."""
    toks: list[tuple[str, object]] = []
    i = 0
    while i < len(payload):
        c = payload[i]
        if c == "\\" and i + 3 < len(payload) and payload[i + 1] == "x":
            toks.append(("esc", int(payload[i + 2:i + 4], 16)))
            i += 4
            continue
        if c == "{" and i + 5 < len(payload) and payload[i + 5] == "}":
            toks.append(("code", int(payload[i + 1:i + 5], 16)))
            i += 6
            continue
        if c in CHAR_TO_CODE:
            toks.append(("code", CHAR_TO_CODE[c]))
            i += 1
            continue
        toks.append(("char", c))
        i += 1
    return toks


def tokenize_theirs(payload: str) -> list[tuple[str, object]]:
    """Their payload: Unicode chars + `\\xNN` escapes."""
    toks: list[tuple[str, object]] = []
    i = 0
    while i < len(payload):
        c = payload[i]
        if c == "\\" and i + 3 < len(payload) and payload[i + 1] == "x":
            toks.append(("esc", int(payload[i + 2:i + 4], 16)))
            i += 4
        else:
            toks.append(("char", c))
            i += 1
    return toks


def extract_our_strings(gcx_path: str) -> list[str]:
    dec = GclDecomp(GcxData(gcx_path))
    dec.decompile_gcx_file()
    text = GclWriter(dec.tree_data, dec.fonts_size).render()
    return _OUR_STR_RE.findall(text)


def extract_their_strings(gcl_path: str) -> list[str]:
    return _THEIR_STR_RE.findall(Path(gcl_path).read_text(encoding="utf-8"))


def align(ours: list, theirs: list, derived: dict[int, str],
          conflicts: dict[int, set[str]]) -> None:
    """Walk two token streams, recording code→char matches.

    WantedThing sometimes split a 2-byte code into `\\xHI` + ASCII-low
    (e.g. 0xC03F → `\\xc0?`), so we accept that pattern explicitly.
    Otherwise we bail the whole string on structural mismatch to avoid
    recording guesses.
    """
    i = j = 0
    pending: dict[int, str] = {}
    while i < len(ours) and j < len(theirs):
        ot, ov = ours[i]
        tt, tv = theirs[j]

        # direct matches
        if ot == "char" and tt == "char" and ov == tv:
            i += 1; j += 1; continue
        if ot == "esc" and tt == "esc" and ov == tv:
            i += 1; j += 1; continue

        # our 2-byte code → their single Unicode char
        if ot == "code" and tt == "char":
            if ov in derived and derived[ov] != tv:
                conflicts[ov].add(tv)
                return
            if ov in pending and pending[ov] != tv:
                return
            pending[ov] = tv
            i += 1; j += 1; continue

        # our 2-byte code → their \xHI + ASCII-low split
        if ot == "code" and tt == "esc" and j + 1 < len(theirs):
            hi_want = (ov >> 8) & 0xFF
            lo_want = ov & 0xFF
            nt, nv = theirs[j + 1]
            if tv == hi_want and nt == "char" and ord(nv) == lo_want:
                i += 1; j += 2; continue
        # or their escape-pair for a 2-byte code
        if (ot == "code" and tt == "esc" and j + 1 < len(theirs)
                and theirs[j + 1][0] == "esc"):
            hi, lo = theirs[j][1], theirs[j + 1][1]
            if (hi << 8 | lo) == ov:
                i += 1; j += 2; continue

        # our 2-byte code → their single ASCII char (whole code is ASCII-like)
        if ot == "code" and tt == "char" and ord(tv) < 0x80:
            i += 1; j += 1  # skip without recording (unsafe ASCII mapping)
            continue

        # structural mismatch
        return

    if i != len(ours) or j != len(theirs):
        return
    for code, ch in pending.items():
        derived[code] = ch


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--wt", required=True,
                    help="WantedThing output dir (contains stage dirs)")
    ap.add_argument("--rex", required=True,
                    help="Rex stage extraction dir")
    ap.add_argument("-o", "--output", default="mgs_chars_derived.py",
                    help="path for derived-dict Python module")
    args = ap.parse_args()

    # WantedThing's filenames map to our hash-named Rex files:
    #   scenerio*.gcl → 00ea54.gcx (GV_StrCode("scenerio"))
    #   demo.gcl      → 00a242.gcx (GV_StrCode("demo"))
    WT_TO_HASH = {
        "scenerio":     "00ea54.gcx",
        "scenerio2":    "00ea54.gcx",
        "hackscenerio": "00ea54.gcx",
        "demo":         "00a242.gcx",
    }
    pairs: list[tuple[str, Path, Path]] = []
    for stage_dir in sorted(Path(args.wt).iterdir()):
        if not stage_dir.is_dir():
            continue
        stage = stage_dir.name
        for wt in stage_dir.glob("*.gcl"):
            fname = WT_TO_HASH.get(wt.stem)
            if fname is None:
                continue
            rex = Path(args.rex) / stage / fname
            if rex.exists():
                pairs.append((f"{stage}/{wt.stem}", wt, rex))

    print(f"# {len(pairs)} paired files")

    derived: dict[int, str] = {}
    conflicts: dict[int, set[str]] = defaultdict(set)
    paired_strings = 0
    aligned_strings = 0

    for label, wt, rex in pairs:
        ours = extract_our_strings(str(rex))
        theirs = extract_their_strings(str(wt))
        # Pair by order; if counts differ, skip this file (safer than
        # guessing which strings correspond).
        if len(ours) != len(theirs):
            continue
        for ko, kt in zip(ours, theirs):
            paired_strings += 1
            before = len(derived)
            align(tokenize_ours(ko), tokenize_theirs(kt),
                  derived, conflicts)
            if len(derived) > before:
                aligned_strings += 1

    # Drop any mapping that later conflicted.
    for code in list(conflicts):
        if code in derived:
            conflicts[code].add(derived[code])
            del derived[code]

    print(f"# paired={paired_strings}  aligned-and-added-mappings={aligned_strings}")
    print(f"# derived={len(derived)} codes  conflicts={len(conflicts)}")
    if conflicts:
        top = sorted(conflicts.items(),
                     key=lambda kv: -len(kv[1]))[:5]
        for code, chars in top:
            print(f"#   conflict 0x{code:04X}: {sorted(chars)[:5]}")

    with open(args.output, "w", encoding="utf-8") as f:
        f.write('"""Derived code→char mappings from WantedThing alignment.\n\n')
        f.write("Generated by derive_table.py — do not edit by hand.\n")
        f.write('"""\n\n')
        f.write("DERIVED_CODE_TO_CHAR: dict[int, str] = {\n")
        for code in sorted(derived):
            f.write(f"    0x{code:04X}: {derived[code]!r},\n")
        f.write("}\n")
    print(f"# wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
