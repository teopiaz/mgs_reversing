# Test Harness JSON Protocol Reference

Socket: Unix domain stream socket at `/tmp/mgs_test.sock`

Transport: newline-delimited JSON. Each command is a single JSON object
terminated by `\n`. Each response is a single JSON object terminated by `\n`.
One command produces exactly one response. The server accepts one client at a
time.

Every response contains `"ok": true` on success or `"ok": false, "error": "..."` on failure.

---

## Connection Lifecycle

```
Client                              Server (game process)
  │                                    │
  │  connect(/tmp/mgs_test.sock)       │  TEST_HARNESS_init() binds + listens
  │ ──────────────────────────────────>│
  │                                    │  accept(), game starts paused
  │  {"cmd":"status"}\n                │
  │ ──────────────────────────────────>│
  │                                    │
  │  {"ok":true,"frame":0,...}\n       │
  │ <──────────────────────────────────│
  │                                    │
  │  {"cmd":"run","frames":100}\n      │  game runs 100 frames...
  │ ──────────────────────────────────>│
  │           (blocks)                 │  ...then responds:
  │  {"ok":true,"frame":100,...}\n     │
  │ <──────────────────────────────────│
  │                                    │
  │  {"cmd":"quit"}\n                  │
  │ ──────────────────────────────────>│  game exits
  │  {"ok":true}\n                     │
  │ <──────────────────────────────────│
```

After `run` or `step`, the game executes N frames before sending the response.
The socket blocks during this time. All other commands respond immediately.

---

## Commands

### Flow Control

#### status

Return current engine state without advancing any frames.

```
→ {"cmd": "status"}
← {"ok": true, "frame": 1234, "load_complete": 1, "gv_time": 1234}
```

| Field | Type | Description |
|-------|------|-------------|
| frame | int | Current GV_Time (global frame counter) |
| load_complete | int | 1 if the current stage has finished loading, 0 or -1 otherwise |
| gv_time | int | Same as frame (kept for backwards compatibility) |

---

#### run

Advance the game by N frames, then respond. The socket blocks until all frames
have been processed.

```
→ {"cmd": "run", "frames": 100}
← {"ok": true, "frame": 1334, "load_complete": 1}
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| frames | int | 1 | Number of frames to advance |

| Response field | Type | Description |
|----------------|------|-------------|
| frame | int | GV_Time after running |
| load_complete | int | GM_LoadComplete after running |

---

#### step

Advance exactly one frame. Equivalent to `run` with `frames: 1`.

```
→ {"cmd": "step"}
← {"ok": true, "frame": 1235, "load_complete": 1}
```

---

#### step_for_frames

Alias for `run`. Identical behavior.

```
→ {"cmd": "step_for_frames", "frames": 10}
← {"ok": true, "frame": 1244, "load_complete": 1}
```

---

#### pause

Enter paused mode. The game blocks inside `TEST_HARNESS_tick()` and will not
advance frames until a `step`, `run`, or `resume` command is received.
Responds immediately.

```
→ {"cmd": "pause"}
← {"ok": true}
```

---

#### resume

Exit paused mode. The game resumes its normal frame loop. Unlike `run`, this
does not wait for any frames — it returns immediately and the game continues
running freely (not driven by the test client).

```
→ {"cmd": "resume"}
← {"ok": true}
```

---

#### quit

Gracefully shut down the game process. Closes the client socket and sets
`g_running = 0`.

```
→ {"cmd": "quit"}
← {"ok": true}
```

---

### Input Control

#### inject_input

Override the controller pad buttons for the next N frames. Button values are
active-HIGH bitmasks matching the `BTN_*` constants in `port/mts/mts.c`.

After N frames, the override expires and normal input resumes (keyboard,
gamepad, or replay).

```
→ {"cmd": "inject_input", "buttons": 32, "frames": 5}
← {"ok": true}
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| buttons | int | 0 | Button bitmask (active-HIGH) |
| frames | int | 1 | Number of frames to hold the buttons |

**Button bitmask values:**

