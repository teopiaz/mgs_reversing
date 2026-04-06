"""
test_game_state.py — Core game state sanity tests.

Verifies that fundamental game variables (health, position, alert mode)
are in valid ranges and that the frame counter advances correctly.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")


def _log(name):
    return os.path.join(LOGDIR, name)


def test_snake_health_valid_after_load():
    """Snake health must be positive and ≤ max_health after stage load."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)

        state = c.get_state()
        hp    = state["snake"]["health"]
        maxhp = state["snake"]["max_health"]

        print(f"    hp={hp}/{maxhp}")
        assert hp > 0,      f"Snake health is zero or negative: {hp}"
        assert hp <= maxhp, f"Snake health exceeds max: {hp}/{maxhp}"
        assert maxhp > 0,   f"Max health is zero: {maxhp}"


def test_frame_counter_advances():
    """GV_Time (frame counter) should increase with each run() call."""
    with MGSTestClient() as c:
        c.wait_for_stage()

        s0 = c.get_state()
        c.run(30)
        s1 = c.get_state()
        c.run(30)
        s2 = c.get_state()

        f0, f1, f2 = s0["frame"], s1["frame"], s2["frame"]
        print(f"    frames: {f0} → {f1} → {f2}")
        assert f1 > f0, f"Frame did not advance: {f0} → {f1}"
        assert f2 > f1, f"Frame did not advance: {f1} → {f2}"


def test_no_alert_at_start():
    """Alert mode should be 0 (no alert) after a fresh stage load."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)

        state = c.get_state()
        alert = state["alert_mode"]
        print(f"    alert_mode={alert}")
        assert alert == 0, f"Unexpected alert mode at stage start: {alert}"


def test_snake_position_in_stage_after_load():
    """Snake world position should be non-trivially zero after navigating to s01a.

    On PSX, position (0,0,0) can be a valid starting point, but after navigating
    through the helipad log the position should reflect real movement.
    """
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_rendering_left_side_s01a.log"))

        pos = state["snake"]["pos"]
        print(f"    snake pos={pos}")
        # After 1751 frames of movement, snake should not be at absolute origin
        assert not (pos[0] == 0 and pos[1] == 0 and pos[2] == 0), (
            "Snake position is still (0,0,0) after log replay — likely not loaded"
        )


def test_stage_name_set_after_gameplay_load():
    """Stage name should be non-empty after navigating to a gameplay stage."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        state = c.get_state()
        stage = state.get("stage", "unknown")
        print(f"    stage='{stage}'")
        assert stage and stage != "unknown", (
            f"Stage name not set after gameplay load: '{stage}'"
        )


def test_load_complete_persists():
    """load_complete should remain 1 after a stage has fully loaded."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(30)

        state = c.get_state()
        lc = state["load_complete"]
        print(f"    load_complete={lc}")
        assert lc == 1, f"load_complete dropped after stage load: {lc}"
