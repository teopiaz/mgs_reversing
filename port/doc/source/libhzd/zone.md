---
file: source/libhzd/zone.c
---

# `libhzd/zone.c` — zone graph + AI navigation

The 989 lines that drive guard pathfinding. Stages are partitioned
into named **zones**; zones connect via portals; navigation searches
the resulting graph to plan routes.

## Mental model

Each HZD stage carries:

- `n_zones` — count of zones (typically 8..32 per stage).
- Per-zone `nears[]` — list of adjacent zones (the graph edges).
- Per-zone bounding shape (a triangulated convex hull).

A guard's `WatcherWork::start_addr` is its current zone; routes
expressed in GCL (`-r 0`, `-r 1`) index per-stage **route tables**
that are sequences of zone IDs.

## `HZD_GetAddress` — point → zone

```c
int HZD_GetAddress(HZD_HDL *hzd, SVECTOR *pos, int hint);
```

Returns the zone ID containing `pos`. `hint` is a previous-frame
zone ID — searched first (cache locality), falls back to a full
walk on miss. Returns `HZD_NO_ZONE = 0xFF` if no zone covers
`pos`.

This is the *most-called* HZD function: every frame, every actor's
position is run through `GetAddress` to update its zone, which
drives:

- AI pathing (which zones can the guard reach next?)
- Sound culling (only play sounds from zones near the player)
- Camera switching (camera transitions on zone boundary)
- Event triggers (entering zone X fires GCL event)

The port spent significant effort on a bug here: see
[camera_zone_investigation.md](#) — stage GCL had to send a
specific message at zone transitions.

## `HZD_ReachTo` — direct reachability

```c
int HZD_ReachTo(HZD_HDL *hzd, int from, int to);
```

Quick check: is `to` directly adjacent to `from`? Returns 1 / 0.
Used by AI think loops as a fast pre-filter before invoking the
full pathfinder.

## `HZD_LinkRoute` — A* pathfinder

```c
int HZD_LinkRoute(HZD_HDL *hzd, int from, int to, SVECTOR *next_pos);
```

The full pathfinder. Computes the shortest path from `from` to `to`
through the zone graph, returns the next zone ID, writes a
suggested intermediate position into `next_pos`.

Uses a small fixed-size open/closed list (no dynamic allocation),
so paths beyond ~32 zones may not converge. In practice no MGS
stage exceeds this; pathfinding is hierarchical (zones, not
individual tiles).

## `HZD_NavigateLimit` / `HZD_NavigateBound`

Bounded path queries — like `LinkRoute` but with a *cost limit*.
Used by guards to decide "should I chase the player or give up" —
if the cost-limited path doesn't reach, the guard falls back to a
non-pursuing alert state.

## `HZD_ZoneDistance` — zone-graph distance

```c
int HZD_ZoneDistance(HZD_HDL *hzd, int from, int to);
```

Returns the **graph distance** (number of edges) from `from` to
`to`, ignoring physical distance. Used for "how far is the player
from this guard's zone" coarse decisions.

## `HZD_GetNears` / `HZD_MaxNear` / `HZD_MinNearDist`

Lower-level helpers exposing the per-zone adjacency lists. Used by
custom navigation (e.g. searchlight tower picks the *nearest* zone
to its current target).

## `HZD_ZoneContains`

```c
int HZD_ZoneContains(HZD_HDL *hzd, SVECTOR *pos, int zone);
```

Tests whether `pos` is inside the named `zone` shape. Cheaper than
`GetAddress` if you already know which zone you're checking.

## Globals + caches

The zone module maintains a small per-frame cache:

- "Last queried position → zone" (~4 entries) for `GetAddress`.
- A reverse-mapping table built at HZD load time for `ReachTo`.

These are reset by `HZD_StartDaemon` between stages.

## Pitfalls

- **Zone IDs are 8-bit.** Max 256 zones per stage. `HZD_NO_ZONE
  = 0xFF` is the reserved "not found".
- **Adjacency is bidirectional but not always symmetric.** Some
  one-way doors mark adjacency as `from → to` only. AI must respect
  this.
- **The pathfinder doesn't know about traps.** Guards routed
  through a zone with a player-laid trap will trigger it.
- **`hint` matters for performance.** Passing -1 (= no hint) makes
  `GetAddress` walk every zone. Always pass the actor's stored
  `start_addr` to keep the search local.

## Port notes

The port had a bug where stage transitions weren't refreshing
the zone cache; fixed in
[s99a_live_game_findings.md](#) with an `n_zones==0` guard inside
`HZD_GetAddress`.

## See also

- [collide.md](collide.md) — segment collision (point-to-zone is
  unrelated to wall-collide).
- [event.md](event.md) — events bind to zones.
- [`source/enemy/think.c`](../../../../source/enemy/think.c) —
  primary consumer.
