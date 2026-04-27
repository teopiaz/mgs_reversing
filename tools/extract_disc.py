#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/extract_disc.py for the implementation.

Replaces the old `tools/extract_disc_assets.py`."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.extract_disc import main

if __name__ == "__main__":
    sys.exit(main() or 0)
