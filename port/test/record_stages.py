#!/usr/bin/env python3
"""
record_stages.py — Record input log files for stage navigation.

Navigates through the menu system and records .log files replayable in tests.

The menu flow (INTEGRAL build):
  1. Initial 'select': press DOWN (to move from TITLE), CIRCLE (confirm disk)
  2. Cinema intro plays → cancel.c fires on any button press via mts_read_pad(1)
  3. Stage select 'select1' appears
  4. Navigate with UP/DOWN to desired stage, confirm with CIRCLE

Run from the port/ directory:
    python3 test/record_stages.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from mgs_client import (
    MGSTestClient,
    BTN_CIRCLE, BTN_UP, BTN_DOWN, BTN_START,
    BTN_L1, BTN_R1, BTN_CROSS,
)

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")
SCREENSHOT_DIR = "/tmp/mgs_stage_screenshots"
os.makedirs(LOGDIR, exist_ok=True)
os.makedirs(SCREENSHOT_DIR, exist_ok=True)


def actors_names(c):
    resp = c.get_actors()
    return [a["name"] for a in resp.get("actors", [])]


def state_summary(c):
    s = c.get_state()
    return s.get("frame", 0), s.get("stage", "?"), s.get("load_complete", 0)


def screenshot(c, name):
    path = os.path.join(SCREENSHOT_DIR, f"{name}.bmp")
    try:
        c.screenshot(path)
        png = path.replace(".bmp", ".png")
        os.system(f"sips -s format png '{path}' --out '{png}' 2>/dev/null")
    except Exception:
        pass


def press(c, btn, hold=5, settle=20):
    c.inject_input(btn, frames=hold)
    c.run(hold + settle)


def wait_for_cancel_actor(c, timeout_frames=600, batch=30):
    """Run until cancel.c appears (cinema is playing and skippable)."""
    waited = 0
    while waited < timeout_frames:
        n = min(batch, timeout_frames - waited)
        c.run(n)
        waited += n
        if "cancel.c" in actors_names(c):
            return True
    return False


def wait_for_select_actor(c, timeout_frames=600, batch=30):
    """Run until select.c appears (stage select is active)."""
    waited = 0
    while waited < timeout_frames:
        n = min(batch, timeout_frames - waited)
        c.run(n)
        waited += n
        if "select.c" in actors_names(c):
            return True
    return False


def navigate_to_select1(c, log_name):
    """
    Navigate from title screen through disk select and cinema to stage select.
    Records input to log_name. Returns True if select1 loaded.

    Flow matches auto_input script in mts.c:
      1. Wait for select.c (initial title menu)
      2. DOWN×1 (select DISK1), CIRCLE (confirm)
      3. Wait for cinema (cancel.c appears)
      4. CIRCLE to skip cinema via cancel.c (mts_read_pad fix needed)
      5. Wait for select.c again (now it's select1 = stage list)
    """
    logpath = os.path.join(LOGDIR, f"nav_{log_name}.log")
    print(f"  Starting record → {logpath}")
    c.record_log(logpath)

    # Wait for initial select
    if not wait_for_select_actor(c, timeout_frames=300):
        print("  ERROR: initial select.c not found")
        return False, logpath

    f, stage, lc = state_summary(c)
    print(f"  Initial select at frame {f}: actors={actors_names(c)}")
    screenshot(c, f"{log_name}_00_initial")

    # Step 1: DOWN to move from TITLE to DISK1, then CIRCLE to confirm
    print("  [select] DOWN → CIRCLE (confirm disk)")
    press(c, BTN_DOWN, hold=5, settle=30)
    screenshot(c, f"{log_name}_01_after_down")
    press(c, BTN_CIRCLE, hold=5, settle=30)
    screenshot(c, f"{log_name}_02_after_circle1")

    # Step 2: Confirm selectd if it appeared
    c.run(60)
    screenshot(c, f"{log_name}_03_wait_selectd")
    f, stage, lc = state_summary(c)
    print(f"  After circle1, frame {f}: actors={actors_names(c)}")

    press(c, BTN_CIRCLE, hold=5, settle=30)
    screenshot(c, f"{log_name}_04_after_circle2")

    # Step 3: Wait for cinema to start (cancel.c appears)
    print("  Waiting for cinema (cancel.c)...")
    if wait_for_cancel_actor(c, timeout_frames=300):
        print("  Cinema started. Pressing CIRCLE to skip...")
        c.run(30)  # Let it stabilize
        press(c, BTN_CIRCLE, hold=5, settle=60)
        screenshot(c, f"{log_name}_05_after_skip")
        print("  Cinema skipped.")
    else:
        print("  No cancel.c found - maybe already at select1?")

    # Step 4: Wait for select1 (select.c reappears for stage list)
    print("  Waiting for select1 (stage select)...")
    if not wait_for_select_actor(c, timeout_frames=600):
        print("  ERROR: select1 not found after cinema skip")
        f, stage, lc = state_summary(c)
        print(f"  Current: frame={f} stage={stage} actors={actors_names(c)}")
        return False, logpath

    f, stage, lc = state_summary(c)
    print(f"  select1 active at frame {f}: actors={actors_names(c)}")
    screenshot(c, f"{log_name}_06_select1_ready")
    return True, logpath


def record_stage(stage_label, n_downs_from_select1):
    """
    Navigate from title to select1, then DOWN n times, CIRCLE to confirm.

    stage_label: label for log file name (e.g. "d00a")
    n_downs_from_select1: number of DOWN presses in select1 to reach the stage
    """
    print(f"\n=== Recording {stage_label} (n_downs={n_downs_from_select1}) ===")
    pkill_old()

    c = MGSTestClient()
    c.start(auto_input=False)
    try:
        ok, logpath = navigate_to_select1(c, stage_label)
        if not ok:
            print(f"  FAILED: could not reach select1")
            return False

        # Navigate to the target stage in select1
        for i in range(n_downs_from_select1):
            print(f"  [select1] DOWN #{i+1}/{n_downs_from_select1}")
            press(c, BTN_DOWN, hold=5, settle=20)
        screenshot(c, f"{stage_label}_07_before_confirm")

        print(f"  [select1] CIRCLE → confirm {stage_label}")
        press(c, BTN_CIRCLE, hold=5, settle=60)
        screenshot(c, f"{stage_label}_08_after_confirm")

        # Wait for stage to load
        print(f"  Waiting for {stage_label} to load...")
        c.run(300)
        f, stage, lc = state_summary(c)
        actors = actors_names(c)
        print(f"  Loaded: frame={f} stage={stage} lc={lc}")
        print(f"  Actors: {actors}")
        screenshot(c, f"{stage_label}_09_loaded")

        # Run a bit more for cinema stages to show content
        c.run(120)
        screenshot(c, f"{stage_label}_10_settled")

        c.stop_log()
        print(f"  Log: {logpath}")
        return True

    except Exception as e:
        import traceback
        print(f"  ERROR: {e}")
        traceback.print_exc()
        return False
    finally:
        c.stop()


def pkill_old():
    """Kill any leftover mgs processes."""
    os.system("pkill -9 mgs 2>/dev/null")
    time.sleep(0.5)


def main():
    print("=== MGS Stage Navigation Recorder ===")
    print(f"Logs → {LOGDIR}")
    print(f"Screenshots → {SCREENSHOT_DIR}\n")

    # Stage select1 list (INTEGRAL build, empirically determined):
    # The auto_input does 2 DOWNs from select1 and lands on s01a (Heliport).
    # Empirical list based on source/game/select.c DEV_EXE comment:
    #   0: TITLE (initial position?)
    #   1: D00A  - Loading dock cutscene
    #   2: S00A  - Loading dock gameplay
    #   3: D01A  - Heliport cutscene
    #   4: S01A  - Heliport gameplay  ← 2 DOWNs from auto_input
    #
    # NOTE: the starting index in select1 is probably 2 (S00A), so:
    #   DOWN 0 = S00A (index 2)
    #   DOWN 1 = D01A (index 3)
    #   DOWN 2 = S01A (index 4) ← matches auto_input
    # To reach D00A (index 1), need UP×1 from starting position.

    stages = [
        # (label, n_downs_from_select1_start)
        ("d01a",      1),   # D01A - Heliport cutscene
        ("s00a",      0),   # S00A - Loading dock (start position)
    ]

    results = {}
    for label, n_downs in stages:
        success = record_stage(label, n_downs)
        results[label] = success
        time.sleep(1)

    print("\n=== Results ===")
    for label, ok in results.items():
        print(f"  {label}: {'OK' if ok else 'FAILED'}")

    print(f"\nScreenshots: {SCREENSHOT_DIR}")


if __name__ == "__main__":
    main()
