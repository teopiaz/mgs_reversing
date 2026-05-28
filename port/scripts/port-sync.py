#!/usr/bin/env python3
"""Detect drift between upstream source/ files and their port/ copies.

Workflow
--------
Each port copy (port/<subdir>/<file>.c) is a hand-stripped clone of an upstream
source/<subdir>/<file>.c — see port/PORT-ARCH.md for the mechanism.  This tool
remembers the last upstream SHA that was reconciled into each port copy (stored
in .port-sync-state.json at the repo root) and reports which port copies have
upstream churn since that SHA.

After rebasing upstream into the editor branch:

    port/scripts/port-sync.py status

    -> lists each port copy whose source/ counterpart was touched, with a
       per-file diff summary (#commits, +adds / -dels, optional --show-diff).

    port/scripts/port-sync.py mark port/libhzd/level.c

    -> after you've reconciled level.c by hand, stamp the new HEAD SHA into
       .port-sync-state.json.

    port/scripts/port-sync.py mark --all

    -> stamp every tracked port copy at the current HEAD (use sparingly — only
       when you've just done a full sync pass).

    port/scripts/port-sync.py discover

    -> re-derive the tracked list from port/Makefile's *_PORT_COPIES variables.
       New entries are seeded at the upstream HEAD; missing entries are kept
       (so a temporarily-disabled port copy doesn't lose its sync anchor).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
STATE_PATH = REPO_ROOT / ".port-sync-state.json"
MAKEFILE_PATH = REPO_ROOT / "port" / "Makefile"

# Maps each *_PORT_COPIES list to the source/ subdir whose files it shadows.
# Kept as a static table because the Makefile expresses this mapping implicitly
# (via per-list filter-out + pattern rules) rather than as data.
LIST_TO_SUBDIR = {
    "LIBGV_PORT_COPIES": "libgv",
    "LIBHZD_PORT_COPIES": "libhzd",
    "LIBDG_PORT_COPIES": "libdg",
    "TAKABE_PORT_COPIES": "takabe",
    "EQUIP_PORT_COPIES": "equip",
    "KOJO_PORT_COPIES": "kojo",
    "MENU_PORT_COPIES": "menu",
    "THING_PORT_COPIES": "thing",
    "SOUND_PORT_COPIES": "sound",
    "GAME_PORT_COPIES": "game",
}


def git(*args: str, check: bool = True) -> str:
    res = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if check and res.returncode != 0:
        sys.exit(f"git {' '.join(args)} failed:\n{res.stderr}")
    return res.stdout


def load_state() -> dict:
    if not STATE_PATH.exists():
        return {"_format": 1, "copies": {}}
    with STATE_PATH.open() as f:
        data = json.load(f)
    data.setdefault("copies", {})
    return data


def save_state(state: dict) -> None:
    with STATE_PATH.open("w") as f:
        json.dump(state, f, indent=2, sort_keys=True)
        f.write("\n")


def discover_from_makefile() -> dict[str, str]:
    """Return {port/<subdir>/<stem>.c: source/<subdir>/<stem>.c} from Makefile."""
    mk = MAKEFILE_PATH.read_text()
    out: dict[str, str] = {}
    for list_name, subdir in LIST_TO_SUBDIR.items():
        # Match: LIBHZD_PORT_COPIES = online near vector ...
        m = re.search(
            rf"^{re.escape(list_name)}\s*=\s*(?P<stems>.+)$",
            mk,
            re.MULTILINE,
        )
        if not m:
            continue
        for stem in m.group("stems").split():
            src = f"source/{subdir}/{stem}.c"
            port = f"port/{subdir}/{stem}.c"
            if (REPO_ROOT / src).exists() and (REPO_ROOT / port).exists():
                out[port] = src
    return out


def upstream_log(src: str, since_sha: str) -> list[tuple[str, str]]:
    """Return [(sha, subject), ...] for commits touching src after since_sha."""
    if not since_sha:
        return []
    out = git(
        "log",
        f"{since_sha}..HEAD",
        "--format=%H%x09%s",
        "--",
        src,
        check=False,
    )
    rows = []
    for line in out.splitlines():
        if "\t" in line:
            sha, subj = line.split("\t", 1)
            rows.append((sha, subj))
    return rows


def numstat(src: str, since_sha: str) -> tuple[int, int]:
    """(added, deleted) lines in src since since_sha. (0,0) on no change."""
    if not since_sha:
        return (0, 0)
    out = git(
        "log",
        f"{since_sha}..HEAD",
        "--numstat",
        "--format=",
        "--",
        src,
        check=False,
    )
    add = dele = 0
    for line in out.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            try:
                add += int(parts[0])
                dele += int(parts[1])
            except ValueError:
                pass  # binary file
    return (add, dele)


def cmd_status(args) -> int:
    state = load_state()
    copies = state["copies"]
    if not copies:
        sys.exit("No tracked port copies — run `port-sync.py discover` first.")

    head = git("rev-parse", "HEAD").strip()
    drifted = []
    clean = []
    for port_path, info in sorted(copies.items()):
        src = info["source"]
        anchor = info["source_sha"]
        log = upstream_log(src, anchor)
        if log:
            add, dele = numstat(src, anchor)
            drifted.append((port_path, src, anchor, log, add, dele))
        else:
            clean.append(port_path)

    if not drifted:
        print(f"All {len(copies)} port copies in sync with HEAD ({head[:9]}).")
        return 0

    print(f"{len(drifted)} of {len(copies)} port copies have upstream drift:\n")
    for port_path, src, anchor, log, add, dele in drifted:
        print(f"  {port_path}")
        print(f"    upstream: {src}")
        print(f"    anchor:   {anchor[:9]} (drift +{add}/-{dele} across {len(log)} commit(s))")
        for sha, subj in log[: args.max_commits]:
            print(f"      {sha[:9]}  {subj}")
        if len(log) > args.max_commits:
            print(f"      ... +{len(log) - args.max_commits} more")
        if args.show_diff:
            diff = git("diff", anchor, "HEAD", "--", src, check=False)
            for line in diff.splitlines():
                print(f"      | {line}")
        print()

    print("Reconcile each port copy by hand (semantic merge), then:")
    for port_path, *_ in drifted:
        print(f"  port/scripts/port-sync.py mark {port_path}")
    return 1


def cmd_mark(args) -> int:
    state = load_state()
    copies = state["copies"]
    head = git("rev-parse", "HEAD").strip()

    targets: list[str]
    if args.all:
        targets = list(copies.keys())
    else:
        if not args.paths:
            sys.exit("mark: pass --all or one or more port/* paths.")
        targets = []
        for p in args.paths:
            # Allow absolute or repo-relative
            p_norm = os.path.relpath(os.path.abspath(p), REPO_ROOT)
            if p_norm not in copies:
                sys.exit(f"mark: {p_norm} is not tracked. Run `discover` first.")
            targets.append(p_norm)

    for port_path in targets:
        old = copies[port_path].get("source_sha", "")[:9]
        copies[port_path]["source_sha"] = head
        print(f"  {port_path}: {old} -> {head[:9]}")

    save_state(state)
    print(f"\nStamped {len(targets)} port copy/copies at {head[:9]}.")
    return 0


def cmd_discover(args) -> int:
    state = load_state()
    copies = state["copies"]
    discovered = discover_from_makefile()
    head = git("rev-parse", "HEAD").strip()

    added = []
    for port_path, src in discovered.items():
        if port_path not in copies:
            # Seed at the latest commit that touched src — that's what the
            # initial port copy was based on.
            sha = git(
                "log", "-1", "--format=%H", "--", src, check=False
            ).strip() or head
            copies[port_path] = {"source": src, "source_sha": sha}
            added.append((port_path, sha))

    stale = [p for p in copies if p not in discovered]

    if added:
        print(f"Added {len(added)} new tracked port copy/copies:")
        for port_path, sha in added:
            print(f"  + {port_path}  (anchored at {sha[:9]})")
    if stale:
        print(f"\n{len(stale)} tracked port copy/copies no longer in Makefile lists:")
        for port_path in stale:
            print(f"  ? {port_path}")
        print("  (left in state — remove manually if intentional.)")
    if not added and not stale:
        print(f"State already in sync with Makefile lists ({len(copies)} port copies).")

    save_state(state)
    return 0


def main() -> int:
    p = argparse.ArgumentParser(
        description="Detect drift between source/ and port/ copies.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    p_status = sub.add_parser("status", help="Show port copies with upstream drift.")
    p_status.add_argument(
        "--show-diff",
        action="store_true",
        help="Print the full git diff for each drifted file.",
    )
    p_status.add_argument(
        "--max-commits",
        type=int,
        default=6,
        help="Show up to N commit subjects per file (default 6).",
    )
    p_status.set_defaults(fn=cmd_status)

    p_mark = sub.add_parser(
        "mark",
        help="Stamp port copies as reconciled at current HEAD.",
    )
    p_mark.add_argument("paths", nargs="*", help="port/<subdir>/<file>.c paths to mark.")
    p_mark.add_argument(
        "--all",
        action="store_true",
        help="Stamp every tracked port copy. Use only after a full sync pass.",
    )
    p_mark.set_defaults(fn=cmd_mark)

    p_disc = sub.add_parser(
        "discover",
        help="Re-derive tracked list from port/Makefile *_PORT_COPIES variables.",
    )
    p_disc.set_defaults(fn=cmd_discover)

    args = p.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
