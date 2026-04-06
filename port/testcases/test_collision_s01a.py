"""
test_collision_s01a.py — HZD (Hazard/Collision) data tests for s01a.

The HZD system provides wall segments, floor polygons, and event triggers.
These tests verify that valid collision data is present and within expected
coordinate bounds for the s01a (helipad) stage.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")

# s01a helipad is roughly a 10000×10000 PSX unit area; coords outside this
# range would indicate corrupted data.
MAX_COORD = 50000


def _log(name):
    return os.path.join(LOGDIR, name)


def test_hzd_maps_present():
    """At least one HZD map must be loaded for s01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp = c.get_collision()
        maps = resp.get("maps", [])

        print(f"    HZD maps: {len(maps)}")
        assert len(maps) > 0, "No HZD maps found after s01a load"


def test_walls_and_floors_present():
    """s01a must have both walls and floor polygons."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp   = c.get_collision()
        maps   = resp.get("maps", [])
        walls  = sum(len(m.get("walls",  [])) for m in maps)
        floors = sum(len(m.get("floors", [])) for m in maps)

        print(f"    walls={walls}  floors={floors}")
        assert walls  > 0, f"No walls in s01a HZD (walls={walls})"
        assert floors > 0, f"No floors in s01a HZD (floors={floors})"


def test_wall_coordinates_in_range():
    """Wall segment endpoints should be within the expected coordinate range."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp = c.get_collision()
        maps = resp.get("maps", [])
        bad  = []
        for m in maps:
            for wall in m.get("walls", []):
                for pt in (wall["p1"], wall["p2"]):
                    if abs(pt[0]) > MAX_COORD or abs(pt[1]) > MAX_COORD:
                        bad.append(pt)

        print(f"    walls checked, bad points: {len(bad)}")
        assert len(bad) == 0, f"Wall coords out of range: {bad[:3]}"


def test_floor_y_values_in_range():
    """Floor polygon Y (height) values should be within plausible range."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp = c.get_collision()
        maps = resp.get("maps", [])
        bad  = []
        for m in maps:
            for floor in m.get("floors", []):
                for corner in floor.get("corners", []):
                    if abs(corner[1]) > MAX_COORD:  # Y is index 1
                        bad.append(corner)

        print(f"    floors checked, bad Y values: {len(bad)}")
        assert len(bad) == 0, f"Floor Y values out of range: {bad[:3]}"


def test_hzd_bounds_nonzero():
    """HZD map bounding box should cover a non-trivial area."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp = c.get_collision()
        maps = resp.get("maps", [])
        assert maps, "No HZD maps"

        for m in maps:
            bounds = m.get("bounds", [[0, 0], [0, 0]])
            width  = abs(bounds[1][0] - bounds[0][0])
            depth  = abs(bounds[1][1] - bounds[0][1])
            print(f"    bounds={bounds}  width={width}  depth={depth}")
            assert width > 0 or depth > 0, f"HZD map has zero-area bounds: {bounds}"
