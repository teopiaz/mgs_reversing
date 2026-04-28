# Streamed pre-baked cinematics (`demothrd`)

There are **two** kinds of cutscene in MGS, and the rest of this
documentation only covers one. Time to make the other explicit.

## The two paths

| Kind | Triggered by | Implemented in | Driven by | Editor status |
| ---- | ------------ | -------------- | --------- | ------------- |
| **GCL-scripted** (the kind covered by everything else in this folder) | The `demo.gcx` script auto-runs at stage entry. Spawns CINEMA + WT_VIEW + DEMODOLL et al. | `source/takabe/cinema.c`, `source/takabe/wt_view.c`, `source/animal/doll/demodoll.c`, the actor system | The actor system. Each `Act` callback advances state per `GV_Clock` tick. | ✅ supported |
| **Streamed pre-baked** | The GCL `demo -s <code>` or `demo -f <file>` command somewhere in `scenerio.gcl` / `demo.gcl`. | [`source/kojo/demothrd.c`](../../../source/kojo/demothrd.c), [`source/kojo/demo.c`](../../../source/kojo/demo.c) | A pre-baked timeline of per-frame `DMO_DAT` records read from `DEMO.DAT` (file-based) or streamed from `ZMOVIE.STR` (stream-based). Each record carries camera + per-character pose for one frame. | ❌ not yet — `FS_StreamGetData` returns NULL because the editor doesn't pump the FS streamer. |

The streamed path is what plays the *big* cinematics — the
long opening fly-by, dramatic codec sequences with synchronised
voice + animation, boss intros where the camera tracks a
character through scripted choreography. It's the one most people
think of when they say "MGS cutscene".

## Data shape (`DMO_*` structs)

Both stream and file demos share the same per-frame payload.
Definitions live in [`source/include/fmt_dmo.h`](../../../source/include/fmt_dmo.h).

### `DMO_DEF` — header

Once per cutscene. Lists the maps + character models the
timeline will reference.

```c
typedef struct {
    u_int    tag;          // marker, validated on load
    int      frame;        // header always frame 0
    int      n_frames;     // total cutscene length in frames
    int      n_maps;       // # background map references
    int      n_models;     // # character model references
    DMO_MAP *maps;         // [n_maps]
    DMO_MDL *models;       // [n_models]
} DMO_DEF;
```

`DMO_MAP { cache_id, filename }` — KMD blob the cutscene wants
loaded; usually they're already in the stage's resident DAR but
streamed cinematics can pull additional rooms.

`DMO_MDL { type, flag, cache_id, filename, name }` — character
KMD + animation data references.

### `DMO_DAT` — per-frame

Read from the stream / file once per cutscene frame (typically
1 stream-tick = 2 engine ticks, so 30 fps).

```c
typedef struct {
    u_int    tag;          // 0x...... — frame marker
    int      frame;        // frame index (used to sync stream consumption)
    short    eye_x, eye_y, eye_z;          // camera eye (PSX world units)
    short    center_x, center_y, center_z; // camera lookat target
    short    roll;                         // camera roll (degrees << ?)
    short    clip_dist;                    // PSX H register (FOV)
    short    n_charas;
    DMO_CHA *chara;        // [n_charas] — character spawn / despawn events
    short    n_adjusts;
    DMO_ADJ *adjust;       // [n_adjusts] — per-character pose this frame
} DMO_DAT;
```

This is **the camera and the choreography baked**: every frame
of the cinematic carries the exact eye/center/roll the camera
should hold and the exact per-character rot/pos/joint-rotation.
`FrameRunDemo()` reads one DMO_DAT and applies its values
directly — *no actor-driven interpolation*.

### `DMO_ADJ` — per-character pose

```c
typedef struct {
    int      type;
    short    visible;
    short    rot_x, rot_y, rot_z;
    short    pos_x, pos_y, pos_z;
    short    n_rots;
    short   *rots;         // [n_rots * 3] joint-rotation triplets
} DMO_ADJ;
```

`rots` is the skeletal-animation payload: per-joint Euler
rotations applied to the character's KMD model. Streamed demos
contain ~30–60 joint frames per character per cutscene-frame,
which is what gives them their "rendered animation" feel as
opposed to GCL demos' "code-driven puppet" look.

### `DMO_CHA` — spawn / despawn events

24-field struct (see [fmt_dmo.h](../../../source/include/fmt_dmo.h)) —
not every frame has these; only the ones that introduce or
remove a character mid-cutscene.

## Trigger chain

```
demo.gcl   GCL_ExecScript walks…
           …chara &CINEMA $s:HHHH -t T
              spawns the cinema actor (the GCL-scripted kind, level 3, draws fade bars)
           …  optionally:
           demo -s 17                                ← stream-based scene #17
           demo -f "DEMO/INTRO.DMO"                  ← file-based path
              ↓ GM_Command_demo (source/game/script.c:771)
              ↓ DM_ThreadStream(flags, code) or DM_ThreadFile(flags, name)
                 ↓ GV_NewActor at GV_ACTOR_MANAGER level
                 ↓ FS_StreamOpen() (stream) or PCopen() + read (file)
                 ↓ Act callback registers as StreamAct or FileAct
PER FRAME:    StreamAct / FileAct
              ↓ FS_StreamGetData(5) → DMO_DAT *
              ↓ FrameRunDemo(work, dat)
                  · update DG_Chanl(0).eye_inv from dat->eye/center/roll
                  · for each adjust: apply pos/rot/skeletal rotations
                                     to the corresponding DEMO_MODEL
              ↓ FS_StreamClear(data) (advance stream)
              ↓ until work->frame >= header->n_frames → GV_DestroyActor
```

