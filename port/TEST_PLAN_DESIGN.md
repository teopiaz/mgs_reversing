# MGS Port — Test Plan Design

This document defines **what** to test, **why**, and **how** using the socket-based
test harness described in `TESTING_PLAN.md`. It is the reference for writing and
extending the test suite in `port/testcases/`.

---

## Guiding Principles

1. **Regression safety.** Every test must catch a real class of bug that the port
   can introduce: crashes, rendering loss, broken collision, wrong camera, corrupt
   mesh data, or state corruption. No tests for the sake of coverage.

2. **Deterministic replay.** Tests navigate to a known state by replaying a `.log`
   file (recorded human input) or by using `inject_input` from a known starting
   point. The game is single-threaded and frame-deterministic, so replay is exact.

3. **Crash vs. correctness.** Many port subsystems are incomplete. A test that
   asserts "the game didn't crash and faces > 0" is more valuable than one that
   asserts an exact face count that will change with every rendering fix. Prefer
   stability checks over precise value checks.

4. **One process per test.** Each `test_*()` function creates its own
   `MGSTestClient`, starts a fresh game process, and tears it down. This costs
   ~17 seconds per test but guarantees full isolation. The runner kills stale
   processes between tests.

---

## Test Infrastructure

| Component | Path | Role |
|-----------|------|------|
| Harness server | `port/test_server.c` | C socket server, called once/frame |
| Python client | `port/test/mgs_client.py` | `MGSTestClient` class, button constants |
| Test runner | `port/test/testcase_runner.py` | Discovery, execution, `-k` filter, timeouts |
| Test cases | `port/testcases/test_*.py` | One file per subsystem or stage |
| Input logs | `port/testcase/*.log` | Recorded human input for deterministic replay |

### Available Inspection Commands

| Command | Returns | Cost |
|---------|---------|------|
| `get_state` | frame, stage, snake pos/hp, camera, alert, faces, objs_count | Cheap |
| `get_actors` | All active actors with name, priority, count | Cheap |
| `get_mesh(simplified=True)` | Bounding boxes, vertex/face counts per model | Medium |
| `get_mesh(simplified=False)` | Full vertex + face index data | Expensive (can exceed buffer) |
| `get_collision` | HZD walls, floors, triggers with coordinates | Medium |
| `screenshot(path)` | BMP framebuffer dump (320x224, 15-bit color) | Medium |
| `inject_input(buttons, frames)` | Override pad for N frames | Instant |
| `peek(addr, size)` / `poke(addr, data)` | Raw memory read/write | Instant |

### Available Input Logs

| Log file | Target stage | Duration | Content |
|----------|-------------|----------|---------|
| `1_rendering_left_side_s01a.log` | s01a (Heliport) | 1751 frames | Walk to left edge, combat area |
| `1_crouch_s01a.log` | s01a | ~400 frames | Crouch sequence |
| `1_crouch_s01a_2.log` | s01a | ~2000 frames | Extended crouch |
| `1_camera_crouching_s00a_2.log` | s00a (Loading Dock) | ~300 frames | Crouch in dock |
| `1_camera_leaning_crouching_s00a.log` | s00a | ~400 frames | Lean + crouch |
| `2_rendering_hangar_floor_s02b.log` | s02b (Tank Hangar 2) | ~500 frames | Hangar floor movement |
| `nav_d01a.log` | d01a (Heliport cutscene) | ~600 frames | Menu nav to cinema |
| `nav_s00a.log` | s00a/d00a (cinema) | ~550 frames | Menu nav to cinema |

### Button Constants (active-HIGH, match `port/mts/mts.c`)

```
L2=0x0001  R2=0x0002  L1=0x0004  R1=0x0008
TRIANGLE=0x0010  CIRCLE=0x0020  CROSS=0x0040  SQUARE=0x0080
SELECT=0x0100  START=0x0800
UP=0x1000  RIGHT=0x2000  DOWN=0x4000  LEFT=0x8000
```

