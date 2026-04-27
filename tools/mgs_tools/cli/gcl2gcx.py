#!/usr/bin/env python3
"""Compile .gcl source text to .gcx bytecode.

Usage:
    ./gcl2gcx.py input.gcl -o output.gcx
    ./gcl2gcx.py --trailing trailing.bin input.gcl -o output.gcx

Notes:
  - `.gcx` files normally have trailing font/data after the script body.
    Pass it via `--trailing` (raw bytes) if you need byte-exact output.
  - `--align4` pads to a 4-byte boundary (PSX convention). Default on.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mgs_tools.gcl.compile import GclComp
from mgs_tools.gcl.parser import parse
from mgs_tools.gcl.gcx import GclNode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help="path to a .gcl source file")
    ap.add_argument("-o", "--output", required=True, help="path for the .gcx output")
    ap.add_argument("--trailing", help="raw bytes to append after script body")
    ap.add_argument("--no-align4", action="store_true", help="skip 4-byte padding")
    args = ap.parse_args()

    src = Path(args.input).read_text(encoding="utf-8")
    try:
        tree = parse(src)
    except SyntaxError as e:
        print(f"parse error: {e}", file=sys.stderr)
        return 1

    if args.trailing:
        blob = Path(args.trailing).read_bytes()
        tree.append(GclNode({"FONTS": [blob.hex()]}))

    comp = GclComp(align4=not args.no_align4)
    data = comp.compile_file(tree)
    Path(args.output).write_bytes(bytes(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
