"""
test_actors.py — Actor system tests.

Verifies that the actor list is populated correctly and that actors have
valid metadata after a stage loads.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
from mgs_client import MGSTestClient


def test_menu_actors_present():
    """At least 3 actors should be active after the menu stage loads."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)

        resp = c.get_actors()
        actors = resp["actors"]
        active = [a for a in actors if a["active"]]

        print(f"    total={len(actors)}  active={len(active)}")
        assert len(active) >= 3, f"Too few active actors: {len(active)}"


def test_actors_have_names():
    """Every actor in the list should have a non-empty filename."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)

        resp = c.get_actors()
        actors = resp["actors"]
        unnamed = [a for a in actors if not a.get("name") or a["name"] == "???"]

        print(f"    total={len(actors)}  unnamed={len(unnamed)}")
        assert len(unnamed) == 0, f"Actors with missing names: {unnamed}"


def test_actor_count_reasonable():
    """Total actor count should be sane (< 500) — overflow/corruption guard."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)

        resp = c.get_actors()
        actors = resp["actors"]

        print(f"    total actors: {len(actors)}")
        assert len(actors) < 500, f"Suspiciously high actor count: {len(actors)}"


def test_actors_have_valid_priority():
    """All actors should report a priority in the valid range [0, 6]."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run(60)

        resp = c.get_actors()
        actors = resp["actors"]
        bad = [a for a in actors if not (0 <= a.get("priority", -1) <= 6)]

        assert len(bad) == 0, f"Actors with invalid priority: {bad}"
