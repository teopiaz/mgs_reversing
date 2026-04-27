# Debugging cutscenes

Symptom-first reference for the editor's Demo Player and live-game
playback. Every entry includes the smoking-gun log line / diagnostic
value to look for and the typical fix.

## "Press Play, nothing happens"

### `[demo] stage 'X' has no demo.gcx in cache`

The stage doesn't ship a cutscene blob. Custom stages without an
authored `demo.gcl` log this. Some disc stages also don't have a
demo (most s-prefix gameplay stages).

**Fix:** confirm `port/gcl/decompiled/<stage>/demo.gcl` exists; if
yes, rerun `tools/import_stage.py` on the stage so the importer
emits demo.gcx into the DATACNF. (See
[06-authoring.md](06-authoring.md) — note importer-side gap.)

### `[demo] GCL_LoadScript refused the blob`

The .gcx in cache is malformed — likely a partial write or a
bytecode produced by an out-of-date compiler version.

**Fix:** rebuild the stage. `tools/gcl2gcx.py` round-trips disc
.gcx files byte-exact, so this should never trigger on disc data.

## "Frame counter ticks but I see nothing happening"

### Diagnostics → Live actors == 1

Only `gvd.c` is alive. Means `GCL_ExecScript` didn't fire any
`chara` directives — typically because the `chara` command isn't
registered.

**Fix:** `ed_demo.c::ed_demo_engine_init` must call `GM_InitArea`,
`GM_InitChara`, `GM_InitScript` *before* loading the demo. If you
forked an older snapshot that called only `GCL_StartDaemon`, the
GCL command table has only the four basic commands — anything else
NULL-derefs.

### Diagnostics → Live actors > 1, no `wt_view.c`

`Dump actors → stdout` and search for `wt_view.c` in the output.
If absent, `WT_VIEW` (chara 0x8E45) isn't registered.

```
[gcl] chara: func not found (hash=0x8E45)
```

**Fix:** add `CHARA_WT_VIEW` to `MainCharacterEntries[]` in
[`port/extern_stubs.c`](../../../port/extern_stubs.c). The factory
(`NewWaterView`) already exists in `source/takabe/wt_view.c`; the
fix is one line.

### Diagnostics → Channels updated `[- - -]`

Actors run, but no engine code writes any `DG_Chanls[].eye_inv`.
The cutscene-camera chain
([04-camera-pipeline.md](04-camera-pipeline.md)) is broken at one
of its steps.

Check in order:

1. **`Live actors` → `Dump actors`**: is `camera.c` listed at level
   2? If not, `NewCameraSystem()` wasn't called. Should be in
   `ed_demo_play`'s prelude — confirm the file matches what's in
   git.
2. **`GM_GameStatus`**: negative? Camera Act early-exits. Clear
   `STATE_PADRELEASE | STATE_ALL_OFF` before Play.
3. **`GV_PauseLevel`**: non-zero? Camera helpers skip; DG_LookAt
   fires but with stale inputs. Clear before Play.
4. **`Dump actors`**: is `wt_view.c` listed? If not, see above
   (CHARA_WT_VIEW unregistered).

## "It plays, but the camera is wrong / inverted / black screen"

### 3D pane is solid black

Most likely the camera's `clip_distance` (PSX H register) ended up
zero or absurd, projecting every face outside NDC.

**Check:** `Camera (chanl N) pos / yaw / pitch / roll` in the
diagnostic readout. Position values huge (>100 000) or tiny
(<-100 000)? `WT_VIEW` is animating to coords your stage's geometry
isn't near, so all tris are being culled.

