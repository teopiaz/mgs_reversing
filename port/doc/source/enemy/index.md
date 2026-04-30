# `source/enemy/` — generic guard AI + per-encounter actors

The shared, reusable enemy-actor library. Where `animal/zako11e/`
and `animal/zako11f/` are stage-specific guard variants,
**`enemy/`** is the *base* AI used everywhere else: regular MGS
patrol guards, watchers, the COMMANDER, plus a host of small
support actors (footstep markers, smoke, lasers, search-lights).

Total: 20393 lines / 30+ files. Fully decompiled.

## High-level architecture

The core enemy actor — `WATCHER` (`watcher.c`) — uses a four-stage
per-tick pipeline:

```
   ┌─ think.c ──────────────────────────────┐
   │  decide what action this guard should  │
   │  be in based on world state            │
   └────────────────┬───────────────────────┘
                    ▼
   ┌─ command.c ────────────────────────────┐
   │  combat helpers — fire bullet, throw   │
   │  grenade, request backup, etc.         │
   └────────────────┬───────────────────────┘
                    ▼
   ┌─ action.c ─────────────────────────────┐
   │  state callbacks — one fn per state    │
   │  (idle / patrol / aim / fire / hit /   │
   │  knockdown / dying / …)                │
   └────────────────┬───────────────────────┘
                    ▼
   ┌─ check.c ──────────────────────────────┐
   │  per-frame condition tests — see       │
   │  player? heard noise? took damage?     │
   └────────────────────────────────────────┘
```

Every animal/zako*/ subfolder copies + mutates this pattern.

## File map

### Core enemy framework

| File | Lines | Role |
| ---- | ----- | ---- |
| [`watcher.c`](../../../../source/enemy/watcher.c) | 716 | Generic watching guard — patrol, alert, engage. The most-spawned enemy hash. |
| [`enemy.c`](../../../../source/enemy/enemy.c) | ~? | Cross-cutting helpers used by all enemy types — TARGET registration, comm-state init, shared init. |
| [`think.c`](../../../../source/enemy/think.c) | ~? | High-level decision-making for watchers. |
| [`command.c`](../../../../source/enemy/command.c) | ~? | Combat / dispatch helpers — bullet spawn, voice, alarm propagation. |
| [`action.c`](../../../../source/enemy/action.c) | ~? | Action-state callbacks for watchers. |
| [`check.c`](../../../../source/enemy/check.c) | ~? | Per-frame condition tests. |

### Specialised guard variants

| File | Description |
| ---- | ----------- |
| `meryl7.c`, `merylaction.c`, `merylcheck.c`, `merylenemy.c`, `merylthink.c` | Meryl as an *enemy* in s07 (before she's revealed). Same think/check/action split. Different file from `animal/meryl72/` because the *enemy* version had different AI tuning. |
| `katana.c` | Katana / sword guard variant. |
| `searchli.c` | Searchlight guard (the spotlight tower). |

### Support actors

| File | Description |
| ---- | ----------- |
| `asiato.c` / `asiato2.c` / `asioto.c` | Footstep marker actors (Asiato = 足音, footstep sound). Asioto might be a typo / alternate spelling. |
| `boxkeri.c` | "Box-kick" reaction — when a guard kicks a cardboard box Snake's hiding under. |
| `camera.c` | The security-camera enemy (the wall-mounted swiveling cam). |
| `demoasi.c` / `demokage.c` | Cinematic-only footstep / shadow effects. |
| `dymc_seg.c` | "Dynamic segment" — possibly procedural geometry segment for HZD walls. |
| `eyeflash.c` | The flash effect when a guard *spots* Snake (the alert-icon at the eye). |
| `glight.c` | Gun-tip light (alternative implementation to `chara/snake/`'s gunlight). |
| `grnad_e.c` | Enemy grenade-throw action (Grenade-E for Enemy). |
| `kiken.c` | "Danger" indicator (Kiken = 危険, danger). |
| `l_sight.c` | Laser sight beam. |
| `object.c` | Generic destructible object base. |
| `smoke.c` | Enemy-side smoke effect (separate from `anime/effect/smoke.c`). |
| `wall.c` | The `WALL` chara — static-prop placeholder cinematic stages spawn. |

### Headers

`enemy.h`, `command.h`, `meryl.h`, etc. — type definitions (the
shared `WatcherWork` / `ZakoWork` structs live in
[`enemy.h`](../../../../source/enemy/enemy.h)).

## Components

- [watcher.md](watcher.md) — the generic guard. The core
  document; everything else parallels this.
- [meryl7.md](meryl7.md) — Meryl as an enemy.
- [supports.md](supports.md) — the small support actors
  catalogued.
- [_unreversed.md](_unreversed.md) — opaque areas.

## Used by

- Every gameplay stage spawns watchers via GCL `chara &WATCHER
  ...`.
- `animal/zako11e/`, `animal/zako11f/` are derived (think:
  copy-paste-and-modify) from this base.
- The cinematic system uses the `WALL` chara (`enemy/wall.c`)
  to spawn placeholder geometry actors.

## See also

- [`source/animal/zako11e/`](../animal/zako11e.md),
  [`source/animal/zako11f/`](../animal/zako11f.md) — derived
  variants.
- [`source/chara/snake/`](snake.md) — the player; uses the same
  motion / control primitives.
- [`source/game/control.c`](../../../../source/game/control.c) +
  [`motion.c`](../../../../source/game/motion.c) — the
  CONTROL/MOTION_CONTROL primitives every enemy uses.
- [03-control-and-motion.md](../03-control-and-motion.md) — the
  engine layer.
