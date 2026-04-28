#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/extract_dmo.py for the implementation."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.extract_dmo import main

if __name__ == "__main__":
    sys.exit(main() or 0)