| Bit | Hex | Button |
|-----|-----|--------|
| 0 | 0x0001 | L2 |
| 1 | 0x0002 | R2 |
| 2 | 0x0004 | L1 |
| 3 | 0x0008 | R1 |
| 4 | 0x0010 | Triangle |
| 5 | 0x0020 | Circle |
| 6 | 0x0040 | Cross |
| 7 | 0x0080 | Square |
| 8 | 0x0100 | Select |
| 11 | 0x0800 | Start |
| 12 | 0x1000 | D-pad Up |
| 13 | 0x2000 | D-pad Right |
| 14 | 0x4000 | D-pad Down |
| 15 | 0x8000 | D-pad Left |

Buttons can be combined: `0x000C` = L1 + R1.

---

#### replay_log

Start replaying a `.log` file. The log contains frame-number / button-state
pairs. The internal frame counter resets to 0 so that log frame numbers align.
If a recording is in progress, it is stopped first.

```
→ {"cmd": "replay_log", "path": "/absolute/path/to/input.log"}
← {"ok": true, "status": "replaying"}
```

| Parameter | Type | Description |
|-----------|------|-------------|
| path | string | Absolute path to the `.log` file |

**Log file format** (space-separated, one entry per line):
```
0 0x0000
28 0x0020
32 0x0000
```
Column 1: frame number. Column 2: button bitmask (hex, active-HIGH). Entries
are sparse — only button-state *changes* are recorded.

---

#### record_log

Start recording controller input to a `.log` file. If a replay is in progress,
it is stopped first. Only button-state changes are written.

```
→ {"cmd": "record_log", "path": "/absolute/path/to/output.log"}
← {"ok": true, "status": "recording"}
```

---

#### stop_log

Stop the current recording. No effect if not recording.

```
→ {"cmd": "stop_log"}
← {"ok": true}
```

---

### Inspection

#### get_state

Return a comprehensive snapshot of the current game state. This is the primary
inspection command — cheap to call every frame.

```
→ {"cmd": "get_state"}
← {
    "ok": true,
    "frame": 1234,
    "stage": "- Heliport -",
    "load_complete": 1,
    "game_status": 0,
    "alert_mode": 0,
    "alert_level": 0,
    "snake": {
      "pos": [4549, 90, 6469],
      "health": 256,
      "max_health": 256
    },
    "camera": {
      "pos": [4049, 7171, 10414],
      "eye_inv": [[4096, 0, 0], [0, -2275, 3405], [0, -3405, -2275]],
      "eye_inv_t": [-4049, -4675, 11745],
      "clip_dist": 320
    },
    "objs_count": 25,
    "faces_last_frame": 195,
    "replay_status": {
      "status": "idle",
      "path": null,
      "processed_inputs": 0,
      "total_inputs": 0
    }
  }
```

| Field | Type | Description |
|-------|------|-------------|
| frame | int | GV_Time |
| stage | string | GM_StageName. `"unknown"` during menus/cutscenes. Gameplay stages set names like `"- Heliport -"` via GCL scripts. |
| load_complete | int | 1 = stage loaded, 0 = loading, -1 = transitioning |
| game_status | int | GM_GameStatus bitfield (0 = normal gameplay) |
| alert_mode | int | 0 = no alert, 1 = caution, 2 = alert, 3 = evasion |
| alert_level | int | 0–255, alert intensity |
| snake.pos | [int, int, int] | Snake world position [X, Y, Z] in PSX units (~1 unit = 1 cm) |
| snake.health | int | Current health (linkvarbuf[11]) |
| snake.max_health | int | Max health (linkvarbuf[12]) |
| camera.pos | [int, int, int] | Camera world position from `DG_Chanls[1].eye.t` |
| camera.eye_inv | [[3],[3],[3]] | 3x3 view matrix (rotation part of eye_inv) |
| camera.eye_inv_t | [int, int, int] | Translation part of eye_inv matrix |
| camera.clip_dist | int | Near clip distance |
| objs_count | int | Number of objects in the render queue |
| faces_last_frame | int | Number of triangles drawn last frame |
| replay_status.status | string | `"idle"`, `"replaying"`, or `"recording"` |
| replay_status.path | string or null | Path to the active log file |
| replay_status.processed_inputs | int | Last frame number processed in the log |
| replay_status.total_inputs | int | Total entries in the log file |

