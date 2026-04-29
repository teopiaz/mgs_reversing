# `source/` — codebase reference

Detailed documentation of the decompiled C in
[`source/`](../../../source). Companion to the
[overall port docs](../) — those describe the *port*'s shape (libfs
shims, GL renderer, editor); this folder describes the *original
PSX engine* as it has been recovered.

The disc binary is built from this tree byte-for-byte; the editor
links the same files. So everything documented here applies to both
`./mgs` and `./editor` runtimes.

## Reading order

Start with the overview, then the game loop, then the system you're
working on:

| Doc | Topic |
| --- | --- |
| [01-overview.md](01-overview.md) | Tiers and dependencies — `lib*` engine code vs `game/` policy code vs `chara/` `enemy/` actors. |
| [02-game-loop.md](02-game-loop.md) | `source/game/gamed.c` — the GameWork master actor, status state machine, load handling, pause / vibration / total-time bookkeeping. |
| [03-control-and-motion.md](03-control-and-motion.md) | `source/game/control.c` + `source/game/motion.c` — CONTROL struct, GM_ActControl per-frame collision integration, motion-segment playback for skeletal characters. |

## Per-subsystem (folder-level) docs

Detailed walk-through of each `source/<subsystem>/` directory.
Each subsystem has its own folder under `port/doc/source/<subsystem>/`
with one file per component plus an `_unreversed.md` page tracking
what's still by-address / opaque.

| Subsystem | Status | Components |
| --------- | ------ | ---------- |
| [animal/](animal/) | ✓ deep | [doll](animal/doll.md), [meryl72](animal/meryl72.md), [zako11e](animal/zako11e.md), [zako11f](animal/zako11f.md), [unreversed](animal/_unreversed.md) |
| [anime/](anime/) | ✓ deep | [animconv](anime/animconv.md), [effect](anime/effect.md), [unreversed](anime/_unreversed.md) |
| [bullet/](bullet/) | ✓ deep | [blast](bullet/blast.md), [bakudan](bullet/bakudan.md), [jirai](bullet/jirai.md), [tenage](bullet/tenage.md), [rmissile](bullet/rmissile.md), [amissile](bullet/amissile.md), [unreversed](bullet/_unreversed.md) |
| [chara/](chara/) | ✓ deep | [snake](chara/snake.md), [snake_vr](chara/snake_vr.md), [hind2](chara/hind2.md), [others](chara/others.md), [torture](chara/torture.md), [unreversed](chara/_unreversed.md) |
| [enemy/](enemy/) | ✓ deep | [watcher](enemy/watcher.md), [meryl7](enemy/meryl7.md), [supports](enemy/supports.md), [unreversed](enemy/_unreversed.md) |
| [equip/](equip/) | ✓ overview + opaque | wearables — README + [unreversed](equip/_unreversed.md) |
| [font/](font/) | ✓ overview + opaque | rasteriser + glyph table — README + [unreversed](font/_unreversed.md) |
| [game/](game/) | ✓ deep | [chara](game/chara.md), [camera](game/camera.md), [target](game/target.md), [item](game/item.md), [script](game/script.md), [alert](game/alert.md), [map](game/map.md), [unreversed](game/_unreversed.md). Plus [02-game-loop.md](02-game-loop.md) + [03-control-and-motion.md](03-control-and-motion.md) cross-cutting docs |
| [kojo/](kojo/) | ✓ overview + opaque | streamed cinematics — README + [unreversed](kojo/_unreversed.md) |
| [libdg/](libdg/) | ✓ deep | [pipeline](libdg/pipeline.md), [obj](libdg/obj.md), [matrix](libdg/matrix.md), [text](libdg/text.md), [display](libdg/display.md), [unreversed](libdg/_unreversed.md) |
| [libfs/](libfs/) | ✓ deep | [streaming](libfs/streaming.md), [unreversed](libfs/_unreversed.md) |
| [libgcl/](libgcl/) | ✓ deep | [bytecode](libgcl/bytecode.md), [parse](libgcl/parse.md), [command](libgcl/command.md), [expr](libgcl/expr.md), [unreversed](libgcl/_unreversed.md) |
| [libgv/](libgv/) | ✓ deep | [actor](libgv/actor.md), [memory](libgv/memory.md), [cache](libgv/cache.md), [message](libgv/message.md), [pad](libgv/pad.md), [math](libgv/math.md), [unreversed](libgv/_unreversed.md) |
| [libhzd/](libhzd/) | ✓ deep | [collide](libhzd/collide.md), [level](libhzd/level.md), [zone](libhzd/zone.md), [event](libhzd/event.md), [dynamic](libhzd/dynamic.md), [unreversed](libhzd/_unreversed.md) |
| [libsio/](libsio/) | ✓ overview + opaque | serial port — README + [unreversed](libsio/_unreversed.md) |
| [memcard/](memcard/) | ✓ deep | [savefile](memcard/savefile.md), [unreversed](memcard/_unreversed.md) |
| [menu/](menu/) | ✓ deep | [menuman](menu/menuman.md), [codec](menu/codec.md), [hud](menu/hud.md), [unreversed](menu/_unreversed.md) |
| [mts/](mts/) | ✓ overview + opaque | multi-task scheduler — README + [unreversed](mts/_unreversed.md) |
| [okajima/](okajima/) | ✓ overview + opaque | per-author 3D effects — README + [unreversed](okajima/_unreversed.md) |
| [onoda/](onoda/) | ✓ overview + opaque | option / demosel / preope — README + [unreversed](onoda/_unreversed.md) |
| [sound/](sound/) | ✓ deep | [architecture](sound/architecture.md), [unreversed](sound/_unreversed.md) |
| [stage/](stage/) | ✓ overview + opaque | per-stage overlays — README + [unreversed](stage/_unreversed.md) |
| [takabe/](takabe/) | ✓ deep | [effects](takabe/effects.md), [unreversed](takabe/_unreversed.md) |
| [thing/](thing/) | ✓ deep | [things](thing/things.md), [unreversed](thing/_unreversed.md) |
| [weapon/](weapon/) | ✓ deep | [weapons](weapon/weapons.md), [unreversed](weapon/_unreversed.md) |

