#!/usr/bin/env python3
"""
trace_auto_input.py — Follow the auto_input sequence step by step with screenshots.
Helps understand which menu screens appear at each auto_input frame.
"""

import os
import sys
sys.path.insert(0, os.path.dirname(__file__))
from mgs_client import MGSTestClient

SCREENSHOT_DIR = "/tmp/mgs_auto_trace"
os.makedirs(SCREENSHOT_DIR, exist_ok=True)


def actors_names(c):
    return [a["name"] for a in c.get_actors().get("actors", [])]


def screenshot(c, name):
    path = os.path.join(SCREENSHOT_DIR, f"{name}.bmp")
    c.screenshot(path)
    os.system(f"sips -s format png '{path}' --out '{path.replace('.bmp', '.png')}' 2>/dev/null")
    return path


# Key frames in the auto_input script
KEYFRAMES = [30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 360, 420, 480, 540, 600]

c = MGSTestClient()
c.start(auto_input=True)  # Let auto_input run
try:
    prev_frame = 0
    for target in KEYFRAMES:
        frames_to_run = target - prev_frame
        c.run(frames_to_run)
        prev_frame = target
        state = c.get_state()
        actors = actors_names(c)
        name = f"frame_{target:04d}"
        screenshot(c, name)
        print(f"Frame {target:4d}: stage={state.get('stage','?'):<12} lc={state.get('load_complete',0)}  actors={actors}")

    # Run more to see final stage
    c.run(300)
    state = c.get_state()
    actors = actors_names(c)
    screenshot(c, "frame_final")
    print(f"\nFinal: frame={state.get('frame')} stage={state.get('stage')} actors={actors}")
finally:
    c.stop()

print(f"\nScreenshots in: {SCREENSHOT_DIR}")
