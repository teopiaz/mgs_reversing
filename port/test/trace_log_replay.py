#!/usr/bin/env python3
"""
trace_log_replay.py — Trace what happens when a log file is replayed,
frame by frame with actor changes and screenshots.
"""

import os
import sys
sys.path.insert(0, os.path.dirname(__file__))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")
SCREENSHOT_DIR = "/tmp/mgs_replay_trace"
os.makedirs(SCREENSHOT_DIR, exist_ok=True)

LOG = os.path.join(LOGDIR, "1_rendering_left_side_s01a.log")


def actors_names(c):
    return [a["name"] for a in c.get_actors().get("actors", [])]


def screenshot(c, name):
    path = os.path.join(SCREENSHOT_DIR, f"{name}.bmp")
    c.screenshot(path)
    os.system(f"sips -s format png '{path}' --out '{path.replace('.bmp', '.png')}' 2>/dev/null")


c = MGSTestClient()
c.start(auto_input=False)
try:
    # Wait for initial select stage (same as tests)
    waited = c.wait_for_stage()
    state = c.get_state()
    print(f"After wait_for_stage ({waited} frames): stage={state.get('stage')} actors={actors_names(c)}")
    screenshot(c, "00_before_replay")

    # Start replay (resets internal frame counter to 0)
    c.replay_log(LOG)
    print(f"\nReplay started: {os.path.basename(LOG)}")

    # Track frame by frame
    prev_actors = None
    prev_stage = None
    check_frames = list(range(10, 110, 5)) + list(range(110, 500, 20)) + list(range(500, 1800, 50))

    global_frame = waited
    for target_log_frame in check_frames:
        frames_needed = target_log_frame - (global_frame - waited)
        if frames_needed > 0:
            c.run(frames_needed)
            global_frame += frames_needed

        state = c.get_state()
        actors = actors_names(c)
        rs = state.get("replay_status", {})
        log_frame = rs.get("processed_inputs", 0)

        changed = (actors != prev_actors) or (state.get("stage") != prev_stage)
        if changed:
            label = f"lf{target_log_frame:04d}"
            screenshot(c, label)
            print(f"LogFrame ~{target_log_frame:4d} (gf={state['frame']:4d}): "
                  f"stage={state.get('stage','?'):<12} "
                  f"load={state.get('load_complete',0)} "
                  f"faces={state.get('faces_last_frame',0):4d}  "
                  f"actors={actors}")
            prev_actors = actors
            prev_stage = state.get("stage")

    print(f"\nFinal state: {c.get_state()}")

finally:
    c.stop()

print(f"\nScreenshots in: {SCREENSHOT_DIR}")
