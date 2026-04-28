# MGS Cutscene System ("demo")

What MGS calls a "demo" is what the rest of the games industry calls a
cutscene. Each playable stage has up to two GCL bytecode scripts in its
DATACNF — `scenerio.gcx` (gameplay scene) and **`demo.gcx`** (cutscene).
Both share the same bytecode format, the same actor-spawn directives,
and the same runtime — the only difference is which one the engine runs
when entering a stage.

This documentation covers the cutscene side end-to-end: where the data
lives on disc, how playback is driven by the actor system, which actors
matter, what the editor's Demo Player does, and how to author a custom
cutscene.

## Contents

| Doc | Topic |
| --- | --- |
| [01-overview.md](01-overview.md) | What a cutscene actually is — the actor-driven model, why there's no timeline. |
| [02-data-flow.md](02-data-flow.md) | DATACNF → demo.gcx → GCL_LoadScript → GCL_ExecScript → actor system. |
| [03-key-actors.md](03-key-actors.md) | CINEMA, WT_VIEW, DEMODOLL, EMITTER, FADEIO and how they cooperate. |
| [04-camera-pipeline.md](04-camera-pipeline.md) | How runtime camera state flows: streamed `FrameRunDemo` (or per-stage overlay) → gUnkCameraStruct2 → camera.c → DG_LookAt → DG_Chanls[0].eye_inv. |
| [05-editor-player.md](05-editor-player.md) | The editor's Demo tab + Demo Player — Play/Pause/Stop, diagnostics, what it does and doesn't render. |
| [06-authoring.md](06-authoring.md) | Writing a `demo.gcl` for a custom stage; common patterns. |
| [07-debugging.md](07-debugging.md) | "Camera doesn't move", "Actors not spawning", missing CHARA registrations, NULL command lookups. |
| [08-known-issues.md](08-known-issues.md) | What still doesn't work in the editor's embedded playback. |
| [09-streamed-demos.md](09-streamed-demos.md) | The *other* cutscene path — `demo -s` / `demo -f`, `demothrd.c`, pre-baked DMO_DAT timelines from `DEMO.DAT` / `ZMOVIE.STR`. **Currently stalls in the editor** for stream-based; file-based is likely-but-untested. |

## Two cutscene paths

MGS has **two** cinematic systems and they work very differently:

1. **GCL-scripted** — `demo.gcx` runs at stage entry, spawns
   `CINEMA` + `WT_VIEW` + `DEMODOLL` actors, the actor system
   advances per `GV_Clock` tick. Animation comes from the actor
   `Act()` callbacks. Most of this folder describes this path.
2. **Streamed pre-baked** — a GCL `demo -s <code>` or `demo -f
   <file>` directive triggers `demothrd.c` to read per-frame
   `DMO_DAT` records from `DEMO.DAT` / `ZMOVIE.STR` and apply
   their baked camera + character poses directly. The "rendered
   cutscene" feeling comes from this path. See
   [09-streamed-demos.md](09-streamed-demos.md).

The editor's Demo Player **fully supports path 1** today.
Path 2 stalls (the FS streamer doesn't pump in the editor) — most
disc-shipped dramatic cinematics use it, which is why d00a's
opener animates while many `d-prefix` demos appear frozen mid-load.

## At a glance

- A cutscene is an imperative GCL script. There's no timeline, no
  keyframe editor, no frame slider in the original engine — events
  fire because actors spawn at script init and drive their own state
  via `delay`, `mesg`, and per-tick `Act()` callbacks.
- The runtime camera comes from a *chain*: streamed `.dmo` records
  feed `FrameRunDemo` (or, for non-streamed scenes, a per-stage
  overlay actor like `democame.c`/`intr_cam.c`) which writes
  `gUnkCameraStruct2_800B7868`. The `camera.c` actor reads that
  struct and calls `DG_LookAt` on `DG_Chanl(0)` every tick.
  `port_RenderObjects` projects every actor's mesh through the
  resulting `eye_inv` matrix. `WT_VIEW` is *not* in this chain —
  it's a water visual effect.
- The editor can play a cutscene in-place by reusing the engine's
  actor system + render path, without launching `./mgs`. See
  [05-editor-player.md](05-editor-player.md).
- Snake / Meryl / guards in cutscenes are `DEMODOLL` actors that
  play canned animations driven by message events — they do **not**
  need controller input or HZD collision, which is what makes
  embedding playback in the editor tractable.

## Quick start (editor playback)

```
cd port/editor
make
./editor --iso ../ISO/mgs.cue --stage d00a
```

Open the **Demo** tab in the inspector. If `d00a/demo.gcx` is in the
stage's cache, **Play** spawns every demo actor + the camera-driver,
the 3D View pane switches to the cutscene's camera, and the frame
counter ticks. **Pause** freezes; **Stop** reloads the stage to reset.

## Quick start (live game)

```
cd port
make
PORT_AUTOLOAD_STAGE=d00a PORT_GL=1 ./mgs ./ISO/mgs.cue
```

Same scene, full audio + UI + game logic. The editor's playback is
read-only and currently lacks audio + 2D HUD; the live game has both.