---

#### get_actors

Return all active actors across all 7 priority levels.

```
→ {"cmd": "get_actors"}
← {
    "ok": true,
    "actors": [
      {
        "priority": 0,
        "level": "DAEMON",
        "name": "gvd.c",
        "active": 1,
        "count": 1500,
        "runtime": 420
      },
      {
        "priority": 3,
        "level": "LEVEL3",
        "name": "sna_init.c",
        "active": 1,
        "count": 800,
        "runtime": 12000
      }
    ]
  }
```

| Actor field | Type | Description |
|-------------|------|-------------|
| priority | int | Actor list index (0–6) |
| level | string | Human-readable level name: DAEMON, MANAGER, LEVEL2, LEVEL3, LEVEL4, LEVEL5, DAEMON2 |
| name | string | Source filename that created the actor (e.g. `"sna_init.c"`) |
| active | int | 1 if the actor's `act` function pointer is set |
| count | int | Number of times the actor has been ticked |
| runtime | int | Cumulative CPU time spent in this actor |

---

#### get_mesh

Return the 3D render queue contents.

```
→ {"cmd": "get_mesh", "simplified": 1}
← {
    "ok": true,
    "simplified": 1,
    "objects": [
      {
        "world_t": [1000, 0, 2000],
        "flag": "0x15",
        "group_id": 3,
        "n_models": 2,
        "models": [
          {
            "n_verts": 24,
            "n_faces": 12,
            "min": [-100, -50, -100],
            "max": [100, 200, 100],
            "obj_world_t": [1000, 0, 2000]
          }
        ]
      }
    ]
  }
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| simplified | int | 0 | If 1, return only bounding boxes and counts. If 0, include full vertex positions and face index arrays. |

**Simplified mode** (recommended for most tests): each model includes `n_verts`,
`n_faces`, `min`, `max`, `obj_world_t`. No vertex or face arrays.

**Full mode** (simplified=0): additionally includes:

| Model field | Type | Description |
|-------------|------|-------------|
| vertices | [[int,int,int], ...] | Vertex positions (only if n_verts <= 4096) |
| faces | [[int,int,int,int], ...] | Per-face vertex indices unpacked from vindices (only if n_faces <= 8192). Each face has 4 indices (7-bit each, packed into a 32-bit word). |

Full mode responses can be very large (megabytes). If the response exceeds the
64 MB buffer limit, an error is returned instead:
```
← {"ok": false, "error": "mesh data exceeded buffer limit"}
```

---

#### get_collision

Return HZD (hazard/collision) data for all loaded maps.

```
→ {"cmd": "get_collision"}
← {
    "ok": true,
    "maps": [
      {
        "n_groups": 4,
        "bounds": [[-5000, -5000], [5000, 5000]],
        "walls": [
          {"p1": [100, 200], "p2": [300, 200]}
        ],
        "floors": [
          {"corners": [[0,0,0], [100,0,0], [100,0,100], [0,0,100]]}
        ],
        "triggers": [
          {"name": "duct01", "b1": [50, 50], "b2": [150, 150]}
        ]
      }
    ]
  }
