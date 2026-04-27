#!/usr/bin/env python3
"""Render the stage-font (bank-2) glyphs used by a .gcx file.

Usage:
    ./show_glyphs.py path/to/00ea54.gcx            # only codes used in strings
    ./show_glyphs.py --all path/to/00ea54.gcx      # every glyph in the trailing blob
    ./show_glyphs.py --scan path/to/stages/        # dedupe across many stages

The rendered bitmaps let you fill in `mgs_chars.py` by visual inspection.
Stage fonts only populate codes 0x9A00+ (bank 2); font.res (bank 0) isn't
handled yet — that needs the game's font.res file.
"""
import argparse
import glob
import hashlib
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mgs_tools.gcl.gcx import GcxData
from mgs_tools.gcl.decompile import GclDecomp
from mgs_tools.gcl.glyphs.extractor import (
    GLYPH_BYTES,
    bank_and_offset,
    collect_codes_from_string,
    extract_glyph,
    render_ascii,
    strip_font_header,
    write_sheet_pgm,
)


def iter_string_nodes(node):
    """Yield every GCLCODE_STR payload in a node tree."""
    if isinstance(node, list):
        for child in node:
            yield from iter_string_nodes(child)
    elif isinstance(node, dict):
        for k, v in node.items():
            if k == "STR" and isinstance(v, str):
                yield v
            else:
                yield from iter_string_nodes(v)


def decomp(path):
    dec = GclDecomp(GcxData(path))
    dec.decompile_gcx_file()
    trailing = bytes(dec.gcx[dec.gcx.offset:dec.gcx.offset + dec.fonts_size])
    return dec, strip_font_header(trailing)


def show_one(path: str, dump_all: bool) -> None:
    dec, tail = decomp(path)
    print(f"# {path}")
    print(f"# trailing bank-2 bytes: {len(tail)}  "
          f"(up to {len(tail) // GLYPH_BYTES} glyphs)")
    print()

    if dump_all:
        for i in range(0, len(tail) - GLYPH_BYTES + 1, GLYPH_BYTES):
            _print_glyph(code=None, bitmap=tail[i:i + GLYPH_BYTES],
                         slot=i // GLYPH_BYTES)
        return

    # Collect every distinct 2-byte code referenced from string payloads.
    codes: set[int] = set()
    for payload in iter_string_nodes(dec.tree_data):
        codes.update(collect_codes_from_string(payload))

    shown = 0
    for code in sorted(codes):
        bank, off = bank_and_offset(code)
        if bank != 2:
            continue
        g = extract_glyph(tail, off)
        if g is None:
            print(f"0x{code:04X}  (offset {off}: out of range, trailing is {len(tail)} bytes)")
            print()
            continue
        _print_glyph(code=code, bitmap=g)
        shown += 1

    if shown == 0:
        print("(no bank-2 codes used in strings)")


def _print_glyph(*, code: int | None, bitmap: bytes, slot: int | None = None) -> None:
    header = []
    if code is not None:
        header.append(f"code=0x{code:04X}")
    if slot is not None:
        header.append(f"slot={slot}")
    header.append(f"sha1={hashlib.sha1(bitmap).hexdigest()[:10]}")
    print("  ".join(header))
    print(render_ascii(bitmap))
    print()


def scan(paths: list[str], sheet: str | None, labels_out: str | None) -> None:
    """Dedupe bank-2 bitmaps across many .gcx files, report first-seen."""
    all_gcx: list[str] = []
    for p in paths:
        if Path(p).is_dir():
            all_gcx.extend(sorted(glob.glob(str(Path(p) / "**" / "*.gcx"),
                                            recursive=True)))
        else:
            all_gcx.append(p)

    seen: dict[str, dict] = {}        # bitmap hash → metadata
    for f in all_gcx:
        dec, tail = decomp(f)
        codes_used: set[int] = set()
        for payload in iter_string_nodes(dec.tree_data):
            codes_used.update(collect_codes_from_string(payload))
        for code in codes_used:
            bank, off = bank_and_offset(code)
            if bank != 2:
                continue
            g = extract_glyph(tail, off)
            if g is None:
                continue
            h = hashlib.sha1(g).hexdigest()[:10]
            entry = seen.setdefault(h, {"bitmap": g, "uses": []})
            entry["uses"].append((Path(f).parent.name, code))

    ordered = sorted(seen.items())
    print(f"# {len(seen)} unique bank-2 glyphs across {len(all_gcx)} files")

    if sheet:
        write_sheet_pgm(sheet,
                        [(h, meta["bitmap"]) for h, meta in ordered])
        print(f"# sheet written to {sheet} "
              f"(open in any image viewer; cells are 12×12, row-major)")

    if labels_out:
        with open(labels_out, "w", encoding="utf-8") as f:
            f.write("# Glyph labels. Fill in the `char:` column with the\n")
            f.write("# Unicode character you recognise; blank lines / '#'\n")
            f.write("# comments are ignored. Feed into merge_labels.py.\n")
            f.write("#\n")
            f.write("# sha1       char   used_by (stage:code)...\n")
            for h, meta in ordered:
                uses = ",".join(f"{s}:{c:04X}" for s, c in meta["uses"][:3])
                more = "" if len(meta["uses"]) <= 3 else f",+{len(meta['uses'])-3}"
                f.write(f"{h}   =     # {uses}{more}\n")
        print(f"# labels template written to {labels_out}")

    if not sheet and not labels_out:
        # Fall back to ASCII dump.
        for h, meta in ordered:
            stages_codes = ", ".join(f"{s}:0x{c:04X}"
                                      for s, c in meta["uses"][:4])
            more = ("" if len(meta["uses"]) <= 4
                       else f" (+{len(meta['uses'])-4} more)")
            print(f"sha1={h}  used by: {stages_codes}{more}")
            print(render_ascii(meta["bitmap"]))
            print()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("path", nargs="+", help="gcx file(s) or directory")
    ap.add_argument("--all", action="store_true",
                    help="dump every slot in the trailing blob, not just used codes")
    ap.add_argument("--scan", action="store_true",
                    help="dedupe bank-2 bitmaps across many files")
    ap.add_argument("--sheet", metavar="FILE.pgm",
                    help="write a PGM sheet of all unique glyphs (scan mode)")
    ap.add_argument("--labels", metavar="FILE",
                    help="write a labels template to fill in (scan mode)")
    args = ap.parse_args()

    if args.scan:
        scan(args.path, args.sheet, args.labels)
    else:
        for p in args.path:
            show_one(p, args.all)
    return 0


if __name__ == "__main__":
    sys.exit(main())