**Legend:** *deep* = README + per-component files + `_unreversed.md`. *overview + opaque* = README + `_unreversed.md` (no separate per-component files needed since the folder is small). *partial* = covered indirectly via cross-cutting docs.

Bigger docs (cross-cutting concerns) planned:

| Doc | Topic |
| --- | --- |
| 04-actors-and-charas.md | `source/game/chara.c` + factory tables (MainCharacterEntries / StageCharacterEntries) — how `chara &TYPE` resolves to a constructor, level placement in `gActorsList_800ACC18[]`. |
| 05-camera-system.md | `source/game/camera.c` — GM_Camera, gUnkCameraStruct2, the helpers that update them per frame, DG_LookAt feed. Cross-references the demo docs. |
| 06-script-and-events.md | `source/game/script.c` + `source/libgcl/` — GCL command table, message dispatch, event panel binding. |
| 07-items-and-equipment.md | `source/game/item.c` + `source/equip/` + `source/weapon/` — pickup / hold / use lifecycle. |
| 08-radar-and-alert.md | `source/game/alert.c` + radar drawing — alert state machine, noise tracking, vision cones. |
| 09-snake.md | `source/chara/snake/` — the player actor: state machine, weapons handling, animation. |
| 10-enemy-ai.md | `source/enemy/` — WATCHER / COMMANDER / ZAKO* AI: think → command → action → check pattern. |
| 11-cinematics.md | (cross-link to [doc/demo/](../demo/) — covered there in detail.) |
| 12-rendering.md | `source/libdg/` — channel system, OBJ/MDL/DEF, OT (ordering table), bound/trans/shade pipeline. |
| 13-collision.md | `source/libhzd/` — HZD walls/floors/zones/traps, collision detection inner loop. |
| 14-sound.md | `source/sound/` + `source/mts/` — SPU streaming, song/sequence/wave separation. |
| 15-save-and-config.md | `source/memcard/` + GCL var spaces (`$w:`, `$b:`, `$f:`). |

## Notation

Convention used throughout this doc set:

- `GM_*` — game-tier API (`source/game/`)
- `GV_*` — engine-tier API (`source/libgv/`)
- `DG_*` — render-tier API (`source/libdg/`)
- `HZD_*` — collision-tier API (`source/libhzd/`)
- `FS_*` — filesystem API (`source/libfs/`)
- `mts_*` — multi-task scheduler (`source/mts/`)
- `_800AXXXX` / `_800BXXXX` — original PSX RAM addresses; appear in
  symbol names where the function or global hadn't been renamed yet.
- `STATIC` — a macro used in the codebase for `static` (preserves
  the matching-build behaviour where some statics had to be promoted
  to non-static for cross-TU references during decompilation).

Most data structures live in [`source/include/`](../../../source/include/)
or per-system headers like `source/game/control.h`. When in doubt,
the actual struct definition is authoritative — these docs trail the
code where the code is more current.

## What's in scope

This docset covers `source/`. It deliberately does **not** cover:

- The port itself — see [`port/doc/`](..) for `port/libfs/` shims,
  `port/libdg/gl_renderer.c`, the editor, etc.
- The build pipeline — see [`build/`](../../../build) for the
  PSYQ/Wibo cross-compile setup.
- The asset pipeline — see [`tools/`](../../../tools) for the
  Python tools that produce `.dmo`, `datacnf.bin`, etc.

Where the port modifies original-engine behaviour, look for
`#ifdef PORT_BUILD` blocks in the source — the docs mention
port-only quirks where they're load-bearing.
