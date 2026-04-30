# The game loop — `gamed.c::GameWork`

The single actor that orchestrates every frame of MGS. It boots the
engine, accepts stage-load requests, runs cutscene scripts, ticks
gameplay, handles pause / GameOver / system callbacks, and drives the
title-menu → in-game state transitions.

If you change anything about how stages start, end, or transition,
this is the file. If you've just spawned a new actor and it never
runs, the cause is probably one of the gates `Act()` checks each tick.

Source: [`source/game/gamed.c`](../../../source/game/gamed.c)
(917 lines), with the struct in
[`source/game/game.h`](../../../source/game/game.h).

## The actor

```c
typedef struct gameWork     // private to gamed.c
{
    GV_ACT  actor;
    int     status;         // enum GAMED_STATE: WAIT_LOAD or WORKING
    int     killing_count;  // teardown countdown (3 ticks of cleanup)
} gameWork;

extern gameWork GameWork;   // single global; not in a list
```

`GameWork` is **statically allocated**, not part of the actor list
walked by `GV_ExecActorSystem`. It's spawned by `GM_StartDaemon` —
called once during boot from `main.c` — and ticked manually by
`main_game.c`'s main loop:

```
title screen → memcard → stage select → GameWork.status = WAIT_LOAD
                                                ↓
                                     LoadReq fires → wait for FS
                                                ↓
                                       GM_LoadComplete = 1
                                                ↓
                              GM_ResetMap, NewCameraSystem, GCL_ExecScript
                                                ↓
                                         status = WORKING
                                                ↓
                              ⟲ pause / vibration / alert / pad poll
                                                ↓
                              eventually: GM_LoadRequest != 0
                                                ↓
                                  killing_count = 3 (drain)
                                                ↓
                              status back to WAIT_LOAD with new stage
```

## Status enum

Two values; the entire state machine is built on them:

| Value | Meaning |
| --- | --- |
| `WAIT_LOAD` (0) | A stage load is in flight — `FS_LoadStageRequest` was issued and `GM_LoadComplete` hasn't flipped to 1 yet. The actor returns early without ticking gameplay. |
| `WORKING` (1) | Gameplay is live — pad input, pause check, alert AI, vibration all tick. |

Transitions:

- `WAIT_LOAD → WORKING`: occurs when `GM_LoadComplete == 1`. Pre-flight
  runs `GM_ResetMap`, `NewCameraSystem`, `GCL_ExecScript`,
  `MENU_ResetTexture`, `GM_AlertModeReset`, `GM_SoundStart`. Then
  `status = WORKING`.
- `WORKING → WAIT_LOAD`: triggered by `GM_LoadRequest != 0`. Sets
  `STATE_PADRELEASE | STATE_ALL_OFF` to silence the player, destroys
  level-4 actors, primes `killing_count = 3` for a 3-tick drain,
  then on `--killing_count <= 0` resets memory, `GM_ActInit(work)`,
  spawns a fresh loader and the cycle restarts.

## Per-tick `Act()` flow

