# What a cutscene actually is

If you're coming from a modern engine, the word "cutscene" suggests a
timeline editor with keyframed cameras, animation clips per character,
maybe a dialogue track. MGS has **two** cutscene systems — and both
are weird from a modern perspective.

This file covers the **GCL-scripted** path: bytecode loaded at stage
entry, walked once to spawn a handful of *actors*, after which each
actor's per-frame `Act()` callback runs the cinematic on its own.
For the *other* path — pre-baked timeline data from `DEMO.DAT` /
`ZMOVIE.STR` consumed by `demothrd.c::FrameRunDemo` — see
[09-streamed-demos.md](09-streamed-demos.md). Most of the disc's
dramatic cinematics use the streamed path; the "in-stage" cinematics
(scene transitions, codec calls, environmental fly-bys) use the
GCL-scripted one.

There is no timeline because there is no scrubber. There are no
keyframes because nothing seeks. The script is a one-shot init
sequence; everything dynamic happens inside the actor system from
that point on.

## The actor-driven model

When the engine enters a stage that has a `demo.gcx`:

1. `GCL_LoadScript(blob)` parses the bytecode header and stores
   pointers to the proc table and script body in `current_script`.
2. `GCL_ExecScript()` walks the script body once. Every top-level
   `chara &TYPE $s:NNNN -p X Y Z …` directive resolves the type's
   factory function and calls it, which `GV_NewActor`s the actor
   into one of nine actor lists (`gActorsList_800ACC18[0..8]`).
3. From the next tick onwards, `GV_ExecActorSystem()` walks every
   list and calls each actor's `Act()` callback. That's where all
   the cinematic motion comes from.

A typical 30-second cutscene contains:

| Actor type   | Role | Count |
| ------------ | ---- | ----- |
| `CINEMA`     | Owns the cutscene's *lifetime*. `-t 30000` means the demo runs for 30000 ticks. Draws the black framing bars on `DG_Chanl(1)`. | 1 |
| `WT_VIEW`    | Cutscene-camera *animator*. Reads its `-b` bounds and `-c` color params, mutates `gUnkCameraStruct2_800B7868.eye/center/zoom` per frame. | 1 |
| `DEMODOLL`   | A character (Snake, Meryl, a guard) playing a canned animation clip referenced by `-m $s:HHHH`. | 1–N |
| `EMITTER`    | Particle / smoke / light spawner. | 1–10 |
| `WALL`       | Static prop placeholder (a piece of geometry the demo expects to be visible). | 1–N |
| `LAMP`       | Light source. | 1–N |
| `FADEIO`     | Screen fade in/out at the start/end. | 1–2 |
| `RADIO`      | Codec call trigger. | 0–N |
| `JIMAKU`     | Subtitle. | 0–N |

Time advances because the actor system itself advances: `delay` and
`mesg` directives inside procs schedule events relative to the running
tick counter (`GV_Clock`). A demo "playing back" is just the engine
ticking the actor system 30000 times.

## Why there's no scrub bar

To "seek to frame 1500" of a cutscene, the engine would have to:
- Reset every actor (free + re-spawn from the script's init pass)
- Tick all 1500 frames as fast as it can to land in the right state

The original engine never does this. The disc shipped with games that
play cutscenes start-to-end. The editor's Demo Player implements
*Play / Pause / Stop / Step* — Stop is the only "seek", and it seeks
to frame 0 by reloading the stage.

## What gets persisted between cutscenes

Almost nothing. The actor system is wiped on stage transitions. The
GCL variable space (`$w:`, `$b:`, `$f:` globals) does carry, which is
how a cutscene tells the *next* scene what happened (which weapon
Snake picked up, which key event flag flipped). But the actors
themselves are ephemeral.

This means: when authoring a cutscene, you don't need to "tear down"
state at the end. The next stage transition or `Stop` does that for
you.

## File locations

```
port/overlays/<stage>/demo.gcl                  ← decompiled text source
                       /demo.gcx                 ← bytecode produced by tools/gcl2gcx.py
                       /demo.tail                ← font / glyph trailer (Japanese stage fonts)
port/gcl/decompiled/<stage>/demo.gcl             ← full decompile from disc (text only)
port/editor/data/<stage>_actors.json             ← actor list extracted by tools/extract_actors.py
                                                    (includes "source": "scenerio" or "demo")
port/editor/extra_stages/<name>/datacnf.bin      ← custom-stage build output containing scenerio + demo .gcx
```

The full disc decompile under `port/gcl/decompiled/` covers every
stage, including ones whose runtime overlay code isn't ported yet —
it's text-only, intended for inspection.

## Where this differs from the rest of the engine

A cutscene runs through *exactly the same* GCL + actor + render path
as gameplay. The only practical differences:

- `DEMODOLL` characters don't read controller input (they replay
  canned animations) — Snake during a cutscene is a `DEMODOLL`, not
  the regular `SNAKE` actor.
- `WT_VIEW` overrides the camera that gameplay's third-person /
  first-person logic would normally produce.
- `JIMAKU` / `RADIO` actors expect text/voice to play; the editor's
  embedded player skips audio.

Read [02-data-flow.md](02-data-flow.md) for the byte-level path of a
single cutscene from disc to screen.
