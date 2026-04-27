#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/derive_table.py for the implementation.

Aligns WantedThing.gcl text vs Rex.gcx bytecode to derive Japanese
character → byte mappings. Output feeds mgs_tools/gcl/glyphs/chars_derived.py."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.derive_table import main

if __name__ == "__main__":
    sys.exit(main() or 0)