---

## Test Categories

### 1. Core Engine (test_game_state.py)

Tests that the fundamental engine variables are sane after any stage loads.
These catch memory corruption, broken initialization, and game loop bugs.

| Test | What it verifies | How |
|------|-----------------|-----|
| Health valid after load | `0 < health <= max_health`, `max_health > 0` | `wait_for_stage` + `run(60)` + `get_state` |
| Frame counter advances | `GV_Time` increases with `run()` calls | Three `get_state` calls with `run(30)` between |
| No alert at start | `alert_mode == 0` after fresh stage load | `get_state` after `wait_for_stage` |
| Snake position non-zero after gameplay | Position not `(0,0,0)` after log replay | `run_log` to s01a, check `snake.pos` |
| Stage name set | `stage != "unknown"` after gameplay load | `run_log` to s01a, check `stage` field |
| load_complete persists | `load_complete == 1` stays set | Check after `wait_for_stage` + `run(30)` |

**When to add tests here:** Any time you discover a new global variable that the
port initializes incorrectly or that gets corrupted during stage transitions.

---

### 2. Actor System (test_actors.py)

Tests that the actor linked lists are not corrupted and contain expected actors.

| Test | What it verifies | How |
|------|-----------------|-----|
| Actors present after load | At least 3 active actors | `get_actors`, count where `active==1` |
| All actors have names | No `"???"` filename entries | Filter actors with missing name |
| Actor count sane | Total count < 500 (corruption guard) | `len(actors)` |
| Valid priority range | All priorities in `[0, 6]` | Filter actors outside range |

**When to add tests here:** After any change to `GV_NewActor`, `GV_DestroyActor`,
or the actor list iteration in `test_server.c`. Also useful after overlay loading
changes — corrupt overlays produce actors with garbage filenames.

---

### 3. Rendering — Per Stage

Rendering tests verify that the 3D pipeline produces output. The critical metric
is `faces_last_frame` — if it drops to zero, the entire rendering path is broken
(draw calls missing, culling bug, or pipeline failure).

#### 3a. s01a Heliport (test_rendering_s01a.py)

| Test | Threshold | Log used |
|------|-----------|----------|
| Faces visible left side | >= 100 | `1_rendering_left_side_s01a.log` |
| Faces visible during crouch | >= 80 | `1_crouch_s01a.log` |
| Faces visible extended crouch | >= 80 | `1_crouch_s01a_2.log` |
| Nonzero faces after load | > 0 | `1_crouch_s01a.log` |

#### 3b. s02b Hangar (test_rendering_s02b.py)

| Test | Threshold | Log used |
|------|-----------|----------|
| Faces in hangar | >= 100 | `2_rendering_hangar_floor_s02b.log` |
| Camera tracks Snake | cam_x within 1000 of snake_x | Same log |
| Render queue non-empty | objs_count > 0 and total_models > 0 | `get_mesh(simplified=True)` |

#### 3c. Cutscene stages (test_cutscenes.py)

| Test | What it verifies | Log used |
|------|-----------------|----------|
| d01a cinema actors present | strctrl.c, demothrd.c, jimctrl.c in actor list | `nav_d01a.log` |
| d01a faces drawn | >= 50 faces | Same |
| d01a objects in queue | objs_count > 0 | Same |
| d01a no crash after load | 200 extra frames without disconnect | Same |
| d01a load_complete persists | load_complete == 1 | Same |

**When to add tests here:** After adding a new stage to the port, record a log
file that navigates to it and add a rendering test. Minimum test: "faces > 0
and no crash after 200 frames."

**How to record a new log:**
```python
c = MGSTestClient(auto_input=False)
c.start()
c.wait_for_stage()
c.record_log("/path/to/new_stage.log")
# Use inject_input to navigate menus, then play for a while
c.stop_log()
c.stop()
```

---

