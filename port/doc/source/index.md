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
| [animal/](animal/index.md) | ✓ deep | [doll](animal/doll.md), [meryl72](animal/meryl72.md), [zako11e](animal/zako11e.md), [zako11f](animal/zako11f.md), [unreversed](animal/_unreversed.md) |
| [anime/](anime/index.md) | ✓ deep | [animconv](anime/animconv.md), [effect](anime/effect.md), [unreversed](anime/_unreversed.md) |
| [bullet/](bullet/index.md) | ✓ deep | [blast](bullet/blast.md), [bakudan](bullet/bakudan.md), [jirai](bullet/jirai.md), [tenage](bullet/tenage.md), [rmissile](bullet/rmissile.md), [amissile](bullet/amissile.md), [unreversed](bullet/_unreversed.md) |
| [chara/](chara/index.md) | ✓ deep | [snake](chara/snake.md), [snake_vr](chara/snake_vr.md), [hind2](chara/hind2.md), [others](chara/others.md), [torture](chara/torture.md), [unreversed](chara/_unreversed.md) |
| [enemy/](enemy/index.md) | ✓ deep | [watcher](enemy/watcher.md), [meryl7](enemy/meryl7.md), [supports](enemy/supports.md), [unreversed](enemy/_unreversed.md) |
| [equip/](equip/index.md) | ✓ deep | gas mask, scope, NVG, cardboard box, Stealth, JPEG camera — README walks the lifecycle pattern + [unreversed](equip/_unreversed.md) |
| [font/](font/index.md) | ✓ deep | rasteriser + KCB handle + multi-font system — README + [unreversed](font/_unreversed.md) |
| [game/](game/index.md) | ✓ deep | [chara](game/chara.md), [camera](game/camera.md), [target](game/target.md), [item](game/item.md), [script](game/script.md), [alert](game/alert.md), [map](game/map.md), [unreversed](game/_unreversed.md). Plus [02-game-loop.md](02-game-loop.md) + [03-control-and-motion.md](03-control-and-motion.md) cross-cutting docs |
| [kojo/](kojo/index.md) | ✓ overview + opaque | streamed cinematics — README + [unreversed](kojo/_unreversed.md) |
| [libdg/](libdg/index.md) | ✓ deep | [pipeline](libdg/pipeline.md), [obj](libdg/obj.md), [matrix](libdg/matrix.md), [text](libdg/text.md), [display](libdg/display.md), [unreversed](libdg/_unreversed.md) |
| [libfs/](libfs/index.md) | ✓ deep | [streaming](libfs/streaming.md), [unreversed](libfs/_unreversed.md) |
| [libgcl/](libgcl/index.md) | ✓ deep | [bytecode](libgcl/bytecode.md), [parse](libgcl/parse.md), [command](libgcl/command.md), [expr](libgcl/expr.md), [unreversed](libgcl/_unreversed.md) |
| [libgv/](libgv/index.md) | ✓ deep | [actor](libgv/actor.md), [memory](libgv/memory.md), [cache](libgv/cache.md), [message](libgv/message.md), [pad](libgv/pad.md), [math](libgv/math.md), [unreversed](libgv/_unreversed.md) |
| [libhzd/](libhzd/index.md) | ✓ deep | [collide](libhzd/collide.md), [level](libhzd/level.md), [zone](libhzd/zone.md), [event](libhzd/event.md), [dynamic](libhzd/dynamic.md), [unreversed](libhzd/_unreversed.md) |
| [libsio/](libsio/index.md) | ✓ overview + opaque | serial port — README + [unreversed](libsio/_unreversed.md) |
| [memcard/](memcard/index.md) | ✓ deep | [savefile](memcard/savefile.md), [unreversed](memcard/_unreversed.md) |
| [menu/](menu/index.md) | ✓ deep | [menuman](menu/menuman.md), [codec](menu/codec.md), [hud](menu/hud.md), [unreversed](menu/_unreversed.md) |
| [mts/](mts/index.md) | ✓ deep | multi-task scheduler — tasks, messages, semaphores, V-blank sync, per-slot map + [unreversed](mts/_unreversed.md) |
| [okajima/](okajima/index.md) | ✓ overview + opaque | per-author 3D effects — README + [unreversed](okajima/_unreversed.md) |
| [onoda/](onoda/index.md) | ✓ deep | title / preope / change / demosel / option — README walks each sub-folder + [unreversed](onoda/_unreversed.md) |
| [sound/](sound/index.md) | ✓ deep | [architecture](sound/architecture.md), [unreversed](sound/_unreversed.md) |
| [stage/](stage/index.md) | ✓ deep | per-stage chara-tables, naming conventions, when overlays kick in — README + [unreversed](stage/_unreversed.md) |
| [takabe/](takabe/index.md) | ✓ deep | [effects](takabe/effects.md), [unreversed](takabe/_unreversed.md) |
| [thing/](thing/index.md) | ✓ deep | [things](thing/things.md), [unreversed](thing/_unreversed.md) |
| [weapon/](weapon/index.md) | ✓ deep | [weapons](weapon/weapons.md), [unreversed](weapon/_unreversed.md) |