```

| Map field | Type | Description |
|-----------|------|-------------|
| n_groups | int | Number of HZD groups in this map |
| bounds | [[int,int],[int,int]] | Map bounding box [[min_x, min_z], [max_x, max_z]] |

| Wall field | Type | Description |
|------------|------|-------------|
| p1 | [int, int] | Segment start [X, Z] |
| p2 | [int, int] | Segment end [X, Z] |

| Floor field | Type | Description |
|-------------|------|-------------|
| corners | [[int,int,int], ...] | Four corners [X, Y, Z] defining the floor quad |

| Trigger field | Type | Description |
|---------------|------|-------------|
| name | string | Trigger identifier (sanitized to printable ASCII) |
| b1 | [int, int] | Bounding box min [X, Z] |
| b2 | [int, int] | Bounding box max [X, Z] |

All coordinates are in PSX units (signed 16-bit range, ~1 unit = 1 cm).

---

#### screenshot

Write the current VRAM framebuffer to a BMP file. The image is 320x224 pixels,
24-bit RGB, converted from the PSX 15-bit BGR1555 format.

```
→ {"cmd": "screenshot", "path": "/tmp/frame.bmp"}
← {"ok": true, "path": "/tmp/frame.bmp"}
```

| Parameter | Type | Description |
|-----------|------|-------------|
| path | string | Absolute path for the output BMP file |

The BMP uses a top-down row order (negative height in the DIB header).
To convert to PNG on macOS: `sips -s format png frame.bmp --out frame.png`

---

### Memory Access

**Warning:** These commands read and write raw process memory with no
sandboxing. Only safe over a local Unix domain socket. An invalid address
will crash the game process (SIGSEGV).

#### peek

Read raw bytes from a process memory address.

```
→ {"cmd": "peek", "addr": "0x800B7910", "size": 16}
← {"ok": true, "addr": "0x800B7910", "size": 16, "data": "AABBCCDDEEFF00112233445566778899"}
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| addr | string | (required) | Hex address (with or without `0x` prefix) |
| size | int | 16 | Number of bytes to read (1–4096) |

| Response field | Type | Description |
|----------------|------|-------------|
| addr | string | Echo of the parsed address |
| size | int | Echo of the size |
| data | string | Uppercase hex string, 2 chars per byte, no separators |

---

#### poke

Write raw bytes to a process memory address.

```
→ {"cmd": "poke", "addr": "0x800B7000", "data": "0000FFFF"}
← {"ok": true, "bytes_written": 4}
```

| Parameter | Type | Description |
|-----------|------|-------------|
| addr | string | Hex address |
| data | string | Hex byte string (2 chars per byte, no separators, max 4096 bytes) |

| Response field | Type | Description |
|----------------|------|-------------|
| bytes_written | int | Number of bytes actually written |

---

## Error Handling

All errors return `"ok": false` with an `"error"` string:

```json
{"ok": false, "error": "unknown command: foo"}
{"ok": false, "error": "no path"}
{"ok": false, "error": "cannot open file"}
{"ok": false, "error": "size out of range (1-4096)"}
{"ok": false, "error": "missing addr or data"}
{"ok": false, "error": "mesh data exceeded buffer limit"}
{"ok": false, "error": "collision data exceeded buffer limit"}
```

If the game process crashes (SIGSEGV, abort, etc.), the socket closes and the
client receives EOF (empty `recv`). The Python client raises `ConnectionError("Game disconnected")`.

---

## Implementation Details

- **Server code:** `port/test_server.c` (~860 lines)
- **Public symbols:** `TEST_HARNESS_init()`, `TEST_HARNESS_tick()`
- **Called from:** `port/main.c` — `TEST_HARNESS_init()` after VRAM init, `TEST_HARNESS_tick()` after `port_render()` each frame
- **JSON parsing:** `strstr`/`sscanf`-based (no external library). Handles `"key": value` patterns.
- **Output buffer:** Starts at 8 MB, grows dynamically via `realloc` up to 64 MB hard cap. Overflow produces a JSON error response.
- **Input buffer:** 4096 bytes (sufficient for all commands except very large `poke` payloads).
- **Blocking model:** `run`/`step` commands return to the main loop and let the game advance frames. `TEST_HARNESS_tick()` decrements `frames_to_run` each frame and sends the response when it reaches zero. All other commands respond within the same `tick()` call.
- **Paused mode:** When paused (after `run` completes, or explicit `pause`), `TEST_HARNESS_tick()` loops with `poll(fd, 1, 50ms)` until a new command arrives.
