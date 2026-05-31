# `source/onoda/` — title, options, stage transitions

Per-author folder named after one of the lead programmers. Everything
under here drives the **non-gameplay scaffolding** of the disc — the
sequence of screens between disc-boot and Snake-spawning, the
post-clear bonus screens, the in-game pause-and-options menu, and
the visual transitions between stages.

Functionally the folder is "everything that happens with the camera
off and the GameWork actor in `STATE_LOAD_STG`".

## Sub-folders

### `preope/` — pre-opening (disc boot → title screen)

| File | Role |
| ---- | ---- |
| [`preope.c`](../../../../source/onoda/preope/preope.c) | Master coordinator. Spawns `PreOpe` actor at boot, runs the title-sequence state machine, hands off to `select` once "Press Start" is pressed. |
| [`pre_met1.c`](../../../../source/onoda/preope/pre_met1.c) | First "MET" (Metal-Gear logo) build-up — geometric wireframe assembly. |
| [`pre_met2.c`](../../../../source/onoda/preope/pre_met2.c) | Second "MET" — solid-shaded version after the wireframe. |

The flow: Konami logo → MET wireframe → MET solid → "TACTICAL
ESPIONAGE ACTION" tagline → press Start prompt. Each substate spawns
the next actor on its own timer; nothing here uses GCL — it's pure
hand-coded state machines because the engine isn't running yet.

### `change/` — inter-stage transitions

| File | Role |
| ---- | ---- |
| [`change.c`](../../../../source/onoda/change/change.c) | Master change-coordinator. Drives the fade-to-black, the loading-screen overlay, and the fade-back-in around `GM_LoadStage`. |
| [`met_logo.c`](../../../../source/onoda/change/met_logo.c) | Spinning MET logo overlay shown during the load (the small one in the corner). |
| [`safety.c`](../../../../source/onoda/change/safety.c) | Safety net — if the stage load fails (missing DATACNF, GCL parse error), this actor catches and routes back to a safe stage instead of crashing. |

The change actor is the *only* thing alive between two stages: when
`GM_LoadStage` is called, every other actor receives `HASH_KILL`,
the engine memory pool is reset, and a single `change.c` actor
remains to draw the loading screen and post a `GV_NewActor` once
the destination stage's data is in memory.

### `demosel/` — post-clear cinematic gallery

| File | Role |
| ---- | ---- |
| [`demosel.c`](../../../../source/onoda/demosel/demosel.c) | The "View cinematics" menu unlocked after first clear — lets players replay each cutscene out of context. |

A list of `(stage_code, demo_id)` pairs that map to the streamed
demos in `DEMO.DAT`. The menu picks a pair and uses the same
`demo -s` GCL path as the in-game cinematics, just from a fixed
script instead of triggered by gameplay.

### `option/` — in-game Option menu

| File | Role |
| ---- | ---- |
| [`opt.c`](../../../../source/onoda/option/opt.c) | The full in-game Option screen — sound (stereo/mono), button config (TYPE A/B/C), language (Japanese/English), vibration on/off, subtitle on/off, radar on/off. |

The screen has six rows the player navigates with up/down, each row
has 2-3 options the player picks with left/right. Selections are
written into `GM_OptionFlag` (`linkvarbuf[2]`) — a 16-bit flag word
defined in `source/include/linkvar.h`:

```
OPTION_BUTTON_TYPE_A       0x0000
OPTION_BUTTON_TYPE_B       0x0001
OPTION_BUTTON_TYPE_C       0x0002
OPTION_BUTTON_MASK         0x0007
OPTION_TUXEDO              0x0020  /* + Red Ninja, Sneaking Meryl */
OPTION_ENGLISH             0x0100  /* 0 = Japanese, 1 = English */
OPTION_VIBRATION_OFF       0x0400
OPTION_RADAR_OFF           0x0800
OPTION_SHUKAN_REVERSE      0x1000  /* "subjective" inverted look */
OPTION_CAPTION_OFF         0x4000
OPTION_SOUND_MONO          0x8000
```

Every game system reads from this flag — `source/menu/jimaku.c`
checks `OPTION_CAPTION_OFF`, `source/sound/sd_main.c` checks
`OPTION_SOUND_MONO`, `source/menu/radio.c` checks `OPTION_ENGLISH`,
`source/libgv/pad.c::GV_ConvertButtonMode` checks
`OPTION_BUTTON_MASK` to remap `△`/`○`/`×`/`□`.

### `s04b/` — small stage-specific actor

| File | Role |
| ---- | ---- |
| [`at.c`](../../../../source/onoda/s04b/at.c) | "AT" — an actor specific to stage s04b. Likely the laser-emitter sensor in that stage's corridor. |

Lives under `onoda/` (rather than `stage/`) because Onoda authored
the stage and the author convention is to keep specialised
gameplay actors in your namespace.

## What ties it together

Every folder here implements one *actor* (small state machine) and a
*coordinator entrypoint*. The pattern is the same:

```
boot or stage-load
       │
       ▼
GV_NewActor(LEVEL, sizeof(Work))     /* coordinator spawn */
       │
       ▼
Act() per frame: advance state machine, render via DG_PutPrim / SPRT
       │
       ▼
on terminal state: GM_LoadStage(next) OR HASH_KILL siblings
```

There's no GCL inside any of these files — they predate the stages
that GCL controls. Onoda's code is the "frame outside the painting":
title screen, transitions, post-clear extras. The PSX engine runs it
between stages, not as part of them.

## Pitfalls

- The pre-opening state machine is hard-coded with frame counts.
  Skipping the Konami logo on the port works by `PORT_SKIP_MENU=1`
  (which bypasses the pre-game menu, not this), but skipping the
  pre-ope requires sending `BTN_START` early or seeking past the
  state's wait counter.
- `change.c::safety.c` is the *last* line of defence on stage load.
  If you're seeing the "safety net" stage repeatedly, the cause is
  usually a missing KMD or a NULL `_StageCharacterEntries` rather
  than a bug in safety itself.
- The Option screen reads `GM_OptionFlag` but doesn't *own* it —
  loading a save (via `source/menu/datasave.c`) overrides the flag
  with the save's value. So changing language mid-game and then
  loading a save can flip the language back.

## See also

- [_unreversed.md](_unreversed.md) — preope state machine details,
  demosel cinematic table, exact option-screen layout coordinates.
- [`source/game/loader.c`](../../../../source/game/loader.c) — the
  stage loader that `change.c` wraps with the visual transition.
- [`source/include/linkvar.h`](../../../../source/include/linkvar.h) —
  `GM_OptionFlag` bit definitions.
- [`source/libgv/pad.c`](../../../../source/libgv/pad.c) —
  `GV_ConvertButtonMode` is the most-load-bearing reader of
  `OPTION_BUTTON_*`.

---

## Port notes

The port runs the pre-opening sequence unmodified, but with one
practical caveat: `PORT_SKIP_MENU=1` (or any of the automation env
vars) skips the **port's pre-game menu**, not the PSX pre-opening
sequence. To skip the latter, hit Start after the Konami logo as
usual — there's no env var for it.

The port's pre-game menu's **Language** radio button writes
`OPTION_ENGLISH` into `GM_OptionFlag` (`linkvarbuf[2]`) before
`game_init` runs, so the title screen comes up in the chosen
language. The in-game Option screen (`opt.c`) still has the same
toggle, and a save-file load still overrides at runtime — same as
PSX. See [`09-input.md`](../../09-input.md) for the
`OPTION_BUTTON_TYPE_*` remapping and [`02-port-architecture.md`'s
"Pre-Game Menu" section](../../02-port-architecture.md) for the
overall flow.