Reading [`gamed.c:322-632`](../../../source/game/gamed.c#L322), the
actor body runs in this order every frame:

```c
static void Act(gameWork *work)
{
    /* 1. Pad vibration — translate GM_PadVibration / GM_PadVibration2
     *    into mts_set_pad_vibration calls. Cleared every tick. */
    pad = mts_read_pad(1);
    update_vibration_from_options();

    /* 2. CD read-error overlay — toggles GV_PauseLevel bit 3 based on
     *    str_mute_fg / CDBIOS_TaskState() == 3. Disc-only; the port's
     *    libfs never sets these. */
    handle_cd_read_error_pause();

    /* 3. Total play time — increment by GV_PassageTime if not paused.
     *    Becomes the visible "TOTAL TIME" on the save / GameOver UI. */
    if ((GV_PauseLevel & 2) == 0) {
        gTotalFrameTime += GV_PassageTime;
        GM_TotalHours   = gTotalFrameTime / (60 * 3600);
        GM_TotalSeconds = (gTotalFrameTime / 60) % 3600;
    }

    /* 4. Status dispatch. */
    if (work->status == WAIT_LOAD) {
        if (!GM_LoadComplete) return;
        run_post_load_setup(work);    // GM_ResetMap, NewCameraSystem,
                                      // GCL_ExecScript, MENU_ResetTexture
        work->status = WORKING;
        return;
    }
    if (work->status != WORKING) return;

    /* 5. Tear-down countdown (between stages). */
    if (work->killing_count > 0) {
        run_killing_count_drain(work);
        return;
    }

    /* 6. GameOver? */
    if (GM_GameOverTimer != 0) {
        run_game_over_state(work);
        return;
    }

    /* 7. Stage-change request? */
    if (GM_LoadRequest != 0 && (GV_PauseLevel & 2) == 0) {
        begin_stage_unload(work);   // sets killing_count=3, returns
        return;
    }

    /* 8. Alert state machine + pause toggle + reset combo + noise
     *    decay. The bulk of the gameplay tick happens here. */
    GM_AlertAct();                  // (when GV_PauseLevel == 0)
    handle_pause_button();          // PAD_START → GM_TogglePauseScreen
    handle_reset_combo();           // L1+L2+R1+R2+SELECT+START
    handle_noise_decay();
}
```

The two return-after-status branches at the top are the most
load-bearing: they suppress every gameplay-relevant action while a
load is pending or a teardown is draining. If you're trying to
spawn an actor "between stages", it'll never run.

## `GM_LoadRequest` — stage transition flags

A 32-bit field set by `GM_LoadStage(name, flags)` to request a stage
change. Bits encode the *kind* of transition:

| Bit | Meaning |
| --- | --- |
| `0x01` | `GM_LoadStage` was called (this bit is the trigger; everything else is options) |
| `0x10` | Save GCL var space before unload (`GCL_SaveVar`) |
| `0x20` | After load, run a specific GCL proc id instead of `GCL_ExecScript`. The proc id is in bits 16..31. |
| `0x40` | Don't reset memory / re-spawn loader (warm transition between two demos) |
| `0x80` | Hide rendering during the transition (`DG_UnDrawFrameCount = 0x7FFF0000`) |

After the transition, `GM_LoadRequest` is cleared back to 0.

## `GM_GameStatus` — the everything bitmask

Big global flag word read by every actor. Bits in
[`game.h:146-178`](../../../source/game/game.h#L146); the gameplay-
critical ones:

| Bit | Name | Effect |
| --- | --- | --- |
| `0x00000001` | `STATE_CHAFF` | Chaff grenade active — guards lose radar / camera vision |
| `0x00000002` | `STATE_STUN` | Stun grenade — guards stunned |
| `0x00000004` | `STATE_NVG` | Night-vision goggles equipped |
| `0x00000008` | `STATE_THERMG` | Thermal goggles equipped |
| `0x00000010` | `STATE_BEHIND_CAMERA` | First-person/photo mode |
| `0x00000020` | `STATE_VOX_STREAM` | Voice playback in progress |
| `0x00000200` | `STATE_ENEMY_OFF` | Disable all enemy think |
| `0x00000400` | `STATE_TAKING_PHOTO` | JPEG-cam photo capture |
| `0x00002000` | `STATE_RADIO_OFF` | Hide / disable codec |
| `0x00004000` | `STATE_PAUSE_OFF` | Block pause menu |
| `0x00020000` | `STATE_LIFEBAR_OFF` | Hide health/stamina bars |
| `0x00080000` | `STATE_MENU_OFF` | Block in-game menu |
| `0x00400000` | `STATE_RADAR_OFF` | Hide radar |
| `0x00800000` | `STATE_JAMMING` | Radar jammed (story flag) |
| `0x02000000` | `STATE_DAMAGED` | Snake taking damage |
| `0x04000000` | `STATE_GAME_OVER` | GameOver display path active |
| `0x08000000` | `STATE_PADMASK` | Pad input goes through `GV_PadMask` first |
| `0x10000000` | `STATE_PADRELEASE` | Pad input ignored entirely |
| `0x20000000` | `STATE_NOSLOW` | Disable slow-motion |
| `0x40000000` | `STATE_PADDEMO` | Pre-baked pad input from CINEMA |
| `0x80000000` | `STATE_DEMO` | Cutscene mode active |

Two convenience composites:

```c
#define STATE_ALL_OFF    (STATE_RADAR_OFF | STATE_MENU_OFF |
                          STATE_LIFEBAR_OFF | STATE_PAUSE_OFF |
                          STATE_RADIO_OFF)        // 0x4A6000
#define STATE_PAUSE_ONLY (STATE_RADAR_OFF | STATE_MENU_OFF |
                          STATE_LIFEBAR_OFF | STATE_RADIO_OFF)  // 0x4A2000
```

`STATE_DEMO | STATE_PADRELEASE | STATE_ALL_OFF` is what gets set when
a cutscene starts; clearing them is what `GM_StartGame` does to
return to gameplay.

The high bit (`STATE_DEMO`) makes `GM_GameStatus` a *signed* negative
when set. `camera.c`'s `Act()` gates on `GM_GameStatus >= 0` — so a
cutscene with `STATE_DEMO` flipped on means camera.c skips
`DG_LookAt` entirely. (The cinematic path writes the matrix
directly via `FrameRunDemo` instead — see
[doc/demo/04-camera-pipeline.md](../demo/04-camera-pipeline.md).)

## `GV_PauseLevel` — pause / freeze bitmask

A second flag word, at the engine tier (`source/libgv/`), holding
*pause-level* state. Lower-cost than `GM_GameStatus` because actors
gate on it without crossing the game/engine boundary.

| Bit | Set by | Effect |
| --- | --- | --- |
| `0x01` | manual freeze (debug) | suppress GV_ExecActorSystem some levels |
| `0x02` | menu pause / GameOver | suppress most gameplay actors |
| `0x04` | (?) | (rarely used) |
| `0x08` | CD read error overlay | gamed.c sets this when `CDBIOS_TaskState() == 3` |

`GV_PauseLevel == 0` is the gameplay-running condition. Most actors
check that (or `& 2`) before running their work.

## `GM_LoadComplete` — handshake bit

Set by the `Loader` actor (`source/game/loader.c`) when the stage's
DATACNF + 'r' archive + 'g' bytecode have all been pulled into RAM.
Read by `GameWork.Act` in the `WAIT_LOAD → WORKING` transition.

When 1, gamed.c also clears `FS_ResidentCacheDirty` (after saving
the cache snapshot) so subsequent stage loads can detect what's new.

## Init path (`GM_StartDaemon`)

[`gamed.c:894-917`](../../../source/game/gamed.c#L894) — called once
from `main.c`. Allocates `GameWork`, wires `Act` and `Init`, calls
the various engine init helpers in this order:

1. `GCL_StartDaemon()` — register the `'g'` cache loader.
2. `GM_InitArea()` — area / region tracking.
3. `GM_InitChara()` — chara hash → factory table
   (`MainCharacterEntries[]`).
4. `GM_InitScript()` — register every `mesg`/`chara`/`light`/etc.
   GCL command.
5. `GM_InitWhereSystem()` — control-point bookkeeping.
6. `GM_InitNoise()` — noise/alert decay state.
7. `GameWork.killing_count = 0`, `status = 0`.
8. `GV_NewActor` for the daemon, `GV_SetNamedActor(work, Act, NULL,
   "gamed.c")`.
9. The first `LoadReq` for the title screen is queued — title
   actually loads stage `init` first (the boot loader), which then
   chains to `select`, `title`, etc.

Out-of-order calls cause a wave of NULL-derefs. The editor's Demo
Player skips `GM_StartDaemon` entirely (would also start the title
state machine which we don't want) and calls items 1, 3, 4
manually — see
[`port/editor/ed_demo.c::ed_demo_engine_init`](../../../port/editor/ed_demo.c).

## System callbacks

The engine exposes 8 numbered callback slots
(`GM_SystemCallbackProc[0..7]`). Game code registers GCL proc ids
into these via `GM_SetSystemCallbackProc(idx, proc)`, and
`GM_CallSystemCallbackProc(idx, arg)` invokes the registered proc
with one argument.

Callback `4` is special: before running the proc, gamed.c
re-fires the player's HZD trap event (`HZD_ReExecEvent` with
event-mask `0x301`) — used for the
"player damage / death drives a script handler" flow.

## Reset combo

L1+L2+R1+R2+SELECT+START held for ~30 ticks resets the game to the
title screen. Implemented as a state machine in `Act()`:

- `RESET_COMBO` mask = combined buttons.
- `RESET_DELAY` = 30 ticks.
- Each tick the combo is *fully* held, decrement a counter; when
  it hits 0, `GM_LoadStage` is called with the title stage.
- Releasing any button resets the counter.

The counter and the constants are local to `Act` so external code
can't influence the timing.

## Pause path

Pressing PAD_START while gameplay is "free" (no demo, no padmask,
no alert overlap) calls `GM_TogglePauseScreen`. The flow:

1. `GV_PauseLevel |= 2` — every actor that gates on that freezes.
2. `MENU_StartPause` opens the pause-menu actor (separate file).
3. The menu actor consumes input until the user picks "continue"
   or "quit". On continue, `GV_PauseLevel &= ~2`.

GameOver runs through a similar path but at a higher priority —
`GM_GameOverTimer` non-zero short-circuits the regular `Act` flow
into the GameOver subroutine (line ~456-481).

## See also

- [03-control-and-motion.md](03-control-and-motion.md) — the
  CONTROL primitive that gameplay actors run on top of `GameWork`.
- [`source/game/`](game/index.md) — the rest of the policy layer.
- [`source/libgv/actor.md`](libgv/actor.md) — the actor system
  `GameWork` is registered into.
- [`source/libgcl/`](libgcl/index.md) — the bytecode interpreter
  driving `GCL_ExecScript`.
- [doc/demo/02-data-flow.md](../demo/02-data-flow.md) — the
  cutscene-specific subset of the loop.

---

## Port notes

The disc binary's gamed.c relies on PSX-specific timing
(`mts_wait_vbl`, CD interrupt firing under SDL). The port's main
loop in [`port/main_game.c`](../../../port/main_game.c) drives
`GameWork.Act` once per render frame and approximates `GV_PassageTime`
based on real elapsed time.

Two PORT_BUILD blocks live inside gamed.c itself:

- Line ~445: after `GCL_ExecScript` returns from a stage's GCL
  init, the port clears `STATE_PADRELEASE | STATE_ALL_OFF`. On disc,
  the GCL `pad -s` directive cleared this naturally; some scripts
  on the port skip cutscene procs and don't, so we do it manually.
- Various debug printfs gated by `PORT_BUILD_VERBOSE` so the
  default port output isn't drowned in trace.