### 4. Camera System (test_camera_*.py)

Camera bugs are the most visible class of port defect. The PSX camera uses
fixed-point GTE matrix math that is sensitive to 32-bit vs 64-bit differences.

#### 4a. s01a Camera (test_camera_s01a.py)

| Test | What it verifies | How |
|------|-----------------|-----|
| Camera tracks Snake left side | `abs(cam_x - snake_x) < 500` | `run_log` left-side log |
| Camera tracks during crouch | `abs(cam_x - snake_x) < 500` | `run_log` crouch log |
| Camera Y above floor | `cam_y > 0` (camera not underground) | `get_state` |
| Clip distance nonzero | `clip_dist > 0` | `get_state` |

#### 4b. s00a Camera (test_camera_s00a.py)

| Test | What it verifies | How |
|------|-----------------|-----|
| Camera tracks during crouch | `abs(cam_x - snake_x) < 800` | s00a crouch log |
| Camera tracks lean+crouch | Same check | s00a lean+crouch log |
| Faces visible during crouch | >= 50 | Face count check |

**When to add tests here:** After any change to `DG_SetPos`, `DG_LookAt`,
`DG_ScreenDistance`, or the camera actor (`source/game/camera.c`). Camera bugs
typically manifest as the camera X/Z drifting far from Snake's position.

---

### 5. Collision / HZD (test_collision_s01a.py)

HZD data defines walls Snake can't walk through, floors that set his Y position,
and trigger zones (doors, events). Corrupt HZD data causes walk-through-walls,
falling through floors, or missing event triggers.

| Test | What it verifies | How |
|------|-----------------|-----|
| HZD maps present | At least 1 map loaded | `get_collision`, check `maps` array |
| Walls and floors present | Both counts > 0 | Sum across all maps |
| Wall coords in range | All endpoints within `[-50000, 50000]` | Iterate wall segments |
| Floor Y values in range | Corner Y within `[-50000, 50000]` | Iterate floor corners |
| Bounds cover nonzero area | `width > 0 or depth > 0` for each map | Check map bounds |

**When to add tests here:** After changes to `libhzd/`, the HZD loader
(`port/libhzd/hzd_loader.c`), or `OFFSET_TO_PTR` fixups that affect HZD
pointer relocation. HZD corruption is silent until Snake walks into a wall
that isn't there.

---

### 6. Mesh Integrity (test_mesh_s01a.py)

Mesh tests verify that the 3D model data in the render queue is self-consistent.
Corrupt KMD loading, bad pointer fixups, or endianness bugs produce models with
zero vertices, inverted bounding boxes, or out-of-range face indices.

| Test | What it verifies | How |
|------|-----------------|-----|
| Objects in render queue | At least 1 object with models | `get_mesh(simplified=True)` |
| Vertex counts nonzero | Every model has `n_verts > 0` | Iterate models |
| Bounding boxes valid | `min <= max` for all axes | Check min/max per model |
| Face indices in range | All face indices `< n_verts` | `get_mesh(simplified=False)` with buffer-exceeded guard |
| World positions sane | All `world_t` within `[-200000, 200000]` | Check per object |

**When to add tests here:** After changes to `port/libdg/kmd_loader.c`,
`OFFSET_TO_PTR`, or `DG_OBJS`/`DG_MDL` struct layout. The face-index test is
particularly valuable — it catches off-by-one errors in vertex index packing
that cause GPU crashes on real hardware.

---

### 7. Weapons & Combat (test_weapons.py)

Weapon tests verify that the input→action pipeline works: pressing R1 fires
the weapon, L1 enters first-person mode, and weapon cycling doesn't crash.
Most weapons rely on complex actor interactions (bullet spawn, collision
detection, animation state machine) that are easy to break.

