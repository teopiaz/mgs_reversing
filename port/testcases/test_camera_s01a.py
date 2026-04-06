"""
test_camera_s01a.py — Camera behaviour tests for the s01a (helipad) stage.

Replays recorded input logs and asserts that the camera stays close to Snake
in both normal movement and while crouching.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")

# Camera must stay within this many PSX units of Snake horizontally
CAMERA_FOLLOW_THRESHOLD = 800


def _log(name):
    return os.path.join(LOGDIR, name)


def test_camera_tracks_snake_left_side():
    """Camera X should stay within threshold of Snake X at left edge of helipad."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_rendering_left_side_s01a.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera too far from Snake: delta={delta}"
        )


def test_camera_tracks_snake_during_crouch():
    """Camera should follow Snake even while crouching."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera lost Snake during crouch: delta={delta}"
        )


def test_camera_y_stays_above_floor():
    """Camera Y should not be buried underground (Y < -100000 is a crash/bug sign)."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_rendering_left_side_s01a.log"))

        cam_y = state["camera"]["pos"][1]
        print(f"    camera Y = {cam_y}")
        assert cam_y > -100000, f"Camera Y suspiciously low: {cam_y}"


def test_camera_clip_distance_nonzero():
    """Clip distance must be non-zero (zero means nothing would render)."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a.log"))

        clip = state["camera"]["clip_dist"]
        print(f"    clip_dist={clip}")
        assert clip != 0, "Clip distance is zero — nothing would render"
