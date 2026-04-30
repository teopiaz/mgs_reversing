---
file: source/thing/{door,emitter,sgtrect3,sight,snow,sphere}.c
---

# `source/thing/` — non-character actors

Six "things": geometric / utility actors that aren't characters
but participate in gameplay (collision, triggers, visual fx).

## File map

| File | What |
| ---- | ---- |
| `door.c` | Door actor — opens/closes, collision toggles |
| `emitter.c` | Particle emitter — spawns sub-actors over time |
| `sight.c` | "Sight" trigger — eye-cone region for cinematic events |
| `sgtrect3.c` | "Segment-rect-3" — 3D rectangle trigger zone |
| `snow.c` | Snowfall effect (s00a, exterior stages) |
| `sphere.c` | Spherical trigger (used by some bosses) |

## door.c

The most commonly-spawned thing. Each door:

- Has a KMD (the door panel mesh).
- Has a state: CLOSED / OPENING / OPEN / CLOSING.
- Has a collision segment (HZD) that's enabled/disabled with state.
- Reacts to mesg from the player when proximity detected.

GCL spawn pattern:

```
chara &DOOR -p (x, y, z) -d (rotY) -i ID -a "auth_level"
```

When the player approaches an unlocked door, ENTER prompt appears;
press B to open. Locked doors require key cards / IDs (matched
against player's inventory).

### Door state machine

```
CLOSED: HZD seg enabled (player can't pass)
   ↓ player approaches + auth check passes
OPENING: animate KMD over 30 frames; HZD seg still enabled
   ↓
OPEN: HZD seg DISABLED (player can pass)
   ↓ N frames after player leaves
CLOSING: animate KMD over 30 frames; HZD seg disabled (so player can re-enter)
   ↓
CLOSED
```

## emitter.c

Generic particle spawner. Owns a callback `spawn_one(work)` that
registers a fresh effect actor per tick.

Configuration:

- `period` — frames between spawns
- `total` — max spawns (or -1 = infinite)
- `target_pos` — world position
- `effect_id` — which effect actor to spawn

Used by:

- Steam vents (continuously emit `okajima/smktrgt`).
- Fire (continuously emit `bullet/blast` flame variant).
- Cinematic dust trails.

## sight.c

A "vision-cone" trigger zone. Used for boss arena entry: when the
player walks within `sight`'s cone (angle + length), trigger a
cinematic.

Internally just a `TARGET_TOUCH` shaped as a cone. On hit, posts
mesg to the bound script.

## sgtrect3.c

3D axis-aligned rectangle trigger. Fires a GCL event when an actor
enters its volume. Used for area-transition triggers between
sub-areas.

## snow.c

Snowfall effect for outdoor stages (s00a winter exterior, s12a
interior with snow leaks).

- Spawns ~50 white quads at random positions above camera.
- Each quad falls with a small horizontal drift.
- Quads at the bottom respawn at top.
- All rendered as additive billboards in `DG_Chanl(0)`.

## sphere.c

Spherical trigger zone. Used by bosses with a "engagement radius"
(player enters → boss intro plays).

## Pitfalls

- **Doors and HZD seg sync is fragile.** A mismatch (state =
  OPEN but seg enabled) means player walks through invisible
  walls. State machine must atomically toggle both.
- **Emitters can leak.** If an emitter has `total = -1` and isn't
  destroyed by GCL, it keeps spawning forever — frame rate dies.
- **Sight cone direction.** The cone facing direction is read
  from the spawn `-d` GCL option; getting the convention wrong
  (forward = +Z or +X) is a common bug.

## See also

- [`source/libhzd/dynamic.md`](../libhzd/dynamic.md) — how doors
  toggle HZD segments.
- [`source/anime/effect.md`](../anime/effect.md) — what emitters
  spawn.
- [`source/okajima/`](../okajima/index.md) — sister effects
  library.