| Test | What it verifies | How |
|------|-----------------|-----|
| SOCOM fire no crash | R1 press doesn't crash, faces still rendered | `inject_input(R1, 3)` after s01a log |
| First-person mode | L1 hold keeps faces > 0 | `inject_input(L1, 30)` |
| First-person fire | L1+R1 doesn't crash | `inject_input(L1\|R1, 5)` |
| Weapon cycle (R2) | 5x R2 presses don't crash | Loop `inject_input(R2, 3)` |
| Item equip (SQUARE) | SQUARE hold doesn't crash | `inject_input(SQUARE, 30)` |
| First-person camera | Camera position changes in FP (or at least no crash) | Compare cam pos before/after L1 |

**When to add tests here:** After implementing a new weapon actor, fixing bullet
collision, or changing the input dispatch path in `port_update_pad`. Each new
weapon should get at minimum a "fire doesn't crash" test.

---

### 8. Items & Equipment (test_items.py)

Equipment tests verify that item cycling (L2), item equip (SQUARE hold), and
combined item+first-person mode work without crashing or corrupting state.

| Test | What it verifies | How |
|------|-----------------|-----|
| Items present in s01a | `item.c` actors exist | `get_actors` after s01a log |
| Equip menu opens | SQUARE hold doesn't crash | `inject_input(SQUARE, 45)` |
| Item cycle (L2) | 6x L2 presses don't crash | Loop `inject_input(L2, 3)` |
| Crouch stability | Faces >= 50 during crouch sequence | `run_log` crouch log |
| Health valid after item cycle | `0 < health <= max_health` | Check after L2 cycling |
| Item + first-person | L2 cycle then L1 hold doesn't crash | Sequential inject_input |

**When to add tests here:** After implementing a new equipment item (`source/equip/`),
or after changes to the menu system (`source/menu/item.c`).

---

## Stage Coverage Matrix

This table shows which stages have test coverage and what type. Adding a row
requires recording a log file and adding at least a rendering + collision test.

| Stage | Name | Rendering | Camera | Collision | Mesh | Combat | Cutscene |
|-------|------|-----------|--------|-----------|------|--------|----------|
| s00a | Loading Dock | - | Yes | - | - | - | - |
| s01a | Heliport | Yes | Yes | Yes | Yes | Yes | - |
| s02b | Tank Hangar 2 | Yes | Yes | - | - | - | - |
| d01a | Heliport Cutscene | - | - | - | - | - | Yes |

### Stages not yet tested (need log files)

| Stage | Name | Priority | Why |
|-------|------|----------|-----|
| s02a | Tank Hangar | High | Large interior, tests indoor rendering |
| s03a | Holding Cell | Medium | Small room, tests tight camera |
| s04a | Armory | Medium | Item-heavy stage |
| s04b | Armory South | High | Ocelot boss fight, complex actor interactions |
| d00a | Loading Dock Cutscene | Low | Similar to d01a, cinema actors |
| s05a | Canyon | Medium | Outdoor long-distance rendering |
| s07a | Underground Passage | Medium | Dark stage, tests lighting path |

**How to add a new stage:**

1. Record a navigation log using `test/record_stages.py` or manual `inject_input`
2. Place the `.log` file in `port/testcase/`
3. Create `port/testcases/test_<category>_<stage>.py`
4. Add minimum tests: faces > 0, no crash after 200 frames, collision data present

---

## Test Patterns

### Pattern: Stability test (smoke test for a new feature)

```python
def test_feature_does_not_crash():
    """Exercising <feature> must not crash the game."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("1_rendering_left_side_s01a.log"))

        # Exercise the feature
        c.inject_input(BTN_SOMETHING, frames=5)
        c.run(30)

        state = c.get_state()
        assert state["load_complete"] == 1, "Game crashed"
        assert state["faces_last_frame"] > 0, "Rendering stopped"
```

### Pattern: Rendering regression test

```python
def test_faces_visible_in_new_area():
    """At least N faces must be drawn in <area>."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("new_area.log"))

        faces = state["faces_last_frame"]
        print(f"    faces: {faces}")
        assert faces >= 80, f"Too few faces: {faces}"
```