The big things to notice:

- **The camera bypass.** Streamed demos do **not** go through the
  WT_VIEW → gUnkCameraStruct2 → camera.c → DG_LookAt chain
  documented in [04-camera-pipeline.md](04-camera-pipeline.md).
  `FrameRunDemo` writes the camera matrix more directly. So during
  a streamed cutscene, channel 0's eye_inv is being driven by
  `demothrd.c`, not by the camera.c actor.

- **The data lifetime.** The DMO_DAT pointer comes from the FS
  streamer's ring buffer; `FS_StreamClear(data)` releases it back
  to the streamer. On the live game's PSX-emulating port, this
  involves the stream sectors being read off disc 1 frame ahead
  of consumption. On the editor today, the streamer isn't ticking,
  so `FS_StreamGetData(5)` returns NULL forever.

- **The skip path.** `FileAct` checks `GV_PadData[1].status &
  PAD_CROSS` and bails out — you can press X to skip a streamed
  cutscene. The GCL-scripted kind doesn't have a skip path
  built-in (it relies on `STATE_PADRELEASE` and CINEMA's own
  termination logic).

## Editor support (or lack of)

The editor's Demo Player today:

- Loads `demo.gcx` via `GCL_LoadScript` + `GCL_ExecScript`.
- Spawns whatever the script's top-level `chara` directives spawn.
- Ticks `GV_ExecActorSystem` per frame.

If `demo.gcx` contains a `demo -s <code>` or `demo -f <file>`
directive, the script's `GM_Command_demo` handler runs and calls
`DM_ThreadStream` / `DM_ThreadFile` like normal. The `DemoWork`
actor is created. Then:

- **`DM_ThreadStream`** path: `FS_StreamOpen()` is called. The
  editor's `port/libfs/libfs.c` exposes a stream API but the
  per-frame `FS_StreamGetData` requires the streamer to actually
  pump bytes into its ring buffer, which currently only happens
  in `port/main_game.c`'s sound block. `StreamAct` polls and
  always sees NULL — the actor sits indefinitely.
- **`DM_ThreadFile`** path: `PCopen` reads the demo file off
  disc. The editor's libfs supports this. Then `FrameRunDemo` is
  called per frame and the cutscene plays. **This path probably
  does work in the editor, but no UI exists to invoke it.**

So pragmatically:

| Scenario | Today's editor |
| -------- | -------------- |
| GCL demo with only CINEMA / WT_VIEW / DEMODOLL spawns (e.g. d00a) | ✅ plays |
| GCL demo that calls `demo -s <code>` (most disc cinematics) | ❌ DemoWork stalls, screen frozen |
| GCL demo that calls `demo -f "PATH"` (file-based) | ⚠ untested — likely plays |
| Custom-stage cutscene | ✅ plays as long as it stays in the GCL-scripted style |

## What it would take to support streamed cinematics

Three engine-side hooks the editor doesn't run today:

1. **FS streamer pump.** Periodically call the streamer's tick so
   `FS_StreamGetData` returns frames as they arrive. The live
   game does this implicitly via `mts_sta_tsk(SOUND_INT, …)` (the
   sound IRQ also pumps the streamer). The editor would need an
   equivalent — likely call `FS_StreamTick` (or its equivalent)
   from `ed_demo_tick`.
2. **Sound pumping.** Streamed cinematics ship VOX audio tracks
   in the stream — `FrameRunDemo` calls into the sound system
   to advance them. Even ignoring audible output, the engine
   may expect the SPU state to advance for sync purposes.
3. **`port_set_iso_path` for stream sectors.** Stream reads go
   to specific LBAs in the disc image. The editor's `IsoImage`
   reader supports the right modes; just needs to be wired into
   the streamer's data path.

Status: untouched. Adding it is a meaningful chunk of work
because `FS_StreamGetData` / `FS_StreamClear` / `FS_StreamGetTick`
together expose the original PSX streaming semantics, and our
port emulates them in `port/libfs/libfs.c` — but the *clock*
that drives them is currently main_game.c-only.

## Identifying which kind a stage uses

Read the stage's decompiled GCL:

```bash
grep -E "^\s*demo " port/gcl/decompiled/*/demo.gcl  port/gcl/decompiled/*/scenerio.gcl
```

Any line matching `demo -s` / `demo -f` is a streamed-cinematic
trigger. Stages without such a line use only the GCL-scripted
path and play fine in the editor's current Demo Player.

Cutscene-heavy stages (`d00a`, `d01a`, `d11c`, `d18a`, etc.)
typically use the streamed path for their dramatic openers and
the GCL-scripted path for in-stage transitions and codec calls.
That's why some demos play in the editor and others freeze.

## Suggested next steps (ordered by impact)

1. **Detect** — when `demo -s` / `demo -f` runs, log a clear
   "[demo] streamed cutscene NN — needs FS streamer support; will
   freeze" message instead of silently stalling. One-line patch
   in `GM_Command_demo`'s port-side hook.
2. **Wire up the file-based path** — `DM_ThreadFile` likely works
   already; just needs a UI button + the demo file's path. Easy
   win.
3. **Pump the streamer** — bring `FS_StreamTick` into
   `ed_demo_tick`. Real work but unlocks the majority of disc
   cinematics.
4. **Add audio pumping** — the cinematics have synced VOX. Optional
   for visual playback but needed for the proper cinematic
   experience.

Until these land, the editor is a *GCL-cutscene viewer*, not a
full cutscene viewer. The README's "Quick start (editor playback)"
under-promises this — see
[08-known-issues.md](08-known-issues.md) for the explicit
limitation.
