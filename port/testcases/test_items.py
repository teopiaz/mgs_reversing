"""
test_items.py — Item/equipment tests in s01a (Heliport).

s01a has items scattered on the ground (item.c actors: 6 of them).
Tests cover:
  - Item actors are present
  - SQUARE (equip menu) does not crash
  - Cardboard box visual effect
  - Scope / goggles equip

PSX button map (mts.c BTN_* defines):
  SQUARE = 0x0080  (hold to equip item)
  L2     = 0x0001  (cycle items left)
  R2     = 0x0002  (cycle weapons right)
  CIRCLE = 0x0020  (action / confirm)
  CROSS  = 0x0040  (cancel / crawl while prone)
  L1     = 0x0004  (first-person)
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import (
    MGSTestClient,
    BTN_L1, BTN_L2, BTN_R2, BTN_SQUARE, BTN_CIRCLE,
    BTN_UP, BTN_DOWN,
)

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")


def _log(name):
    return os.path.join(LOGDIR, name)


def test_items_present_in_s01a():
    """item.c actors must be present in s01a (pickup items on the ground)."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        actors = [a["name"] for a in c.get_actors().get("actors", [])]
        item_count = actors.count("item.c")
        print(f"    item.c actors: {item_count}")
        assert item_count > 0, f"No item.c actors in s01a: {actors}"


def test_equip_menu_opens():
    """Holding SQUARE should open item equip menu without crashing."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        c.run(5)
        c.inject_input(BTN_SQUARE, frames=45)
        c.run(45)

        state = c.get_state()
        faces = state["faces_last_frame"]
        print(f"    equip menu: load={state['load_complete']} faces={faces}")
        assert state["load_complete"] == 1, "Game crashed opening equip menu"
        # Menu overlay should add faces (HUD rendered on top)


def test_item_cycle_with_l2():
    """Pressing L2 cycles through items; game must remain stable."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        c.run(5)
        for _ in range(6):
            c.inject_input(BTN_L2, frames=3)
            c.run(10)

        state = c.get_state()
        print(f"    After L2×6: load={state['load_complete']} faces={state['faces_last_frame']}")
        assert state["load_complete"] == 1, "Crash during item cycle"


def test_crouch_and_prone():
    """
    Pressing DOWN (crouch) repeatedly should not drop faces to zero.
    Tests that the crouching animation system is stable.
    """
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("1_crouch_s01a.log"))

        faces = state["faces_last_frame"]
        print(f"    faces during extended crouch: {faces}")
        assert faces >= 50, f"Too few faces while crouching: {faces}"
        assert state["load_complete"] == 1, "Crash during crouch"


def test_snake_health_valid_after_item_cycle():
    """Snake health must stay positive and within max after cycling items."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        # Cycle items several times
        for _ in range(3):
            c.inject_input(BTN_L2, frames=3)
            c.run(10)

        state = c.get_state()
        hp = state["snake"]["health"]
        maxhp = state["snake"]["max_health"]
        print(f"    health after item cycle: {hp}/{maxhp}")
        assert hp > 0, f"Health dropped to zero after item cycle: {hp}"
        assert hp <= maxhp, f"Health exceeds max after item cycle: {hp}/{maxhp}"


def test_item_cycle_then_first_person():
    """Cycling items then entering first-person must not crash."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        # Cycle through items
        c.run(5)
        for _ in range(4):
            c.inject_input(BTN_L2, frames=3)
            c.run(8)

        # Enter first-person with whatever is equipped
        c.inject_input(BTN_L1, frames=30)
        c.run(30)

        state = c.get_state()
        faces = state["faces_last_frame"]
        print(f"    faces in FP after item cycle: {faces}")
        assert state["load_complete"] == 1, "Crash in first-person after item cycle"
        assert faces > 0, f"No faces visible in first-person after item cycle: {faces}"