**Legend:** *deep* = `index.md` + per-component files + `_unreversed.md`. *overview + opaque* = `index.md` + `_unreversed.md` (no separate per-component files needed since the folder is small). *partial* = covered indirectly via cross-cutting docs.

## Document conventions

Every doc in this tree follows the same layout:

1. **Frontmatter** — `file: source/<path>.c` (or `+ another.c`) on
   per-component pages. Index pages and the `_unreversed.md` pages
   omit frontmatter.
2. **Sections** describing the original PSX engine.
3. **Pitfalls** — gotchas the doc has to call out.
4. **See also** — cross-references to neighbouring docs.
5. *(horizontal rule)* — a `---` visually separates port material.
6. **Port notes** — anything specific to the macOS / GL port. A
   reader who only cares about the original engine can stop at the
   horizontal rule.

Folder index pages are named `index.md` (not `README.md`). Linking
to a folder always goes through the explicit `index.md` filename.

## Cross-cutting topics

Topics that span multiple subsystems — start at the listed entry
point.

| Topic | Entry point |
| ----- | ----------- |
| Actor system | [libgv/actor.md](libgv/actor.md) → [game/chara.md](game/chara.md) → [game/script.md](game/script.md) |
| Camera + cinematics | [game/camera.md](game/camera.md) → [doc/demo/](../demo/) |
| Scripting (GCL) | [libgcl/bytecode.md](libgcl/bytecode.md) → [libgcl/command.md](libgcl/command.md) → [game/script.md](game/script.md) |
| Items + weapons | [game/item.md](game/item.md) → [equip/index.md](equip/index.md) → [weapon/weapons.md](weapon/weapons.md) |
| Alert / radar | [game/alert.md](game/alert.md) → [menu/hud.md](menu/hud.md) → [enemy/watcher.md](enemy/watcher.md) |
| Player | [chara/snake.md](chara/snake.md) |
| Enemy AI | [enemy/watcher.md](enemy/watcher.md) → [enemy/supports.md](enemy/supports.md) |
| Rendering | [libdg/pipeline.md](libdg/pipeline.md) → [libdg/obj.md](libdg/obj.md) → [libdg/text.md](libdg/text.md) |
| Collision | [libhzd/collide.md](libhzd/collide.md) → [libhzd/zone.md](libhzd/zone.md) → [libhzd/event.md](libhzd/event.md) |
| Sound | [sound/architecture.md](sound/architecture.md) → [menu/codec.md](menu/codec.md) |
| Save / load | [memcard/savefile.md](memcard/savefile.md) → [libgcl/expr.md](libgcl/expr.md) |

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