### Pattern: Camera tracking test

```python
def test_camera_tracks_snake_in_area():
    """Camera must stay within threshold of Snake position."""
    THRESHOLD = 500
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log(_log("area_movement.log"))

        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        delta   = abs(cam_x - snake_x)

        print(f"    cam_x={cam_x} snake_x={snake_x} delta={delta}")
        assert delta < THRESHOLD, f"Camera too far: delta={delta}"
```

### Pattern: Collision data validation

```python
def test_collision_valid_in_stage():
    """HZD wall/floor data must be present and in coordinate range."""
    MAX_COORD = 50000
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("stage_nav.log"))

        resp = c.get_collision()
        maps = resp.get("maps", [])
        assert len(maps) > 0, "No HZD maps loaded"

        walls  = sum(len(m.get("walls", []))  for m in maps)
        floors = sum(len(m.get("floors", [])) for m in maps)
        assert walls > 0 and floors > 0, f"walls={walls} floors={floors}"
```

### Pattern: Input action test

```python
def test_action_with_button_combo():
    """Pressing <combo> in <context> must not crash."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        c.run_log(_log("setup_log.log"))

        # Pre-condition: ensure we're in the right state
        c.run(10)

        # Action under test
        c.inject_input(BTN_L1 | BTN_R1, frames=5)
        c.run(5)
        c.run(30)  # settle

        state = c.get_state()
        assert state["load_complete"] == 1, "Crash after button combo"
```

---

## Running the Test Suite

```bash
cd port

# Run all tests (headless, default 120s timeout per test)
python3 test/testcase_runner.py testcases/

# Run a single file
python3 test/testcase_runner.py testcases/test_rendering_s01a.py

# Run only tests matching a pattern
python3 test/testcase_runner.py testcases/ -k camera

# Show the game window while tests run (for visual debugging)
python3 test/testcase_runner.py --no-headless testcases/

# Shorter timeout for quick iteration
python3 test/testcase_runner.py --timeout 60 testcases/

# Combine flags
python3 test/testcase_runner.py --no-headless --timeout 30 -k socom testcases/
```

---

## Adding a Test — Checklist

1. Identify the subsystem and failure mode you're guarding against.
2. Find or record a `.log` file that reaches the relevant state.
3. Write the test function in the appropriate `test_*.py` file (or create a new one).
4. Follow the naming convention: `test_<what>_<where>` (e.g., `test_faces_visible_hangar`).
5. Use `print(f"    key={value}")` for diagnostic output before assertions.
6. Prefer stability assertions (`load_complete == 1`, `faces > 0`) over exact values.
7. Run the test in isolation: `python3 test/testcase_runner.py -k your_test_name testcases/`
8. Run the full suite to verify no interaction effects.

---

## Known Limitations

- **GM_StageName** is `"unknown"` during menu stages and cutscenes. Only gameplay
  stages set it via GCL scripts. Tests cannot distinguish which cutscene stage
  is loaded by name alone — use actor lists instead.

- **get_mesh(simplified=False)** can produce responses exceeding the 64 MB buffer
  limit for complex stages. Tests using full mesh data must catch the buffer-exceeded
  error and skip gracefully.

- **Cutscene navigation** requires cinema skip via `cancel.c`, which reads
  `mts_read_pad(1)`. The port maps player 1 buttons to channel 1 for this to work.
  If menu navigation changes, the `nav_*.log` files will need re-recording.

- **No save/load state.** Each test starts from a fresh game launch. There is no
  snapshot/restore mechanism. Tests that need a specific game state (e.g., "after
  getting FAMAS") must either replay a long input log or use `poke` to set
  inventory directly.

- **SIGALRM timeout** only works on Unix. The test runner's per-test hard timeout
  uses `signal.alarm` and will not function on Windows.
