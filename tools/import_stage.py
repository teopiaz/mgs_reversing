#!/usr/bin/env python3
"""Thin shim — keeps the historical entry-point path stable.
Real implementation lives in mgs_tools/cli/import_stage.py.

The editor's Reimport / Play buttons (port/editor/ed_ui.cpp) shell out
to this exact path — `python3 tools/import_stage.py …` — so the file
must remain here even after the codebase reorganization."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.import_stage import main

if __name__ == "__main__":
    sys.exit(main() or 0)
