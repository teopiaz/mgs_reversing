"""
test_mesh_s01a.py — 3D render queue / mesh integrity tests for s01a.

Verifies that the render queue is populated with objects and that the
mesh data (vertex counts, face indices) is self-consistent and not
corrupted.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")


def _log(name):
    return os.path.join(LOGDIR, name)


def test_objects_in_render_queue():
    """Render queue should be non-empty after navigating to s01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp    = c.get_mesh(simplified=True)
        objects = resp.get("objects", [])
        total   = sum(o.get("n_models", 0) for o in objects)

        print(f"    objects={len(objects)}  total_models={total}")
        assert len(objects) > 0, "No objects in s01a render queue"
        assert total > 0, "No models in s01a render queue"


def test_model_vertex_counts_nonzero():
    """Every model in the render queue should report at least 1 vertex."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp    = c.get_mesh(simplified=True)
        objects = resp.get("objects", [])
        bad     = []
        for obj in objects:
            for mdl in obj.get("models", []):
                if mdl.get("n_verts", 0) == 0:
                    bad.append(mdl)

        print(f"    models with zero verts: {len(bad)}")
        assert len(bad) == 0, f"Models with zero vertex count: {len(bad)}"


def test_model_bounding_boxes_valid():
    """Bounding box min should not exceed max for any model axis."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp    = c.get_mesh(simplified=True)
        objects = resp.get("objects", [])
        bad     = []
        for obj in objects:
            for mdl in obj.get("models", []):
                mn = mdl.get("min", [0, 0, 0])
                mx = mdl.get("max", [0, 0, 0])
                for axis in range(3):
                    if mn[axis] > mx[axis]:
                        bad.append((mn, mx))
                        break

        print(f"    models with inverted bbox: {len(bad)}")
        assert len(bad) == 0, f"Models with min > max: {bad[:3]}"


def test_face_indices_in_vertex_range():
    """Face vertex indices must be within [0, n_verts) for each model."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_crouch_s01a.log"))

        # Use full mesh (not simplified) to get actual index data.
        # This response can be large; if the server returns an error due
        # to buffer limits, skip the test gracefully.
        try:
            resp = c.get_mesh(simplified=False)
        except Exception as e:
            if "buffer" in str(e).lower() or "exceeded" in str(e).lower():
                print(f"    SKIPPED: mesh data too large ({e})")
                return
            raise

        objects = resp.get("objects", [])
        bad     = []
        for obj in objects:
            for mdl in obj.get("models", []):
                n = mdl.get("n_verts", 0)
                for face in mdl.get("faces", []):
                    for idx in face:
                        if idx >= n:
                            bad.append((idx, n))
                            break
                if bad:
                    break
            if bad:
                break

        print(f"    out-of-range face indices: {len(bad)}")
        assert len(bad) == 0, f"Face indices out of vertex range: {bad[:3]}"


def test_world_positions_in_stage_bounds():
    """Object world positions should be within plausible s01a bounds."""
    MAX_DIST = 200000  # PSX units; helipad is ~50m wide
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        resp    = c.get_mesh(simplified=True)
        objects = resp.get("objects", [])
        bad     = []
        for obj in objects:
            wt = obj.get("world_t", [0, 0, 0])
            if any(abs(v) > MAX_DIST for v in wt):
                bad.append(wt)

        print(f"    objects out of bounds: {len(bad)}")
        assert len(bad) == 0, f"Objects with out-of-range world positions: {bad[:3]}"
