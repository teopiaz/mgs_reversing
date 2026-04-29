# `source/kojo/` — streamed cinematics + special-purpose actors

Per-author folder. "Kojo" is the name of one of the lead
programmers; everything authored by him lives here. The big
contribution: the **streamed cinematic system** (the path that
plays the disc's dramatic cinematics).

## Files

| File | Lines | Role |
| ---- | ----- | ---- |
| [`demo.c`](../../../../source/kojo/demo.c) | ~2400 | The cinematic playback engine — `CreateDemo`, `FrameRunDemo`, `MakeChara`, `demothrd_8007CFE8` (per-frame DMO_ADJ apply). The largest single file in `source/`. |
| [`demothrd.c`](../../../../source/kojo/demothrd.c) | ~500 | The DemoWork actor — `DM_ThreadStream` / `DM_ThreadFile` / `StreamAct` / `FileAct`. Polls FS streamer or reads from a file, calls `FrameRunDemo`. |
| [`demo.h`](../../../../source/kojo/demo.h) | small | Shared types — `DemoWork`, `DEMO_MODEL`, `ACTNODE`, etc. |
| [`m1e1catr.c`](../../../../source/kojo/m1e1catr.c) | ~? | M1A1/E1 caterpillar tracks animator — the tank's tracks need bespoke logic for the segmented mesh + smoke. |
| [`m1e1.h`](../../../../source/kojo/m1e1.h) | small | M1A1 type defs. |
| [`famaslit.c`](../../../../source/kojo/famaslit.c) | ~? | FAMAS muzzle light + smoke. |
| [`inverlt2.c`](../../../../source/kojo/inverlt2.c) | ~? | Inverter (?) — possibly an LED display effect. |
| [`sstorm.c`](../../../../source/kojo/sstorm.c) | ~? | Sand-storm / dust-storm effect (s07c canyon). |

## demo.c + demothrd.c — the streamed cinematic engine

These two are the **runtime** for the disc's `.dmo` files. The
authoring side is documented in
[doc/demo/](../../demo/); here we focus on the runtime.

### High-level flow

```
GCL `demo -s t:NNNN`
        ↓
GM_Command_demo (source/game/script.c)
        ↓
DM_ThreadStream(flag, code)
        ↓
GV_NewActor → DemoWork actor at level 3
        ↓
StreamAct (per frame):
   FS_StreamGetData(5) → DMO_DEF or DMO_DAT
   if first time:  CreateDemo(work, def)        → spawn DEMO_MODELs
   else:           FrameRunDemo(work, dat)      → apply per-frame data
        ↓
   FrameRunDemo:
      write gUnkCameraStruct2.eye/center
      DG_SetPos2 / ReadRotMatrix → write DG_Chanls[0].eye_inv directly
      iterate chara[] events     → spawn / remove charas
      iterate adjust[] events    → set DEMO_MODEL pos/rot/rots[]
```

### CreateDemo

[`demo.c:30-130`](../../../../source/kojo/demo.c). One-shot
init when the cinematic starts:

1. Apply `OFFSET_TO_PTR` fixups to maps[] / models[] (skipped on
   PORT_BUILD — the editor's StreamAct does the conversion).
2. Allocate `DEMO_MODEL` array sized to header's n_models.
3. For each model: `GV_GetCache(cache_id)` — confirm KMD is
   loaded. `GM_InitObject` to allocate `DG_OBJS`.
4. Special-cases by filename (`m1e1`, `hind`) for vehicle setups
   that need extra geometry.

### FrameRunDemo

[`demo.c:385+`](../../../../source/kojo/demo.c#L385). Per-frame
work:

1. Apply offset fixups for chara[] / adjust[] arrays.
2. Write `gUnkCameraStruct2.eye/center` from data->eye_x,y,z.
3. Compute view matrix and write `DG_Chanls[0].eye_inv` directly
   (bypassing camera.c).
4. Walk chara events (`MakeChara`).
5. Walk adjust events (`demothrd_8007CFE8`) — apply pos/rot/rots
   to each DEMO_MODEL.
6. Special-case dispatch for `m1e1` / `hind` via
   `demothrd_m1e1_8007D404` / `demothrd_hind_8007D9C8`.

### MakeChara

When a `DMO_CHA` event says "spawn this character at this frame",
`MakeChara` calls into the chara factory system. The character is
typically a DEMODOLL (puppet) configured with `step_size = 0` so
the cinematic data drives its position absolutely.

## m1e1catr.c — M1A1 tank caterpillar tracks

The M1A1 (referenced as M1E1 in code) tank in s07c's boss fight
has caterpillar tracks that need per-frame procedural animation:

- The track segments slide around the wheels as the tank moves.
- Each track segment is a separate KMD chunk.
- Smoke emits from the track-ground contact points when moving
  fast.

Special logic in `demothrd_m1e1_8007D404`:

```c
DG_VisibleObjs(model->extra->object[0][model->extra->field_558_idx[0]].objs);
DG_VisibleObjs(model->extra->object[1][model->extra->field_558_idx[1]].objs);
```

Visibility flips per-track on each side as the tank rolls — the
visible-segment cycles through `field_558_idx[0..2]` to make the
track look like it's revolving.

`m1e1catr.c` provides `M1E1GetCaterpillerVertex` which extracts
the bottom-of-track world points used for smoke spawn points.

## famaslit.c — FAMAS muzzle light

Authoring quirk: the FAMAS rifle's muzzle flash + smoke have a
distinct authored look (longer flame than SOCOM, more smoke). The
team broke this out into its own file rather than reusing
`anime/effect/socom.c`.

## inverlt2.c — Inverter

Visual effect of unclear purpose. Possibly the radar's
oscilloscope or one of the stage-display screens that shows
animated patterns.

## sstorm.c — sandstorm

Particle effect for s07c's exterior canyon sandstorm. Uses
`gUnkCameraStruct2_800B7868.eye/center` to determine which
particles are in view and skips off-screen.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (streamed cinematic
  format, per-actor magic constants).
- [`doc/demo/`](../../demo/) — full coverage of the cinematic
  system from script-author perspective.
- [`doc/demo/02-data-flow.md`](../../demo/02-data-flow.md) — the
  end-to-end data path through this folder.
- [`doc/demo/10-dmo-format.md`](../../demo/10-dmo-format.md) —
  wire-format reference.
- [`source/game/script.c`](../../../../source/game/script.c) —
  `GM_Command_demo` is what dispatches into `DM_ThreadStream`.
