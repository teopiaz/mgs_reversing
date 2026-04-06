"""
test_cutscenes.py — Tests for cutscene stages (d01a).

d01a is the Heliport cutscene: Snake arrives by sea, exterior shots of
Shadow Moses. These tests verify that the cinema actors load and render
correctly without crashing.

The nav_d01a.log navigates: initial select → DOWN×1 → CIRCLE → confirms
d01a in select1. Cinema actors (demothrd, jimctrl, cinema) spawn.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient

LOGDIR = os.path.join(os.path.dirname(__file__), "..", "testcase")

CINEMA_ACTORS = {"strctrl.c", "demothrd.c", "jimctrl.c", "cinema.c"}


def _log(name):
    return os.path.join(LOGDIR, name)


def test_d01a_cinema_actors_present():
    """Cinema actors (strctrl, demothrd, jimctrl) must be present in d01a."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("nav_d01a.log"))

        actors = [a["name"] for a in c.get_actors().get("actors", [])]
        present = CINEMA_ACTORS & set(actors)
        print(f"    cinema actors present: {present}")
        assert "strctrl.c" in actors, f"strctrl.c missing from actors: {actors}"
        assert "demothrd.c" in actors, f"demothrd.c missing: {actors}"
        assert "jimctrl.c" in actors, f"jimctrl.c missing: {actors}"


def test_d01a_faces_drawn():
    """At least 50 faces must be visible during d01a cutscene."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("nav_d01a.log"))

        faces = state["faces_last_frame"]
        print(f"    d01a faces: {faces}")
        assert faces >= 50, f"Too few faces in d01a cutscene: {faces}"


def test_d01a_objects_in_render_queue():
    """d01a must have objects in the render queue (cinema has 3D world)."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("nav_d01a.log"))

        objs = state["objs_count"]
        print(f"    d01a objs_count={objs}")
        assert objs > 0, f"No objects in d01a render queue: objs_count={objs}"


def test_d01a_no_crash_after_load():
    """d01a should run 200 frames after loading without crash/disconnect."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("nav_d01a.log"))

        # Run 200 more frames — any crash would raise ConnectionError
        c.run(200)
        state = c.get_state()
        print(f"    d01a survived 200 extra frames: frame={state['frame']}")
        assert state["load_complete"] == 1, "load_complete dropped mid-cinema"


def test_d01a_load_complete_persists():
    """load_complete must remain 1 throughout d01a playback."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("nav_d01a.log"))

        lc = state["load_complete"]
        print(f"    load_complete={lc}")
        assert lc == 1, f"load_complete not 1 in d01a: {lc}"
