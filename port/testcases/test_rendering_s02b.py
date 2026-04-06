"""
test_rendering_s02b.py — Rendering geometry tests for the s02b (hangar) stage.

The hangar has a large interior with floor geometry and ceiling structures.
These tests verify that sufficient faces are drawn and the camera is working.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")
CAMERA_FOLLOW_THRESHOLD = 1000  # Hangar is wider, allow more slack


def _log(name):
    return os.path.join(LOGDIR, name)


def test_faces_visible_hangar():
    """At least 100 faces must be drawn in the s02b hangar interior."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("2_rendering_hangar_floor_s02b.log"))

        faces = state["faces_last_frame"]
        print(f"    faces drawn (hangar): {faces}")
        assert faces >= 100, f"Too few faces in hangar: {faces}"


def test_camera_tracks_snake_hangar():
    """Camera X should stay within threshold of Snake X in the hangar."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("2_rendering_hangar_floor_s02b.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera too far from Snake in hangar: delta={delta}"
        )


def test_render_queue_nonempty_hangar():
    """Render queue must contain objects in the hangar."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("2_rendering_hangar_floor_s02b.log"))

        resp    = c.get_mesh(simplified=True)
        objects = resp.get("objects", [])
        total   = sum(o.get("n_models", 0) for o in objects)

        print(f"    render objects={len(objects)}  total_models={total}")
        assert len(objects) > 0, "No objects in render queue in hangar"
        assert total > 0, "No models in render queue in hangar"
