# Known issues with cutscene playback

Tracked limitations of the editor's embedded Demo Player + the live
game's port of cutscene logic. Anything explicitly out of scope for
the editor is in [05-editor-player.md](05-editor-player.md) under
"What it isn't (yet)"; this file is for *bugs and incomplete
implementations* that we intend to fix.

## Editor Demo Player

### No audio

The editor inits the SPU emulator (`spu_emu_init` + `sd_init`)
because `FS_LoadStageRequest` writes wave data through `SpuWrite`
during stage load, and skipping it would crash the loader. But the
per-frame sound pumps (`SdInt`, `IntSdMain`, `StrFadeInt`,
`WaveSpuTrans`, `StrSpuTrans`) aren't called from `ed_demo_tick`.

Result: cutscenes play silent. RADIO actors spawn but no voice
streams; cinematic music doesn't start.

**Fix sketch:** mirror the per-tick sound block from
[port/main_game.c:345-380](../../../port/main_game.c#L345) into
`ed_demo_tick` after the `GV_ExecActorSystem` call. Tradeoff: the
editor's main loop runs at editor framerate (often >60Hz when the
3D pane is hovered), but the engine ticks at 30Hz nominally. Audio
might double-up if not throttled.

### No 2D HUD / subtitles / codec portraits

`port_RenderObjects(GV_Clock)` handles the 3D channel. The 2D
channel (DG_Chanl(1) — fade rectangles, JIMAKU text, RADIO
portraits) is rendered by `DG_DrawOTag(GV_Clock)` in the live
game. The editor's `ed_render_frame_demo` doesn't call it, so 2D
prims accumulate in the OT every frame and never draw.

**Fix:** add `DG_DrawOTag(GV_Clock)` after `port_RenderObjects` in
`ed_render_frame_demo`. Possible side-effect: subtitle / codec
text would draw inside the 3D pane's content rect, which may
interact strangely with the dock layout. Test before committing.

### Stop is heavy

`Stop` reloads the entire stage to reset the actor system + GCL
state. That's bulletproof but takes ~1 second for big DATACNFs. For
a quick "frame 0" reset, a partial-reset path that only nukes the
actor list + re-runs `GCL_ExecScript` would be much faster.

**Fix sketch:** new `ed_demo_reset_actors_only()` calling
`GV_DestroyActorSystem(GV_ACTOR_LEVEL)` + re-running the prelude
(GM_ResetMap → NewCameraSystem → GCL_ExecScript). Risk: actors
mutated cache state (e.g. KMD textures) that ed_load_stage clears
along with the actors; partial reset would leave it dirty.

### No seek / no scrub bar

To jump to "frame N" we'd reset to frame 0 + tick N times as fast
as the actor system can run. For a 30000-frame demo (8 minutes
real time at 60Hz), that's a few seconds of wall-clock spinning.
Doable, but not implemented.

**Fix sketch:** add a "Seek to frame" input that:
1. Calls Stop (or the partial reset above).
2. Sets `g_demo_state = ED_DEMO_PLAYING`, hides the render output.
3. Loops `ed_demo_tick()` N times back-to-back.
4. Re-enables render, switches to PAUSE.

The actor system is deterministic given the same script + initial
state, so seek is reproducible. But: any actor that depends on
real-time wall-clock (rare but exists for some demos with
synchronised audio cues) will desync.

### Some demo-private chara hashes aren't registered

Currently registered: see `MainCharacterEntries[]` in
[`port/extern_stubs.c`](../../../port/extern_stubs.c). Disc
cutscenes may reference per-stage overlay charas that aren't in
that list — they log `chara: func not found (hash=0xNNNN)` and
are silently skipped.

Tracked stages with missing entries (incomplete; expand as
demos are tried):

| Stage | Missing hash | Suspected actor |
| ----- | ------------ | --------------- |
| (none reported yet) | | |

When a new demo is tested, this table grows. Each entry is a one-
liner fix: add `CHARA_<NAME>` to the MainCharacterEntries array.

### Camera readout sometimes flickers between channels

`ed_demo.c::update_camera_snapshot` adopts whichever channel changed
last as the active one. If multiple channels write the same frame
(rare — a debug path, an overlay error), the readout can hop
between them frame-to-frame.

In practice all disc cutscenes write only `DG_Chanls[0]`, so this
is theoretical. If it ever bites, force the readout to channel 0
instead of last-write-wins.

## Live game

### CINEMA's `-t` lifetime can desync from real time

`-t 30000` means 30000 ticks at the engine's nominal 30 Hz. On the
port, the actor system can tick faster or slower than 30 Hz
depending on render frame rate. Cutscenes time-locked to audio (the
voice + subtitle pacing in d-prefix scenes) drift relative to wall
clock when the port runs faster than 30Hz.

**Fix:** the port's main loop already throttles GV_ExecActorSystem
to 30Hz; this should be tight. Drift has been observed but only
by a frame or two over a 30-second cutscene.

### Snake skip / pause during demos

The live game lets the player press Start to skip a cutscene
(`STATE_PADRELEASE` flips off, demo's `chara &CINEMA` self-
destructs early, normal flow resumes). The editor doesn't poll
pad input into the engine, so Start-to-skip doesn't work in the
embedded player. Use **Stop** instead.

## See also

- [05-editor-player.md](05-editor-player.md) — what the editor
  *does* implement.
- [07-debugging.md](07-debugging.md) — symptom-first reference for
  things that go wrong.
- [`port/doc/12-todo.md`](../12-todo.md) — wider port TODO list,
  some entries cross-cut into demo support.
