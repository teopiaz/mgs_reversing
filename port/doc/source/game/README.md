---
file: source/game/
---

# `source/game/` — game policy layer

Where the engine primitives in `lib*/` become *gameplay*. This is
the highest tier of always-loaded code — every stage relies on it.

```
   source/lib*/        engine primitives  (actor, OBJS, HZD, GCL)
   source/game/        gameplay policy    ← THIS FOLDER
   source/chara/       player + named characters
   source/enemy/       generic guard AI
   source/thing/       non-character actors
   ...                 per-author / per-system specials
```

## Files (by role)

### Top-level master actor

| File | Lines | Role |
| ---- | ----- | ---- |
| [gamed.c](../../../../source/game/gamed.c) | 917 | The `GameWork` master actor — status state machine, load handling, pause / vibration / total-time bookkeeping. See [02-game-loop.md](../02-game-loop.md). |

### Player + character integration

| File | Lines | Role |
| ---- | ----- | ---- |
| [control.c](../../../../source/game/control.c) | 560 | `CONTROL` struct + `GM_ActControl` collision-integration loop. See [03-control-and-motion.md](../03-control-and-motion.md). |
| [motion.c](../../../../source/game/motion.c) | 1254 | `MOTION_CONTROL` skeletal animation playback. |
| [chara.md](chara.md) | 75 | Chara-factory dispatch — GCL `chara &NAME` → constructor lookup. |
| [target.md](target.md) | 726 | TARGET hit-system — bullets, damage, faint, knockback. |
| [homing.c](../../../../source/game/homing.c) | 159 | Homing-target allocation (eye-flash/laser-sight pivots). |

### Stage / scene management

| File | Lines | Role |
| ---- | ----- | ---- |
| [map.md](map.md) | 516 | `GM_CurrentMap`, multi-area maps, sub-area HZD swapping. |
| [area.c](../../../../source/game/area.c) | – | Area-bound dispatching (between map-zones). |
| [point.c](../../../../source/game/point.c) | – | Named-point lookup (positions / spawn points). |
| [loader.c](../../../../source/game/loader.c) | – | Stage loader helper — orchestrates KMD/HZD/PCX/GCL loads. |
| [select.c](../../../../source/game/select.c) | 191 | Stage-select state (used by demosel / preope). |

### Camera + cinematics + UI

| File | Lines | Role |
| ---- | ----- | ---- |
| [camera.md](camera.md) | 1238 | The single biggest game-tier file: GM_Camera, camera-zone transitions, DG_LookAt feed. |
| [movie.c](../../../../source/game/movie.c) | 455 | Live-action FMV playback wrapper (used in opening/cinematics). |
| [over.c](../../../../source/game/over.c) | 636 | Game-over / continue / death sequence. |
| [strctrl.c](../../../../source/game/strctrl.c) | 281 | "Strange Control" — special control mode (rope / climb / gun-prone). |

### Game systems

| File | Lines | Role |
| ---- | ----- | ---- |
| [item.md](item.md) | 912 | Pickup / equip / use lifecycle (rations, FAMAS, key cards). |
| [script.md](script.md) | 1185 | GCL command set — registers `mesg`, `chara`, `delay`, `pad`, `vox`, `light`, etc. |
| [alert.md](alert.md) | 250 | Alert state machine — caution / alert / evasion timers + radar binding. |
| [evpanel.c](../../../../source/game/evpanel.c) | 1048 | Event-panel dispatcher — binds GCL events to per-actor handlers. |
| [sound.c](../../../../source/game/sound.c) | 467 | Game-side sound dispatcher (calls into sd_main + sequence). |
| [pad.c](../../../../source/game/pad.c) | – | Game-side pad reading + button-mode + cinematic pad-mask. |
| [vibrate.c](../../../../source/game/vibrate.c) | 136 | DualShock vibration controller — `GM_Vibrate` queue. |

### Misc utilities

| File | Lines | Role |
| ---- | ----- | ---- |
| [object.c](../../../../source/game/object.c) | 272 | Generic OBJECT helper (renderable wrapper around DG_OBJS). |
| [delay.c](../../../../source/game/delay.c) | 188 | Delayed-execution actor — fire callback after N frames. |
| [cancel.c](../../../../source/game/cancel.c) | 121 | `GM_CancelChara` — destroy all chars matching a hash. |
| [jimctrl.c](../../../../source/game/jimctrl.c) | 460 | JIMAKU (subtitle) controller — text reveal, font binding. |
| [lamp.c](../../../../source/game/lamp.c) | 390 | Lamp / point-light system used during cinematics. |
| [tobcnt.c](../../../../source/game/tobcnt.c) | 627 | "Tob count" — track / counter actor (cinematic timing?). |
| [second.c](../../../../source/game/second.c) | – | Second-pass actor (post-physics fixups). |
| [sndtst.c](../../../../source/game/sndtst.c) | 198 | Sound-test debug menu actor. |

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [chara.md](chara.md) | The chara factory + GCL spawning |
| [camera.md](camera.md) | GM_Camera + zone transitions + DG_LookAt feed |
| [target.md](target.md) | TARGET hit system — bullets, damage, faint |
| [item.md](item.md) | Inventory + pickup + equip + use |
| [script.md](script.md) | GCL command registration (the policy command set) |
| [alert.md](alert.md) | Alert state machine + radar |
| [map.md](map.md) | Multi-area maps and HZD sub-area binding |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [01-overview.md](../01-overview.md) — tier system.
- [02-game-loop.md](../02-game-loop.md) — `gamed.c` deep dive.
- [03-control-and-motion.md](../03-control-and-motion.md) — control
  + motion helpers.
- [`source/include/game.h`](../../../../source/game/game.h) — full
  public API.
