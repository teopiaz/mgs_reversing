#!/usr/bin/env python3
"""Quick verification that nav_*.log files load the expected stages."""

import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")
SCRDIR = "/tmp/mgs_verify"
os.makedirs(SCRDIR, exist_ok=True)

LOGS = [
    ("nav_s00a.log",  "s00a"),
    ("nav_d01a.log",  "d01a"),
]

for logname, expected in LOGS:
    logpath = os.path.join(LOGDIR, logname)
    print(f"\n--- {logname} ---")

    c = MGSTestClient()
    c.start(auto_input=False)
    try:
        c.wait_for_stage()
        state = c.run_log(logpath)
        stage = state.get("stage", "?")
        lc    = state.get("load_complete", 0)
        faces = state.get("faces_last_frame", 0)
        actors = [a["name"] for a in c.get_actors().get("actors", [])]

        bmp = os.path.join(SCRDIR, f"{expected}_verify.bmp")
        c.screenshot(bmp)
        os.system(f"sips -s format png '{bmp}' --out '{bmp.replace('.bmp','.png')}' 2>/dev/null")

        print(f"  stage='{stage}' lc={lc} faces={faces}")
        print(f"  actors={actors[:6]}")
    except Exception as e:
        print(f"  ERROR: {e}")
    finally:
        c.stop()
        import time; time.sleep(0.5)
