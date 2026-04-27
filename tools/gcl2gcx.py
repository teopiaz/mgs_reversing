#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/gcl2gcx.py for the implementation.

Compile a .gcl text source into a .gcx bytecode blob. Promoted from
the old `port/gcl_tools/gcl2gcx.py`. Invoked by
`port/overlays/compile.sh` to round-trip every disc overlay's GCL."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.gcl2gcx import main

if __name__ == "__main__":
    sys.exit(main() or 0)
