"""
test_camera_s00a.py — Camera behaviour tests for the s00a stage.

Uses logs recorded while crouching and lean-crouching in s00a to verify
the camera continues tracking Snake in those movement states.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")
CAMERA_FOLLOW_THRESHOLD = 800


def _log(name):
    return os.path.join(LOGDIR, name)


def test_camera_tracks_during_crouch_s00a():
    """Camera X should stay close to Snake X while crouching in s00a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_camera_crouching_s00a_2.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera lost Snake during crouch in s00a: delta={delta}"
        )


def test_camera_tracks_lean_and_crouch_s00a():
    """Camera should follow Snake through a lean+crouch sequence in s00a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_camera_leaning_crouching_s00a.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera lost Snake during lean+crouch in s00a: delta={delta}"
        )


def test_faces_visible_during_crouch_s00a():
    """At least 50 faces should be drawn while crouching in s00a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_camera_crouching_s00a_2.log"))

        faces = state["faces_last_frame"]
        print(f"    faces during crouch (s00a): {faces}")
        assert faces >= 50, f"Too few faces while crouching in s00a: {faces}"
