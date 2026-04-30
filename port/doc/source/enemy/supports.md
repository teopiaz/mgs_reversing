---
file: source/enemy/{asiato,asiato2,asioto,boxkeri,camera,demoasi,demokage,dymc_seg,eyeflash,glight,grnad_e,katana,kiken,l_sight,object,searchli,smoke,wall}.c
---

# `enemy/` — Support actors catalogue

Eighteen smaller actors that live alongside `WATCHER` / `MERYL7`.
Most are **visual companions** (eye-flash, gun-light, laser sight) or
**special encounter pieces** (security camera, searchlight tower,
katana variant).

## Visual feedback (spotted-Snake icon, gun lights)

### `eyeflash.c` (151 lines)

The `!` exclamation-mark icon that pops above a guard's head when he
spots Snake. Contains a 3D billboard with an animated yellow sprite.

- Spawned by `NewEyeFlash_*` from `command.c` when alert_level
  transitions to "see player".
- Owns a `GV_ACTOR_LEVEL5` slot, lifetime ≈ 2 s.
- Renders via DG_DEF / DG_OBJS; the animation is a simple time-based
  scale ramp (start small, snap to full size, fade).
- Self-destroys when `time > duration` or when its parent
  `WatcherWork` is destroyed first (followed via stored pointer +
  null check).

### `glight.c` (128 lines)

Gun-tip light. The yellow flash at the muzzle when a guard fires.
Spawned per shot by `command.c::ENE_Fire`. Tied to a body-bone
matrix so it follows the gun across animations.

Unlike `chara/snake/`'s `NewSnakeGunLight_*`, this version is shared
across all enemies — it just needs the source matrix and a colour
override. Spawns a small `DG_PRIM` quad in the additive sort layer,
expires after ~3 frames.

### `l_sight.c` (108 lines)

Laser-sight beam — the red line a few elite guards (s11d / s14a)
project. Drawn as a long thin DG_PRIM polygon ending at the first
HZD wall hit. Updates every frame; cheap.

### `kiken.c` (80 lines)

