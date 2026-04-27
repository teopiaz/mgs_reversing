#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/show_glyphs.py for the implementation.

Render the stage-font glyphs used by a .gcx (Japanese-text debugging)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.show_glyphs import main

if __name__ == "__main__":
    sys.exit(main() or 0)
