#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/gcx2gcl.py for the implementation.

Decompile a .gcx bytecode blob back to .gcl text. Promoted from the
old `port/gcl_tools/gcx2gcl.py`."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.gcx2gcl import main

if __name__ == "__main__":
    sys.exit(main() or 0)
