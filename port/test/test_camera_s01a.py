"""
test_camera_s01a.py — Camera and rendering tests for the s01a (helipad) stage.

These tests replay the existing input logs and verify that the camera tracks
Snake correctly and that sufficient geometry is rendered.

Run with:
    python testcase_runner.py test_camera_s01a.py
Or directly:
    python test_camera_s01a.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from mgs_client import MGSTestClient


TESTCASE_DIR = os.path.join(os.path.dirname(__file__), "..", "testcase")

# Camera should stay within this many PSX units of snake horizontally
CAMERA_FOLLOW_THRESHOLD = 800  # ~8 metres in game units


def _log(name):
    return os.path.join(TESTCASE_DIR, name)


def test_camera_tracks_snake_left_side():
    """Camera X should stay within threshold of snake X at end of left-side log."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_rendering_left_side_s01a.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera too far from snake: delta={delta} "
            f"(cam_x={cam_x} snake_x={snake_x})"
        )


def test_faces_visible_left_side():
    """At least 100 faces must be drawn when snake is at left edge of helipad."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_rendering_left_side_s01a.log"))

        faces = state["faces_last_frame"]
        print(f"    faces drawn: {faces}")
        assert faces >= 100, (
            f"Too few faces drawn: {faces} (expected >= 100)"
        )


def test_crouch_does_not_lose_geometry():
    """Crouching should not cause visible faces to drop below 80."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a.log"))

        faces = state["faces_last_frame"]
        print(f"    faces drawn while crouching: {faces}")
        assert faces >= 80, (
            f"Too few faces drawn while crouching: {faces}"
        )


def test_camera_tracks_snake_during_crouch():
    """Camera should follow snake even while crouching (s01a)."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x}  snake_x={snake_x}  delta={delta}")
        assert delta < CAMERA_FOLLOW_THRESHOLD, (
            f"Camera lost track during crouch: delta={delta}"
        )


def test_actors_present_after_load():
    """At least 3 actors should be active after stage load."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)  # settle for 1 second

        resp = c.get_actors()
        actors = resp["actors"]
        active = [a for a in actors if a["active"]]

        print(f"    total actors: {len(actors)}  active: {len(active)}")
        assert len(active) >= 3, (
            f"Too few active actors: {len(active)} (expected >= 3)"
        )


def test_collision_data_present():
    """HZD collision should have walls and floors in s01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        # Navigate to s01a via a known replay log so HZD data is present.
        # The select/menu stage has no collision — gameplay stages do.
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp = c.get_collision()
        maps = resp.get("maps", [])
        assert len(maps) > 0, "No HZD maps found"

        total_walls  = sum(len(m.get("walls",  [])) for m in maps)
        total_floors = sum(len(m.get("floors", [])) for m in maps)

        print(f"    maps={len(maps)}  walls={total_walls}  floors={total_floors}")
        assert total_walls  > 0, f"No walls in HZD (total_walls={total_walls})"
        assert total_floors > 0, f"No floors in HZD (total_floors={total_floors})"


def test_mesh_objects_present():
    """Render queue should contain objects with vertices in s01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        # Navigate to s01a via a known replay log so mesh data is present.
        # The select/menu stage has no 3D geometry — gameplay stages do.
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp = c.get_mesh(simplified=True)
        objects = resp.get("objects", [])
        total_models = sum(o.get("n_models", 0) for o in objects)

        print(f"    render objects: {len(objects)}  total models: {total_models}")
        assert len(objects) > 0, "No objects in render queue"
        assert total_models > 0, "No models in render queue"


if __name__ == "__main__":
    import traceback

    tests = [
        test_camera_tracks_snake_left_side,
        test_faces_visible_left_side,
        test_crouch_does_not_lose_geometry,
        test_camera_tracks_snake_during_crouch,
        test_actors_present_after_load,
        test_collision_data_present,
        test_mesh_objects_present,
    ]

    passed = failed = 0
    for fn in tests:
        print(f"\n--- {fn.__name__} ---")
        if fn.__doc__:
            print(f"    {fn.__doc__.strip().splitlines()[0]}")
        try:
            fn()
            print("    PASS")
            passed += 1
        except Exception as e:
            print(f"    FAIL: {e}")
            traceback.print_exc()
            failed += 1

    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)