The "!?" `Kiken` (危険 — danger) icon used by surprise / suspicious
states (a level-2 alert below `eyeflash`'s `!`). Same lifecycle as
eyeflash but a different sprite.

## Footstep / shadow (cinematic)

### `asiato.c` / `asiato2.c` / `asioto.c` (501 / 342 / 253 lines)

Footstep markers — they spawn on each guard step and persist as
fading translucent prints on the floor.

- `asiato.c` is the canonical version; `asiato2.c` is a tuned variant
  (different texture / scale curve); `asioto.c` is a third spelling
  (likely from a different file and never harmonised).
- `WatcherWork::field_BA3 |= 0x10` after `-c` GCL flag enables footstep
  spawning for that guard (see `meryl7.c` GCL options).
- Each spawned print is a billboard quad in `DG_Chanls[*]`, lifetime
  ~1 s, fade alpha over the last 20 frames.

### `demoasi.c` / `demokage.c` (191 / 244 lines)

Cinematic-only versions of footstep / shadow. Used by demo .DMO
playback when guards walk during cutscenes — they don't spawn during
gameplay because the gameplay system uses `asiato.c` directly.

`demokage.c` (kage = 影 = shadow) is the under-foot character shadow.
It mirrors `chara/snake/shadow.c` for non-Snake characters.

## Encounter pieces

### `camera.c` (1127 lines)

The wall-mounted security camera enemy. Has its own state machine —
no MOTION_CONTROL because it doesn't walk; just a yaw-pitch turret
that swivels along a programmed sweep pattern.

- Spawns with GCL options `-p` (position), `-d` (sweep direction),
  `-a` (sweep angle), `-r` (rotation speed), `-z` (zone-id).
- Vision uses the same `VISION` struct as a watcher (facedir / angle /
  length), so it triggers alert state through the existing
  `command.c::CheckSeen` test.
- Three alert sub-states: idle sweep, suspicious fixate, alarm-fire
  (broadcasts mesg 0xC356 to nearby watchers).
- Owns a small `WALL_GLOW` sub-actor for the red blinking LED.

### `searchli.c` (1117 lines)

The searchlight tower (s00a roof, s11d courtyard). A taller, cone-
shaped variant of `camera.c` — same swivel logic plus a *cone* of
visible light projected into the world (rendered as an additive
gradient).

- Cone visualisation is a custom `DG_PRIM` mesh updated each frame.
- Walking inside the cone trips alert with a faster ramp than the
  default vision system.

### `katana.c` (226 lines)

A katana-variant guard — used in stage s14b (the cyborg ninja
encounter). Smaller than a watcher; just an action-table override
that swaps gun for sword animations and a TARGET hit registry that
deals melee damage instead of bullet.

### `boxkeri.c` (199 lines)

"Box-kick" (keri = 蹴り = kick) — the cinematic where a guard kicks a
cardboard box Snake's hiding under. Spawned by `box.c` (in `thing/`)
when a guard's vision crosses the box for long enough.

- Plays a kick animation, then sends a mesg to the box actor that
  forces Snake out of hiding.
- One-shot actor; self-destroys on completion.

### `wall.c` (279 lines)

The `WALL` chara — a static-prop placeholder that cinematics spawn
when they need a destructible barrier or environment piece. Despite
the name it's an `enemy/` actor because it has CONTROL + a TARGET
that takes bullet hits.

Used heavily in DMO playback to render walls/posts that get blown
away in scripted explosions.

## Dynamic HZD geometry

### `dymc_seg.c` (99 lines)

"Dynamic segment" — a runtime-toggleable HZD wall. The bathroom-can
hide-zone (s07a) and toilet-stall door (s07a) are implemented as
dynamic segments controlled by `meryl7.c`.

- Allocated via `s07a_dymc_seg_800D65C8(name, &min, &max, min_h,
  max_h, flag, &enable_ptr)`.
- The `enable_ptr` is a global int — write 0 to enable wall, 1 to
  disable.
- HZD's collision raycaster checks the enabled flag during scan; a
  disabled segment is skipped as if it didn't exist.

This is how the bathroom-stall door appears solid until Meryl walks
through it (her think.c writes 1 to disable while she crosses).

## Generic destructible

### `object.c` (570 lines)

Generic destructible-object base. Used by `thing/box.c`, `thing/door.c`
and several stage-private destructibles. Provides:

- 1 TARGET (TARGET_FLAG) for HP tracking.
- Damage callback dispatch (`object_die`).
- KMD swap on damage (intact → cracked → destroyed).
- Particle-spawn callback on each damage state.

It's filed under `enemy/` because it's an actor-level type even
though no enemy AI is involved — historical filing decision.

## Smoke

### `smoke.c` (238 lines)

Enemy-side smoke effect. Separate file from `anime/effect/smoke.c`
(2D billboard) and `okajima/smktrgt.c` (smoke target marker) — this
one is the *grenade-explosion* smoke that obscures the guard's
vision after a chaff/smoke grenade.

Sets `field_BA3 &= ~0x07` on every watcher in radius for the smoke
duration, which clears the player-spotted bits and forces alert level
to recompute from scratch — the gameplay-meaningful effect.

## Enemy grenade

### `grnad_e.c` (153 lines)

Enemy grenade-throw projectile. Spawned by an alert guard when his
think.c picks the grenade-attack state.

- Inherits behaviour from `bullet/bakudan.c` but with a fixed
  damage/radius and a slower fuse to give Snake time to dodge.
- Owns its own arc-trajectory integration (no full physics — just a
  parabola).

## See also

- [watcher.md](watcher.md) / [meryl7.md](meryl7.md) — the actors
  these supports decorate.
- [`source/thing/`](../thing/index.md) — non-character actors,
  which `object.c` is sister-to.
- [`source/anime/effect/`](../anime/effect.md) — 2D billboard
  effects, where some of these duplicates live.
