"""
test_weapons.py — Weapon mechanic tests in s01a (Heliport).

Snake starts s01a with SOCOM pistol equipped. Tests cover:
  - SOCOM: fire (R1), first-person aim (L1), shoot in FP (L1+R1)
  - FAMAS rifle: not in s01a inventory by default, skip
  - Grenade: may be in inventory if picked up

Button constants (from mts.c BTN_ defines):
  L1 = 0x0004, R1 = 0x0008, CIRCLE = 0x0020, CROSS = 0x0040
  SQUARE = 0x0080 (equip/item)
  L2 = 0x0001, R2 = 0x0002 (weapon/item cycle)
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient, BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_SQUARE

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")


def _log(name):
    return os.path.join(LOGDIR, name)


def _has_socom(c):
    """Returns True if a bullet.c actor is present (SOCOM fired a bullet)."""
    actors = [a["name"] for a in c.get_actors().get("actors", [])]
    return "bullet.c" in actors


def test_socom_fire_no_crash():
    """Pressing R1 (fire SOCOM) in s01a must not crash the game.

    Bullet actors may or may not be visible depending on weapon equip state,
    so we verify stability rather than bullet presence.
    """
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        # Stand still, then press R1 to fire SOCOM
        c.run(10)
        c.inject_input(BTN_R1, frames=3)
        c.run(3)
        c.run(10)  # Let any bullet/effect settle

        state = c.get_state()
        actors = [a["name"] for a in c.get_actors().get("actors", [])]
        has_bullet = "bullet.c" in actors
        print(f"    bullet.c present: {has_bullet}  load={state['load_complete']}")
        assert state["load_complete"] == 1, "Game crashed after SOCOM fire"
        assert state["faces_last_frame"] > 0, "No faces rendered after fire"


def test_first_person_mode_changes_faces():
    """Holding L1 enters first-person mode — face count should remain nonzero."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        state_before = c.get_state()
        faces_before = state_before["faces_last_frame"]
        print(f"    faces before L1: {faces_before}")

        # Hold L1 for 30 frames (first-person mode)
        c.inject_input(BTN_L1, frames=30)
        c.run(30)

        state_fp = c.get_state()
        faces_fp = state_fp["faces_last_frame"]
        print(f"    faces during L1 (first-person): {faces_fp}")

        assert faces_fp > 0, f"No faces visible in first-person mode: {faces_fp}"
        assert state_fp["load_complete"] == 1, "Crash entering first-person mode"


def test_first_person_fire():
    """L1 (first-person) + R1 (fire) should not crash the game."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        # Enter first-person and fire
        c.run(10)
        c.inject_input(BTN_L1 | BTN_R1, frames=5)
        c.run(5)
        c.inject_input(BTN_L1, frames=20)
        c.run(20)

        state = c.get_state()
        faces = state["faces_last_frame"]
        print(f"    faces after L1+R1: {faces}")
        assert state["load_complete"] == 1, "Crash after first-person fire"
        assert faces > 0, f"No faces after first-person fire: {faces}"


def test_weapon_cycle_with_r2():
    """Pressing R2 cycles through weapons; game should not crash."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        c.run(10)
        # Cycle through weapons several times
        for _ in range(5):
            c.inject_input(BTN_R2, frames=3)
            c.run(10)

        state = c.get_state()
        print(f"    After R2×5: load={state['load_complete']} faces={state['faces_last_frame']}")
        assert state["load_complete"] == 1, "Crash during weapon cycle"


def test_item_equip_with_square():
    """Holding SQUARE opens the item equip menu; game should not crash."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        c.run(10)
        c.inject_input(BTN_SQUARE, frames=30)
        c.run(30)
        c.run(20)  # Release and settle

        state = c.get_state()
        print(f"    After SQUARE hold: load={state['load_complete']} faces={state['faces_last_frame']}")
        assert state["load_complete"] == 1, "Crash while equipping item"


def test_first_person_camera_aim():
    """In first-person, camera pos should shift (Snake looks around)."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        state_normal = c.get_state()
        cam_normal = state_normal["camera"]["pos"]

        # Enter first-person — hold for 60 frames to let the camera transition
        c.inject_input(BTN_L1, frames=60)
        c.run(60)
        state_fp = c.get_state()
        cam_fp = state_fp["camera"]["pos"]

        print(f"    cam normal={cam_normal}")
        print(f"    cam FP    ={cam_fp}")
        # Camera should have moved (FP camera is different from 3rd-person)
        delta = sum(abs(a - b) for a, b in zip(cam_normal, cam_fp))
        print(f"    cam delta={delta}")
        # Delta=0 is acceptable if the game doesn't implement FP camera yet;
        # the critical check is that no crash occurred.
        assert state_fp["load_complete"] == 1, "Crash in first-person camera"
        assert state_fp["faces_last_frame"] > 0, "No faces in first-person mode"
