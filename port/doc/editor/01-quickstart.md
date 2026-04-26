# Quickstart

## Prerequisites

- The main port must build cleanly first — the editor links against
  `port/obj/*.o` produced by `port/Makefile`. If you've never built the
  port:

  ```
  cd port
  make            # produces port/mgs and port/obj/*.o
  ```

- A Metal Gear Solid Integral disc image (`*.iso` / `*.bin` / `*.cue`)
  reachable on disk. The editor reads stage data straight from the
  image — it does **not** modify the original file.

- Python 3.10+ with `pillow` (only required for the stage authoring
  pipeline, not for running the editor itself):

  ```
  pip3 install pillow
  ```

## Build the editor

```
cd port/editor
make
```

`port/editor/Makefile` first runs `make -C ..` to ensure the port is
built, then compiles the editor's own `main.c` / `ed_*.c` / `ed_ui.cpp`
and links them against the port's object files (excluding `main.o`,
`main_game.o`, `imgui_debug.o`, and `test_server.o` — those are
replaced by the editor's own translation units).

The output is a single binary: `port/editor/editor` (~4 MB).

## Run

```
cd port/editor
./editor --iso path/to/MGS.bin
```

CLI flags:

| Flag                    | Meaning                                                        |
| ----------------------- | -------------------------------------------------------------- |
| `--iso <path>`          | Disc image to mount. Also accepts a positional `*.bin/*.iso/*.cue`. |
| `--stage <name>`        | Stage to load at startup (default `s01a`).                     |
| `PORT_ISO=...` env var  | Same as `--iso`.                                               |
| `PORT_DATA_DIR=...` env | Use an extracted disc directory instead of an image.           |
| `PORT_GL_SCALE=N`       | Internal render-resolution multiplier (default 4 → 1280×896). |

## First-run walk-through

You should see a 1280×720 window split into a 3D viewport and a
floating "MGS Stage Editor" panel:

```
┌─────────────┬───────────────────────────────────────┐
│ ▼ Editor    │                                       │
│ s01a 60 fps │            3D viewport                │
│ cursor: …   │            (level + HZD)              │
│ Scene▾      │                                       │
│  filter     │                                       │
│  s01a    ▾  │                                       │
│  Reload     │                                       │
│  axes       │                                       │
│ Camera      │                                       │
│ Actors      │                                       │
│ HZD         │                                       │
└─────────────┴───────────────────────────────────────┘
```

Default camera is top-down at world `(0, 20000, 0)`, looking down with
pitch ≈ −1.5 rad. You should see the s01a heliport from above, with the
HZD floor wireframe overlay and one cube marker per actor placed by the
GCL.

Try:

1. **Switch stages** — Scene tab → click the `s01a` combo → pick `s02a`
   (Tank Hangar). The picker auto-discovers any stage in
   `port/editor/extra_stages/<name>/` too, so a custom `s99a` shows up
   alongside the disc stages.

2. **Click an actor** — left-click any cube marker in the viewport, or a
   row in the Actors tab. The camera focuses on it and a floating
   *Selected* card appears top-right with the actor's full data
   (editable position + rotation).

3. **Move with WASD** — `W`/`A`/`S`/`D` move along the camera's local
   axes. `Q`/`E` move along world `-Y` / `+Y`. Right-mouse-drag looks.

4. **Press `G`** — opens a small Goto modal; type any world XYZ and
   the camera warps there.

5. **Press `F1`** — full keybinding reference.

If the window is black: confirm your `--iso` is correct (the editor
prints the resolved path on startup) and that the GL backend
initialised (look for `[gl] context ready ...` in stdout).

## Authoring a custom stage

```
python3 tools/import_stage.py \
    --name s99a \
    --input    port/editor/assets/s99a/s99a.obj \
    --texture  port/editor/assets/s99a/s99a.png \
    --collision port/editor/assets/s99a/s99a_collision.obj
```

Outputs `port/editor/extra_stages/s99a/datacnf.bin`. Re-launch the
editor — `s99a` appears in the picker automatically. See
[05-stage-authoring.md](05-stage-authoring.md) for the full workflow.