**Fix:** for custom cutscenes, double-check `WT_VIEW`'s `-b X1 Y1
Z1 X2 Y2 Z2` bounds match your stage's actual extents. Disc demos
were authored against specific stage geometry — using a disc
`WT_VIEW` directive verbatim against a custom map's dimensions
won't frame correctly.

### Looks weird, mirror-imaged or upside down

PSX uses `+Y down` screen convention. The editor's free-fly camera
already accounts for this in `ed_camera.c`. The demo path uses the
engine's matrix directly, so it should be correct — but if you've
modified `ed_render_frame_demo`, check `s_eye_inv` is assigned from
`DG_Chanls[ci].eye_inv` and not from anything that flips Y.

### One actor is visible, others aren't

The visible actor's `Act` is succeeding; other actors are crashing
silently. `GV_ExecActorSystem` has a SIGSEGV handler that
`siglongjmp`s past a faulting actor — the actor list keeps walking
but the dead actor's faces don't render.

**Find the culprit:** run from a terminal so you see stderr — the
fault address printed by the handler tells you which actor.
Alternatively rebuild the editor without `-DPORT_BUILD` (or comment
out the handler in `actor.c:166`) so a real segfault drops into
lldb / gdb with a proper stack frame.

## "It plays but crashes mid-cutscene"

### `GCL_Command +N (command.c:56)` NULL deref

`FindCommand` returned NULL — a GCL command lookup failed mid-script.
Most commonly: `GM_InitScript` wasn't called, so non-builtin commands
are missing.

**Fix:** see "Live actors == 1" above.

### Actor `Act` SIGSEGV mid-tick

The faulting actor's name + filename are listed in the next
`GV_DumpActorSystem` output. Common offenders:

- **Snake actor** (not DEMODOLL — full Snake) — expects
  `GM_PlayerStatus` initialised + pad poll. The editor doesn't run
  Snake; if a demo spawns him, that's a chara mis-registration.
- **Items in cutscenes** — expect a current map. `GM_ResetMap()`
  in the prelude should cover this.
- **Overlay-private actors** — fault in code that wasn't compiled
  in. Hash will not appear in `MainCharacterEntries[]` and you'll
  see `chara: func not found` for it earlier in the log.

### `[gcl] chara: func not found (hash=0xNNNN)` (informational)

Not a crash — just a skip. The actor doesn't spawn. If it was a
critical actor (like WT_VIEW), the cutscene visually breaks.

**Fix:** look up the hash in
[`source/include/charalst.h`](../../../source/include/charalst.h),
add `CHARA_<NAME>` to `MainCharacterEntries[]`. The factory must
also be linked into `port/obj/`; if not, decompilation work is
needed.

## "It works in the editor but not in the live game (or vice versa)"

The two share the same engine code, but boot through it differently.
The editor explicitly skips `GM_StartDaemon` (which would run the
title-menu state machine) and runs a hand-curated subset of its
init steps. If your scene works in `mgs` but not in editor:

- `GameWork`-driven state transitions don't run in the editor.
  Anything reading `GM_PlayerStatus`, `GM_LoadComplete`,
  `GM_LoadRequest` can see different values from the live game.
  Solution: explicitly set the values in `ed_demo_play` before
  `GCL_ExecScript`.

If your scene works in editor but not in `mgs`:

- The live game runs `scenerio.gcx` first, then transitions to
  `demo.gcx` via a `chara &CINEMA` in scenerio — i.e. the cutscene
  is part of a normal stage flow. The editor goes straight to
  demo. Some demos depend on scenerio having run first (vars set,
  characters spawned). Solution: do the same in your scenerio.gcl,
  or move the dependency into demo.gcl directly.

## "Per-frame state is hard to inspect"

The diagnostics tree gives a snapshot of the *current* tick. To see
the trajectory:

- **Step** repeatedly. Each click ticks the engine once and pauses.
  Watch the camera readout / channel-dirty bitmask between steps.
- Add a temporary `printf` in the actor whose behaviour you're
  investigating (e.g. WT_VIEW.Act). The editor runs from a
  terminal; stdout is captured.
- For a recording: `script -t 0 /tmp/cutscene.log` to capture
  stderr to a file with timestamps, then play. Each tick produces
  a `[gcl]` line per chara fired so you can reconstruct the
  spawn order.

## When all else fails

Compare the editor's `Dump actors` output against `./mgs`'s
`PORT_AUTOLOAD_STAGE=<stage>` log of the same demo. Live game's
`[gcl] chara:` lines list every spawn that fired; if the editor's
list is shorter, the difference is exactly which factories the
live game has that the editor doesn't.

```bash
PORT_AUTOLOAD_STAGE=d00a PORT_GL=1 ./mgs ./ISO/mgs.cue 2>&1 | \
    grep "^\[gcl\] chara:" > /tmp/live_charas.log
# editor: hit Play + Dump actors → stdout, save somewhere
diff /tmp/live_charas.log /tmp/editor_charas.log
```

The diff is the work-list of CHARA_* entries to add to the editor.
