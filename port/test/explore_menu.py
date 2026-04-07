#!/usr/bin/env python3
"""
explore_menu.py — Systematically explore the stage selection menu to find
navigation paths to d00a, d01a, d01b and other stages.

Usage:
    cd port/test && python explore_menu.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from mgs_client import MGSTestClient, BTN_CIRCLE, BTN_UP, BTN_DOWN, BTN_START, BTN_CROSS

SCREENSHOT_DIR = "/tmp/mgs_screenshots"

os.makedirs(SCREENSHOT_DIR, exist_ok=True)


def actors_names(c):
    resp = c.get_actors()
    return [a["name"] for a in resp.get("actors", [])]


def state_summary(c):
    state = c.get_state()
    return {
        "frame": state.get("frame"),
        "stage": state.get("stage", "unknown"),
        "load_complete": state.get("load_complete"),
        "faces": state.get("faces_last_frame", 0),
    }


def screenshot(c, name):
    path = os.path.join(SCREENSHOT_DIR, f"{name}.bmp")
    try:
        c.screenshot(path)
        png_path = path.replace(".bmp", ".png")
        os.system(f"sips -s format png '{path}' --out '{png_path}' 2>/dev/null")
        print(f"      Screenshot: {png_path}")
    except Exception as e:
        print(f"      Screenshot failed: {e}")


def press(c, btn, hold_frames=3, settle_frames=30):
    """Press a button and let the game settle."""
    c.inject_input(btn, frames=hold_frames)
    c.run(hold_frames)
    c.run(settle_frames)


def explore():
    print("=== MGS Menu Explorer ===\n")
    print(f"Screenshots in: {SCREENSHOT_DIR}\n")

    c = MGSTestClient()
    c.start(auto_input=False)
    try:
        # Wait for any initial load
        print("Waiting for game to load...")
        c.run(120)

        st = state_summary(c)
        actors = actors_names(c)
        print(f"Frame {st['frame']}: stage={st['stage']} load_complete={st['load_complete']}")
        print(f"  Actors: {actors}")
        screenshot(c, "00_initial")

        # Step 1: Press CIRCLE to get past title screen
        print("\n--- Step 1: CIRCLE ---")
        press(c, BTN_CIRCLE, hold_frames=3, settle_frames=60)
        st = state_summary(c)
        actors = actors_names(c)
        print(f"Frame {st['frame']}: stage={st['stage']}")
        print(f"  Actors: {actors}")
        screenshot(c, "01_after_circle_1")

        # Step 2: CIRCLE again
        print("\n--- Step 2: CIRCLE ---")
        press(c, BTN_CIRCLE, hold_frames=3, settle_frames=60)
        st = state_summary(c)
        actors = actors_names(c)
        print(f"Frame {st['frame']}: stage={st['stage']}")
        print(f"  Actors: {actors}")
        screenshot(c, "02_after_circle_2")

        # Step 3: CIRCLE again
        print("\n--- Step 3: CIRCLE ---")
        press(c, BTN_CIRCLE, hold_frames=3, settle_frames=60)
        st = state_summary(c)
        actors = actors_names(c)
        print(f"Frame {st['frame']}: stage={st['stage']}")
        print(f"  Actors: {actors}")
        screenshot(c, "03_after_circle_3")

        # Step 4: Try START to skip any cinema/demo
        print("\n--- Step 4: START ---")
        press(c, BTN_START, hold_frames=3, settle_frames=90)
        st = state_summary(c)
        actors = actors_names(c)
        print(f"Frame {st['frame']}: stage={st['stage']}")
        print(f"  Actors: {actors}")
        screenshot(c, "04_after_start_1")

        # Step 5: CIRCLE again
        print("\n--- Step 5: CIRCLE ---")
        press(c, BTN_CIRCLE, hold_frames=3, settle_frames=90)
        st = state_summary(c)
        actors = actors_names(c)
        print(f"Frame {st['frame']}: stage={st['stage']}")
        print(f"  Actors: {actors}")
        screenshot(c, "05_after_circle_4")

        # Now try navigating with DOWN to scan the stage list
        print("\n=== Scanning stage list with DOWN presses ===")
        for i in range(25):
            press(c, BTN_DOWN, hold_frames=3, settle_frames=30)
            st = state_summary(c)
            actors_now = actors_names(c)
            print(f"  DOWN #{i+1}: frame={st['frame']} stage={st['stage']} faces={st['faces']}")
            print(f"    Actors: {actors_now}")
            screenshot(c, f"scan_down_{i+1:02d}")

            if st['stage'] not in ('unknown', 'select', 'selectd', 'select1', ''):
                print(f"  >>> Reached gameplay stage: {st['stage']}")
                break

        print("\n=== Scan complete ===")

    finally:
        c.stop()


if __name__ == "__main__":
    explore()
