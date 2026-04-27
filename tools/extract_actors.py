#!/usr/bin/env python3
"""Thin shim — see mgs_tools/cli/extract_actors.py for the implementation.

Replaces the old `tools/extract_gcl_actors.py`. Pass `--batch` to
walk every decompiled scenerio.gcl + demo.gcl under
`port/{gcl/decompiled,overlays}/` and emit per-stage JSON into
`port/editor/data/` — the role of the legacy build_editor_data.py."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mgs_tools.cli.extract_actors import main

if __name__ == "__main__":
    sys.exit(main() or 0)
