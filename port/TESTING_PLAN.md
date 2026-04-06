# MGS Port: Test Harness & Autonomous Debug Interface

## Context

Rendering and camera bugs (like the s01a left-side camera drift) are currently debugged by
adding printf statements, rebuilding, replaying a log, and reading output manually. There is
no way to programmatically inspect game internals, verify expected state, or create targeted
test cases without modifying source and rebuilding each time.

The goal is a **socket-based test harness** that lets Claude (or any external client) drive
the game headlessly, inspect all internals at any frame, inject input, and assert on state —
enabling autonomous test creation and debugging.

---


## Separation Rule

- The testing harness must be kept as separated from the main game code as possible.
- All functions added for the test harness in the codebase must be prefixed with `TEST_HARNESS_` (e.g., `TEST_HARNESS_init`, `TEST_HARNESS_tick`).

## Architecture

```
Claude (Bash tool)
    └─ python port/test/mgs_client.py  (or a test script)
         └─ Unix socket /tmp/mgs_test.sock
              └─ mgs binary (SDL_VIDEODRIVER=dummy)
                   ├─ TEST_HARNESS_tick()  ← called once per frame in main loop
                   └─ all game globals (camera, actors, mesh, HZD, VRAM)
```

The game is **single-threaded** — `TEST_HARNESS_tick()` runs synchronously between frames with
non-blocking I/O. No threading needed.

---

## Socket Protocol

Newline-delimited JSON over Unix domain socket `/tmp/mgs_test.sock`.

### Commands and Responses

```
{"cmd":"status"}
→ {"ok":true,"frame":1234,"load_complete":1,"gv_time":5678}

{"cmd":"run","frames":100}
→ {"ok":true,"frame":1334}

{"cmd":"step"}
→ {"ok":true,"frame":1235}

{"cmd":"step_for_frames","frames":10}
→ {"ok":true,"frame":1244} // blocks until 10 frames have been processed

{"cmd":"pause"}
→ {"ok":true}
  // game blocks in test_server_tick until step or run

{"cmd":"inject_input","buttons":32,"frames":1}
→ {"ok":true}
  // overrides pad for next N frames

{"cmd":"replay_log","path":"testcase/foo.log"}
→ {"ok":true, "status":"replaying"}
  // replays input log from start; game runs until log end, then pauses

{"cmd":"record_log","path":"testcase/bar.log"}
→ {"ok":true, "status":"recording"}
  // records all input frames until "stop_log"

{"cmd":"stop_log"}

{"cmd":"get_state"}
→ {"ok":true,
   "frame":1234,
   "stage":"s01a",
   "load_complete":1,
   "replay_status": { "status":"idle"|"replaying"|"recording", "path":"/path/to/log" or null, "processed_inputs":N, "total_inputs": N},
   "camera":{"pos":[x,y,z],"eye_inv":[[...],[...],[...]]},
   "snake":{"pos":[x,y,z],"health":100,"max_health":100},
   "game_status":0,"alert_mode":0,
   "objs_count":15,"faces_last_frame":200}

{"cmd":"get_actors"}
→ {"ok":true,"actors":[
     {"name":"sna_init.c","priority":2,"count":100,"runtime":500},
     ...]}

{"cmd":"get_mesh"}
→ {"ok":true,"objects":[
     {"world_t":[x,y,z],"models":[
       {"n_verts":N,"vertices":[[vx,vy,vz],...],"n_faces":N,"faces":[[i0,i1,i2,i3],...]}
     ]},...]}
  // add "simplified":true for bounding-box-only mode (avoids 5-20 MB responses)

{"cmd":"get_collision"}
→ {"ok":true,"active_group":3,
   "walls":[{"p1":[x,z],"p2":[x,z]},...],
   "floors":[{"corners":[[x,y,z],...]},...],
   "triggers":[{"name":"duct01","b1":[x,z],"b2":[x,z]},...]}

{"cmd":"screenshot","path":"/tmp/frame.bmp"}
→ {"ok":true,"path":"/tmp/frame.bmp"}

{"cmd":"quit"}
→ {"ok":true}


```

