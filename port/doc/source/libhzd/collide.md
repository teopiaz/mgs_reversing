---
file: source/libhzd/collide.c
---

# `libhzd/collide.c` — line and point collision

The hot path of MGS collision: 1455 lines, exercised every frame by
every actor. Implements **line raycasts** and **point-in-radius**
queries against the HZD scene.

## Mental model

The HZD scene contains:

- **Floors** (`HZD_FLR`) — triangulated walkable surfaces.
- **Segments** (`HZD_SEG`) — vertical wall planes.
- **Dynamic** versions of both — toggleable at runtime (e.g. doors).

Each query is parameterised by:

- `flag`: which categories to check (`HZD_CHECK_FLR | _SEG | _DYNFLR | _DYNSEG`).
- `exclude`: a flag bitmask that skips segments matching it (e.g.
  bullet checks set `HZD_SEG_NO_BULLET` to ignore "ghost" walls).

## `HZD_LineCheck` — raycast

```c
int HZD_LineCheck(HZD_HDL *hzd, SVECTOR *from, SVECTOR *to, int flag, int exclude);
```

Tests if the line `from → to` crosses any segment (or floor edge if
floors are in `flag`). Returns 1 if blocked, 0 if clear.

If blocked, the *nearest* hit is recorded in private state for the
caller to query:

```c
void *HZD_LineNearSurface(void);   // pointer to the surface struct hit
int   HZD_LineNearFlag(void);      // the surface's flag bits
void  HZD_LineNearDir(SVECTOR *out);   // the surface's facing direction
void  HZD_LineNearVec(SVECTOR *out);   // the hit point itself
```

Used everywhere — bullet trajectory, guard line-of-sight, missile
explosion ray, "can the player hide here" checks, …

### Exclude flags

```c
HZD_SEG_NO_COLLIDE   0x01    // don't collide
HZD_SEG_NO_NAVIGATE  0x02    // enemy navigation skips
HZD_SEG_NO_PLAYER    0x04    // player sight (hides behind)
HZD_SEG_NO_MISSILE   0x08    // missile collisions
HZD_SEG_NO_HARITSUKI 0x10    // C4 attachment
HZD_SEG_NO_BULLET    0x20    // bullets pass through
HZD_SEG_NO_BEHIND    0x40    // player lean
HZD_SEG_NO_RADAR     0x80    // radar draw
```

A guard's vision cone uses `HZD_LineCheck(... HZD_SEG_NO_PLAYER)` —
walls flagged "no player" don't block the cone (e.g. transparent
glass partitions). Missiles use `HZD_SEG_NO_MISSILE` similarly.

## `HZD_PointCheck` — radius query

```c
int HZD_PointCheck(HZD_HDL *hzd, SVECTOR *point, int range, int flag, int exclude);
```

Returns the count of **nearby** segments + floors within `range`
of `point`. The hits are stored internally for the caller to walk:

```c
void HZD_PointNearSurface(void **surface);   // pointer to surface array
void HZD_PointNearFlag(char *flags);          // flag bytes per hit
void HZD_PointNearVec(SVECTOR *vectors);     // closest-point vectors
```

Used by:

- Push-out collision: the player and guards run `PointCheck`
  every frame, then for each near-segment correct their position
  to be ≥ `radius` away.
- Trap triggers: `event.c` queries `PointCheck` for events near
  the actor.

## `HZD_StepCheck` — step-up logic

```c
int HZD_StepCheck(SVECTOR *nears, int count, int scale, SVECTOR *out);
```

Given an array of near-points + their normals (output of
`PointCheck`), compute the cumulative push-out vector that resolves
all collisions simultaneously. Returns the dominant normal.

This is the multi-collision resolver: when an actor is wedged
between two walls, single-collision push-out would push past
the other; StepCheck blends both into a single corrective vector.

## `HZD_SurfaceNormal`

```c
void HZD_SurfaceNormal(HZD_FLR *floor, SVECTOR *out);
```

Computes the unit normal of a triangular floor. Used by the
slope-aware step logic in `level.c`.

## Pitfalls

- **`HZD_LineCheck` results are global state.** Multiple checks
  per frame from different callers all write to the same
  `HZD_LineNearSurface`. Read out the result *immediately* after
  the call.
- **`SVECTOR` is short[3].** Distances and ray lengths fit in
  signed 16-bit (PSX world units roughly mm-scale). Long rays
  overflow.
- **The handler must be valid.** `HZD_HDL` is per-stage; passing
  the wrong one collides against the wrong scene.

---

## Port notes

`collide.c` runs unmodified on the port — pure math. The HZD data
structures are loaded by `HZD_LoadInitHzd` (in `hzdd.c`) which the
port patches for 64-bit pointer fix-up; once loaded, collide.c sees
identical data.

## See also

- [level.md](level.md) — floor-level testing built on these.
- [zone.md](zone.md) — zone navigation (uses different queries).
- [event.md](event.md) — trap / bind dispatch via PointCheck.
- [03-control-and-motion.md](../03-control-and-motion.md) —
  `GM_ActControl` invokes `HZD_PointCheck` + `HZD_StepCheck` to
  push the actor out of walls.
