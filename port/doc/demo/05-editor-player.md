# Editor Demo Player

The editor binary (`port/editor/editor`) embeds the engine's actor
system + render pipeline so cutscenes can be played back without
launching `./mgs`. This file documents what the player can do, what
it deliberately doesn't, and the controls + diagnostics it exposes.

## What it is

When you press **Play** on the **Demo** tab while a stage is loaded,
the editor:

1. Starts the GCL daemon if not already (`GCL_StartDaemon` —
   parser/var/builtin commands), and pins `scenerio_code` to a
   sentinel so subsequent stage loads don't auto-run scripts.
2. Calls the game-side init helpers normally invoked by
   `GM_StartDaemon`:
   - `GM_InitArea()` — area / region tracking
   - `GM_InitChara()` — chara-type → factory table
   - `GM_InitScript()` — registers `chara` / `light` / `map` /
     `mapdef` / `mesg` / `delay` / `radio` / `pad` / `sound` /
     `menu` / `rand` / `func` / `jimaku` GCL commands
3. Looks up the demo blob by hash 0x6A242 (`gv_strcode("demo") |
   ('g'<<16)`) in `GV_CacheSystem`. Aborts cleanly if missing.
4. Runs the per-stage prelude that gamed.c normally would:
   - `GM_ResetMap()` — initial GM_CurrentMap / GM_Camera defaults
   - `NewCameraSystem()` — spawns the camera-driver actor that calls
     `DG_LookAt(DG_Chanl(0), …)` every frame
5. `GCL_LoadScript(demoBlob)` + `GCL_ExecScript()` — walks the script
   body, fires every top-level `chara`/`map`/`mesg`/`light` directive,
   spawning the cutscene's actors into the actor system.
6. Sets state to `PLAYING`. From the next editor frame onwards,
   `ed_demo_tick()` calls `GV_ExecActorSystem()` once per editor
   frame, advancing every actor.
7. The render path (`ed_render_frame_demo`) reads `DG_Chanls[ci]`
   where `ci = g_demo_active_chanl` (auto-detected — see
   [04-camera-pipeline.md](04-camera-pipeline.md)) and projects the
   live `DG_OBJS` list through that camera. The editor's static-map
   walker still runs after, layering map geometry beneath the
   animated actors.

## What it isn't (yet)

- **Streamed cinematics.** A demo whose `demo.gcx` invokes
  `demo -s <code>` to play a pre-baked stream from `ZMOVIE.STR`
  via [demothrd.c](../../../source/kojo/demothrd.c) stalls — the
  editor doesn't pump the FS streamer that
  `FS_StreamGetData(5)` waits on. See
  [09-streamed-demos.md](09-streamed-demos.md) for the full
  picture and the fix sketch. This is what's playing in any
  cutscene that "starts but freezes immediately"; a cutscene that
  *plays* in the editor (d00a's opener, many in-stage scenes) is
  using only the GCL-scripted path.
- **Audio.** The editor's sound system is initialised so
  `FS_LoadStageRequest` can write to SPU memory without crashing,
  but the per-tick `SdInt` / `IntSdMain` / `StrSpuTrans` pumps
  aren't wired into `ed_demo_tick`. Cutscene voices and music are
  silent.
- **Subtitle / radio HUD.** `JIMAKU` / `RADIO` actors spawn and tick,
  but the editor's render path doesn't run `DG_DrawOTag` for the
  2D channel that hosts those primitives — text and codec portraits
  don't appear. Add `DG_DrawOTag(GV_Clock)` after `port_RenderObjects`
  in `ed_render_frame_demo` to enable them.
- **Seek / scrub.** No frame slider. **Stop** seeks to frame 0 by
  reloading the stage; there's no way to jump to frame 1500. The
  underlying engine has no support for this.
- **Pad input.** The editor doesn't poll a controller into the
  engine's `GV_PadData` ring buffer. Cutscenes don't read it
  (`DEMODOLL` plays canned animations) so this isn't typically a
  problem, but any actor that DOES read pad input during a
  cinematic (rare) will see all-zeroes.
- **Per-stage overlay code.** The editor links most disc-shared
  actor sources (`source/takabe/*`, `source/animal/*`,
  `source/anime/*`, etc.) but not every per-stage overlay's chara
  table. A demo that uses an overlay-private chara type will log
  `[gcl] chara: func not found (hash=0xNNNN)` and that actor will
  not spawn.

## Controls

The Demo Player section sits at the top of the **Demo** tab,
directly above the actor-by-category listing.

| Button | Action | Idempotent? |
| ------ | ------ | ----------- |
| **Play** | Boot engine state if needed, run script, start ticking | yes — calling Play during PLAY is a no-op; from PAUSE it resumes |
| **Pause** | Stop ticking. Actors freeze in place. | yes |
| **Resume** | (replaces Play label after Pause) | |
| **Stop** | Reload stage to wipe all engine state. Resets frame counter. | yes — Stop while STOPPED is a no-op |
| **Step** | Tick the engine exactly once and pause | works in any state |

## Live readout

While a demo is loaded:

```
Camera (chanl 0)   pos -3142, 1500, 8472
                   yaw 22.4°   pitch -8.1°   roll 0.0°
```

The `chanl N` parenthetical reflects which `DG_Chanls[]` slot is
being treated as authoritative (auto-detected per
[04-camera-pipeline.md](04-camera-pipeline.md)). Position is in
PSX world units. Rotations are Z-Y-X Euler decomposed from the
runtime `eye_inv`.

## Diagnostics tree

A collapsing block below the camera readout exposes engine state
the user needs to debug "why doesn't this play":

| Line | Meaning | Suspicious if… |
| ---- | ------- | -------------- |
| `GV_Clock` | Engine tick counter, increments per `GV_ExecActorSystem` call | Stays at 0 → ticks aren't firing |
| `Live actors` | Walks `gActorsList_800ACC18[0..8]`, mirrors `GV_DumpActorSystem` | == 1 (only `gvd.c`) means no chara directives spawned |
| `GM_GameStatus` | Snapshot. Camera Act gates on `>= 0` | Negative → `STATE_PADRELEASE` set, camera Act skips |
| `GV_PauseLevel` | Snapshot. Camera helpers gate on `== 0` | Non-zero → DG_LookAt fires but with stale inputs |
| `Channels updated` | Bitmask: bit N if `DG_Chanls[N].eye_inv` changed last tick | All `-` while Live actors > 1 → no engine code drives the camera |
| **Dump actors → stdout** | Calls `GV_DumpActorSystem`. Lists every alive actor with its file/proc name | Use this to confirm `wt_view.c` and `camera.c` are in the list |

Two contextual warnings render in red beneath when conditions point
at a known cause:

- **"0 active actors after Play"** — GCL bytecode loaded but
  `GCL_ExecScript` didn't fire. Would mean the `chara` command
  isn't registered (didn't call `GM_InitScript`).
