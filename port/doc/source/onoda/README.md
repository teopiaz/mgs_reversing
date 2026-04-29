# `source/onoda/` — Onoda's menu / option / pre-game code

Per-author folder. Authored by **Onoda**. Three sub-folders, all
related to *non-gameplay* state machines (option screens, demo
selector, pre-opening sequence).

## Sub-folders

### `change/`
Stage-change handling — the visual transition between stages.

| File | Role |
| ---- | ---- |
| `change.c` | Master change-coordinator. |
| `met_logo.c` | "MET" / Metal Gear logo overlay during stage-change. |
| `safety.c` | Safety net for stage transitions (load-error fallback). |

### `demosel/`
Demo selector — the menu accessible after game completion that
lets players replay cinematics.

| File | Role |
| ---- | ---- |
| `demosel.c` | The demo-picker UI + dispatch. |

### `preope/`
Pre-opening sequence — the screens that play before the game's
title sequence.

| File | Role |
| ---- | ---- |
| `preope.c` | Master pre-opening coordinator. |
| `pre_met1.c` | First "MET" logo screen. |
| `pre_met2.c` | Second "MET" logo screen. |

## What you'd expect

These files are mostly:

1. A small actor with a state machine (intro → display → outro).
2. Sprite / fade rendering via `DG_PutPrim` or HUD-channel
   primitives.
3. Eventual transition to the next stage via `GM_LoadStage`.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (preope state
  machine, option screen layout, demosel filter logic).
- `source/stage/preope.c` — the stage-tier wrapper that includes
  this folder.
- `source/stage/option.c` — the option-menu stage.