---

## Files to Create / Modify

### New: `port/test_server.h`

```c
void TEST_HARNESS_init(void);   // Open socket, set non-blocking
void TEST_HARNESS_tick(void);   // Poll socket, process commands; blocks if paused
```

### New: `port/test_server.c` (~400 lines)

**Sections:**

1. **Socket init**: `socket(AF_UNIX)`, `unlink` stale socket, `bind("/tmp/mgs_test.sock")`,
   `listen()`, `fcntl(O_NONBLOCK)`

2. **Frame tick** (`test_server_tick`):
   - Non-blocking `accept()` + `recv()` — accumulate line into buffer
   - Parse JSON command with `strstr`/`sscanf` (no library needed for simple
     `{"cmd":"X","key":N}` format)
   - Dispatch to handler, write JSON response with `snprintf` into a 4 MB output buffer
     (needed for large mesh/collision payloads)
   - If `paused` flag set: loop with `poll(fd, 1, 16)` until step/run received

3. **Run-N-frames logic**: `TEST_HARNESS_tick()` returns immediately when `frames_to_run > 0`;
   main loop decrements; when 0 re-enters blocking wait

4. **Command handlers** (read game globals directly):
   - `cmd_status()` — GV_Time, GM_LoadComplete
   - `cmd_run(n)` / `cmd_step()` / `cmd_pause()` / `cmd_resume()`
   - `cmd_inject_input(buttons, frames)` — writes to `test_override_buttons` / `test_override_frames`
   - `cmd_replay_log(path)` — calls `port_replay_start(path)` in mts.c
   - `cmd_get_state()` — camera eye_inv + translation, GM_PlayerPosition, GM_GameStatus/AlertMode,
     objs_count, port_last_drawn_faces
   - `cmd_get_actors()` — iterate `gActorsList_800ACC18[7]`, each linked list via `->next`
   - `cmd_get_mesh()` — iterate `DG_Chanls[1].queue[0..objs_index-1]`, per DG_OBJS iterate
     DG_MDL→vertices/vindices; `simplified` flag returns only bounds + counts
   - `cmd_get_collision()` — iterate `gMapRecs_800B7910[0..gMapCount_800ABAA8-1]`, each
     `map->hzd->header->groups[i]` → walls (HZD_SEG), floors (HZD_FLR), triggers (HZD_TRG)
   - `cmd_screenshot(path)` — write `vram[0..223][0..319]` as BMP (manual 40-byte header +
     320×224×3 bytes RGB converted from PSX 15-bit BGR1555; no external library)

**Key external globals:**
```c
extern int GV_Clock, GV_Time, GM_LoadComplete;
extern int GM_GameStatus, GM_AlertMode, GM_AlertLevel;
extern SVECTOR GM_PlayerPosition;
extern int GM_SnakeCurrentHealth, GM_SnakeMaxHealth;
extern DG_CHANL DG_Chanls[3];
extern ActorList gActorsList_800ACC18[7];
extern uint16_t vram[512][1024];
extern MAP gMapRecs_800B7910[16];
extern int gMapCount_800ABAA8;
```

**Two new globals** (defined in test_server.c, declared extern in mts.c):
```c
int test_override_buttons = -1;
int test_override_frames  = 0;
```

### Modified: `port/main.c`

```c
#include "test_server.h"

// After port_vram_init(g_renderer):
TEST_HARNESS_init();

// After port_render(), before frame limiter:
TEST_HARNESS_tick();
```

### Modified: `port/mts/mts.c` — `port_update_pad()`

```c
extern int TEST_HARNESS_override_buttons;
extern int TEST_HARNESS_override_frames;
if (TEST_HARNESS_override_frames > 0) {
    b = (unsigned short)TEST_HARNESS_override_buttons;  // active-HIGH, same as log format
    TEST_HARNESS_override_frames--;
}
// b is stored in port_pad_buttons; mts_get_pad() inverts it for PSX active-LOW
```

Also expose `port_replay_start(const char *path)` to allow mid-session replay switching
(currently only supported at startup via `MGS_INPUT_REPLAY` env var).

