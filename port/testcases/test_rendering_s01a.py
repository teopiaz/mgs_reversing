"""
test_rendering_s01a.py — Rendering geometry tests for the s01a (helipad) stage.

Checks that sufficient faces are drawn in different movement states and
camera positions. A face-count drop to near zero indicates missing geometry
(missing draw calls, culling bug, or rendering pipeline breakage).
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")


def _log(name):
    return os.path.join(LOGDIR, name)


def test_faces_visible_left_side():
    """At least 100 faces must be drawn when Snake is at the left edge of s01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_rendering_left_side_s01a.log"))

        faces = state["faces_last_frame"]
        print(f"    faces drawn (left side): {faces}")
        assert faces >= 100, f"Too few faces at left side: {faces}"


def test_faces_visible_during_crouch():
    """At least 80 faces must be drawn while crouching in s01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a.log"))

        faces = state["faces_last_frame"]
        print(f"    faces drawn (crouch): {faces}")
        assert faces >= 80, f"Too few faces while crouching: {faces}"


def test_faces_visible_extended_crouch():
    """Face count should remain >= 80 throughout an extended crouch sequence."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a_2.log"))

        faces = state["faces_last_frame"]
        print(f"    faces drawn (extended crouch): {faces}")
        assert faces >= 80, f"Geometry lost during extended crouch: {faces}"


def test_face_count_nonzero_after_stage_load():
    """Some faces should be visible immediately after s01a loads."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        # Navigate to s01a via a short log, then check a few frames in
        state = c.run_log(_log("1_crouch_s01a.log"))

        faces = state["faces_last_frame"]
        objs  = state["objs_count"]
        print(f"    faces={faces}  objs_in_queue={objs}")
        assert faces > 0, "No faces drawn after s01a load"
        assert objs  > 0, "No objects in render queue after s01a load"
