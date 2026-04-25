#!/usr/bin/env python3
"""Run extract_gcl_actors over every stage with a decompiled scenerio.gcl.

Looks in two places:
  port/overlays/<stage>/scenerio.gcl     (active overlays)
  port/gcl/decompiled/<stage>/scenerio.gcl   (full decompile dump)

The active overlay wins if both exist (it's the one the engine actually
loads). Output files land in port/editor/data/<stage>_actors.tsv (+ .json).
"""

import subprocess
import sys
from pathlib import Path

THIS = Path(__file__).resolve()
REPO = THIS.parent.parent
EXTRACT = THIS.parent / "extract_gcl_actors.py"
OUT_DIR = REPO / "port" / "editor" / "data"


def discover():
    """Return {stage_name: [path_to_scenerio_gcl (and demo.gcl if present)]}.
       overlays/ wins over gcl/decompiled/ when both have a stage."""
    stages = {}
    for base in (REPO / "port" / "gcl" / "decompiled",
                 REPO / "port" / "overlays"):
        if not base.is_dir():
            continue
        for child in base.iterdir():
            paths = []
            for fname in ("scenerio.gcl", "demo.gcl"):
                p = child / fname
                if p.is_file():
                    paths.append(p)
            if paths:
                stages[child.name] = paths
    return stages


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    stages = discover()
    if not stages:
        print("no stages with decompiled scenerio.gcl found", file=sys.stderr)
        return 1

    ok, fail = 0, 0
    for name in sorted(stages):
        gcls = stages[name]
        out = OUT_DIR / f"{name}_actors.json"
        cmd = [sys.executable, str(EXTRACT)]
        for g in gcls:
            cmd += ["--input", str(g)]
        cmd += ["--output", str(out)]
        try:
            subprocess.run(cmd, check=True, capture_output=True)
            ok += 1
        except subprocess.CalledProcessError as e:
            print(f"  {name}: FAILED — {e.stderr.decode().strip().splitlines()[-1] if e.stderr else e}")
            fail += 1
    print(f"generated {ok} stages, {fail} failed -> {OUT_DIR}")
    return 0 if fail == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