### Modified: `port/libdg/libdg_stub.c`

```c
int port_last_drawn_faces = 0;  // global, updated each frame by port_RenderObjects()
```

Replace local `drawn_faces` with this global so `get_state` can report it.

### Modified: `port/Makefile`

Add `$(OBJDIR)/test_server.o` to `PORT_OBJ`.

---

## Python Client: `port/test/mgs_client.py`

```python
import subprocess, socket, json, os, time

class MGSTestClient:
    def __init__(self, binary=None, socket_path="/tmp/mgs_test.sock"):
        self.binary = binary or os.path.join(os.path.dirname(__file__), "../mgs")
        self.socket_path = socket_path
        self.proc = None
        self.sock = None
        self._buf = b""

    def start(self, env_extras=None):
        env = {**os.environ, "SDL_VIDEODRIVER": "dummy",
         "MGS_AUTO_INPUT": "1"}
        if env_extras:
            env.update(env_extras)
        self.proc = subprocess.Popen(
            [self.binary], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self._wait_connect()

    def stop(self):
        try: self.send("quit")
        except: pass
        if self.sock: self.sock.close()
        if self.proc: self.proc.wait(timeout=5)

    def send(self, cmd, **kwargs):
        msg = json.dumps({"cmd": cmd, **kwargs}) + "\n"
        self.sock.sendall(msg.encode())
        return json.loads(self._recv_line())

    def run(self, frames=1):         return self.send("run", frames=frames)
    def step(self):                  return self.send("step")
    def pause(self):                 return self.send("pause")
    def get_state(self):             return self.send("get_state")
    def get_actors(self):            return self.send("get_actors")
    def get_mesh(self, simplified=False): return self.send("get_mesh", simplified=simplified)
    def get_collision(self):         return self.send("get_collision")
    def screenshot(self, path):      return self.send("screenshot", path=path)
    def inject_input(self, buttons, frames=1):
        return self.send("inject_input", buttons=buttons, frames=frames)
    def replay_log(self, path):      return self.send("replay_log", path=path)

    def wait_for_stage(self, timeout=3000):
        for _ in range(timeout):
            r = self.run(1)
            if r.get("load_complete"):
                return True
        raise TimeoutError("Stage never loaded")

    def run_log(self, log_path):
        """Replay a full input log and return final state."""
        self.replay_log(log_path)
        last_frame = self._log_last_frame(log_path)
        self.run(last_frame + 30)
        return self.get_state()

    def __enter__(self): self.start(); return self
    def __exit__(self, *_): self.stop()

    def _wait_connect(self, timeout=10):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(self.socket_path)
                self.sock = s
                return
            except:
                time.sleep(0.1)
        raise TimeoutError("Game did not start — socket not ready")

    def _recv_line(self):
        while b"\n" not in self._buf:
            data = self.sock.recv(65536)
            if not data:
                raise ConnectionError("Game disconnected")
            self._buf += data
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode()

    @staticmethod
    def _log_last_frame(path):
        last = 0
        with open(path) as f:
            for line in f:
                parts = line.split()
                if parts:
                    last = int(parts[0])
        return last
```

---

## Test Runner: `port/test/testcase_runner.py`

Discovers `test_*.py` files, runs all `def test_*()` functions, reports pass/fail with
captured stdout. Usage: `python testcase_runner.py [pattern]`

---

## Example Test: `port/test/test_camera_s01a.py`

```python
from mgs_client import MGSTestClient

def test_camera_tracks_snake_on_left_side():
    """Camera X should stay within 500 units of snake X."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log("../testcase/1_rendering_left_side_s01a.log")
        cam_x   = state["camera"]["pos"][0]
        snake_x = state["snake"]["pos"][0]
        assert abs(cam_x - snake_x) < 500, f"cam={cam_x} snake={snake_x}"

def test_faces_visible_on_left_side():
    """At least 100 faces should be drawn when snake is at left edge of helipad."""
    with MGSTestClient() as c:
        c.wait_for_stage()
        state = c.run_log("../testcase/1_rendering_left_side_s01a.log")
        assert state["faces_last_frame"] >= 100, \
            f"Only {state['faces_last_frame']} faces drawn"
```