- **"Actors running but no DG_Chanls write"** — actor system
  ticks, but no engine code touched `eye_inv`. Most commonly:
  `WT_VIEW` factory not registered, so `gUnkCameraStruct2` stays
  zero and `DG_LookAt` keeps producing the same matrix.

## Implementation files

| File | Role |
| ---- | ---- |
| [port/editor/ed_demo.c](../../../port/editor/ed_demo.c) | Engine driver — Play/Pause/Stop, GCL_LoadScript + GCL_ExecScript, per-tick state mirror. |
| [port/editor/ed_demo](../../../port/editor/editor.h) (`editor.h` decls) | Public API to ed_ui / main.c. |
| [port/editor/ed_render.c](../../../port/editor/ed_render.c) | `ed_render_frame_demo` — the demo-mode render entry that uses `DG_Chanls[g_demo_active_chanl].eye_inv` and calls `port_RenderObjects(GV_Clock)`. |
| [port/editor/main.c](../../../port/editor/main.c) | Calls `ed_demo_tick()` once per editor frame; routes the 3D viewport's render to `ed_render_frame_demo` while `g_demo_loaded`. |
| [port/editor/ed_ui.cpp](../../../port/editor/ed_ui.cpp) `tab_demo` | The Demo Player UI section. |
| [port/extern_stubs.c](../../../port/extern_stubs.c) | `MainCharacterEntries[]` — the registration list. Add `CHARA_*` entries here when stderr logs `chara: func not found`. |

## Per-stage actor coverage

Different cutscenes use different actor types. Your stage's demo will
work iff every chara hash it spawns has a `CHARA_*` entry in
`MainCharacterEntries[]`. The currently-registered set in
`port/extern_stubs.c` covers all the d00a / s01a / s02a / s03a-class
demos (snake intro, codec calls, opening fly-throughs).

If a new stage hits `chara: func not found (hash=0xNNNN)`, the fix is
mechanical:

1. Find `0xNNNN` in
   [`source/include/charalst.h`](../../../source/include/charalst.h)
   (search for `{ 0xNNNN,` literal in the macro definitions).
2. Note the macro name (`CHARA_<NAME>`).
3. Add it to `MainCharacterEntries[]` in
   [`port/extern_stubs.c`](../../../port/extern_stubs.c) near the
   existing demo entries.
4. Rebuild the editor (and live game) — `make -j8` from `port/`.

If the constructor's source file isn't in `port/obj/` (the
factory itself is missing, not just unregistered), that's a
separate decompilation gap; check
[`port/doc/12-todo.md`](../12-todo.md).
