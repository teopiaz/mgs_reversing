---
file: source/game/map.c
---

# `game/map.c` — maps + sub-areas

Per-stage map management. A "map" in MGS = an area's HZD + KMD +
PCX bundle. Some stages have multiple maps (rooms / floors); map.c
handles switching between them. 516 lines.

## Definitions

```c
typedef struct GM_MAP {
    HZD_HDL  *hzd;              // collision data for this area
    DG_DEF   *kmd;              // model data for the area mesh
    int       n_subzones;
    SVECTOR  *spawn_points;     // named entry points
    // ... more
} GM_MAP;

extern GM_MAP *GM_CurrentMap;     // active map this frame
extern GM_MAP  GM_StageMaps[N];   // per-stage list
```

## API

```c
void  GM_InitMap(void);
void  GM_ResetMap(void);
void  GM_SetCurrentMap(GM_MAP *map);
GM_MAP *GM_GetMap(int idx);
int   GM_GetMapAtPos(SVECTOR *pos);    // which map contains the point
void  GM_ChangeMap(int new_map_idx);    // trigger transition
```

## Map transitions

The player crosses a sub-area boundary (a special HZD trap in some
stages):

1. Trap fires → calls `GM_ChangeMap(new_idx)`.
2. `GM_CurrentMap` is updated.
3. Each actor that has `control->map` re-binds to new HZD.
4. Camera re-evaluates zone (different HZD = different zone graph).
5. Some actors (those tied to old map) self-destroy via
   message-pass.

Sub-area transitions are *cheaper* than full stage loads — no
disc-streaming, just pointer swaps.

## `GM_CurrentMap` reset

After every actor's `act()` callback in `GV_ExecActorSystem`,
the framework sets `GM_CurrentMap = 0` — defensive reset to force
re-binding.

This prevents an actor from accidentally using the previous
actor's map for collision queries (which would be wrong if the
two are in different sub-areas).

## Multi-area stage example: s11d (basement / ground floor)

s11d has 2 maps:

```
GM_StageMaps[0]  ground floor (where Snake meets Otacon)
GM_StageMaps[1]  basement (the lab area)
```

A trap on the elevator binds:

```
trap → GM_ChangeMap(elev_target_floor) + reposition player
```

So elevator transitions are map switches, not stage loads. Each
map has its own HZD with its own zone graph.

## Single-area stages

Most stages have just one map. `GM_StageMaps[0]` is set at boot;
`GM_CurrentMap = &GM_StageMaps[0]` for the entire stage.

## Pitfalls

- **Don't hold `GM_CurrentMap` pointers across actor callbacks.**
  The defensive reset means it might be NULL when you next read it.
- **An actor's `control->map` outlives the active map switch only
  if you re-bind it manually**. Default behaviour is to keep the
  old map's HZD until the next change.
- **Map index 0 is *not* a sentinel.** It's a valid map (the
  default). NULL / -1 are the "no map" indicators.

## Port notes

The port preserves the exact map switching behaviour. No 64-bit
adjustments needed because GM_MAP entries are small.

## See also

- [`source/libhzd/`](../libhzd/README.md) — collision data.
- [`source/libdg/obj.md`](../libdg/obj.md) — KMD data.
- [`source/game/area.c`](../../../../source/game/area.c) —
  area-bound dispatching helpers.
- [03-control-and-motion.md](../03-control-and-motion.md) —
  `control->map` field.