---


## Low-Level Memory Access (Surgical Debugging)

### Command

| Command | Parameters                | Description                                 |
|---------|---------------------------|---------------------------------------------|
| peek    | addr (hex), size (int)    | Returns raw hex string of memory at address. |
| poke    | addr (hex), data (hex)    | Writes raw bytes to memory at address.       |

**Security note:** peek/poke operate on raw process memory with no sandboxing.
This is safe over a Unix domain socket (`/tmp/mgs_test.sock`, local-only).
Never expose this server over a network socket.

#### Peek/Poke Examples

```
{"cmd":"peek", "addr":"0x800B7910", "size":16}
→ {"ok":true, "data":"AABBCCDDEEFF00112233445566778899"}

{"cmd":"poke", "addr":"0x800B7000", "data":"0000"}
→ {"ok":true}
```

## Implementation Order

1. `port/test_server.h` — header
2. `port/test_server.c` — socket init + status + run/step/pause + get_state (core)
3. `port/main.c` — wire in `TEST_HARNESS_init()` and `TEST_HARNESS_tick()`
4. `port/libdg/libdg_stub.c` — expose `port_last_drawn_faces`
5. `port/Makefile` — add `test_server.o`
6. Build & smoke-test with `nc -U /tmp/mgs_test.sock`
7. `port/mts/mts.c` — input override hook + `port_replay_start()`
8. `port/test_server.c` — add get_actors, get_mesh, get_collision, screenshot
9. `port/test/mgs_client.py` — Python client library
10. `port/test/testcase_runner.py` + `port/test/test_camera_s01a.py`

---

## Verification

```bash
# Build
cd port && make

# Run headless in background
SDL_VIDEODRIVER=dummy MGS_AUTO_INPUT=1 ./mgs &

# Manual smoke test
echo '{"cmd":"status"}'                              | nc -U /tmp/mgs_test.sock
echo '{"cmd":"run","frames":1000}'                   | nc -U /tmp/mgs_test.sock
echo '{"cmd":"get_state"}'                           | nc -U /tmp/mgs_test.sock
echo '{"cmd":"screenshot","path":"/tmp/frame.bmp"}' | nc -U /tmp/mgs_test.sock

# Full test suite
cd port/test && python testcase_runner.py
```

### Autonomous debug session (pattern Claude would use):

```python
with MGSTestClient() as c:
    c.wait_for_stage()
    c.replay_log("../testcase/1_rendering_left_side_s01a.log")
    for target_frame in [200, 400, 600, 800, 1000, 1400, 1751]:
        c.run(target_frame)
        state = c.get_state()
        c.screenshot(f"/tmp/frame_{target_frame}.bmp")
        # Inspect camera vs snake offset, face counts, mesh coverage
        cam   = state["camera"]["pos"]
        snake = state["snake"]["pos"]
        print(f"frame={target_frame} cam_x={cam[0]} snake_x={snake[0]} "
              f"faces={state['faces_last_frame']}")
```

---

## Notes

- **No threading needed** — `TEST_HARNESS_tick()` uses non-blocking I/O; blocking only when
  paused in headless mode (SDL_VIDEODRIVER=dummy)
- **Headless operation** — software renderer fallback already in main.c; dummy SDL video works
- **get_mesh size** — full s01a stage is ~100K–500K verts, potentially 5–20 MB JSON; use
  `simplified:true` for quick inspection, full mesh on demand
- **HZD coordinates** — signed shorts in PSX units (same as SVECTOR, ~1 unit ≈ 1 cm)
- **Screenshot format** — BMP (no external library); Python PIL can convert to PNG if needed
- **replay_log mid-session** — needs `port_replay_start(path)` refactor in mts.c since
  `MGS_INPUT_REPLAY` is currently read only at startup
- **Socket cleanup** — `TEST_HARNESS_init()` calls `unlink("/tmp/mgs_test.sock")` first to
  handle stale sockets from crashed runs
- **One client at a time** — server accepts one connection; reconnect after `quit`
