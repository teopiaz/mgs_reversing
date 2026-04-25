#!/usr/bin/env python3
"""Decompile a .gcx bytecode file to .gcl source text.

Usage:
    ./gcx2gcl.py input.gcx [-o output.gcl] [--json out.json]
"""
import argparse
import json
import sys
from pathlib import Path

# Make sibling modules importable when run as a script.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from gcx import GcxData
from gcl_decompile import GclDecomp
from gcl_writer import GclWriter


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help="path to a .gcx file")
    ap.add_argument("-o", "--output", help="write .gcl text (default: stdout)")
    ap.add_argument("--json", help="also dump the AST as JSON to this path")
    ap.add_argument("--trailing", help="write trailing font/data bytes here "
                                        "(pair with gcl2gcx.py --trailing)")
    ap.add_argument("--names", action="store_true",
                    help="emit &NAME symbolic constants for known hashes "
                         "(see port/gcl_tools/mgs_names.py)")
    args = ap.parse_args()

    gcx = GcxData(args.input)
    dec = GclDecomp(gcx)
    dec.decompile_gcx_file()

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(dec.tree_data, f, indent=2, default=str)

    if args.trailing:
        tail = bytes(dec.gcx[dec.gcx.offset:dec.gcx.offset + dec.fonts_size])
        Path(args.trailing).write_bytes(tail)

    text = GclWriter(dec.tree_data, fonts_size=dec.fonts_size,
                     use_names=args.names).render()
    if args.output:
        Path(args.output).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
