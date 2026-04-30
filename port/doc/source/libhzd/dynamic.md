---
file: source/libhzd/dynamic.c + hzdd.c
---

# `libhzd/dynamic.c` + `hzdd.c` — runtime-toggleable HZD elements

A static HZD is baked into the disc; gameplay needs *some* walls and
floors to flip on/off (doors, dynamic-segment hides, destroyed
floors). `dynamic.c` is that mechanism.

## Dynamic segments — toggleable walls

```c
int  HZD_QueueDynamicSegment2(HZD_HDL *hzd, HZD_SEG *seg, int flag);
void HZD_DequeueDynamicSegment(HZD_HDL *hzd, HZD_SEG *seg);
void HZD_SetDynamicSegment(HZD_SEG *a1, HZD_SEG *a2);
```

A "dynamic segment" is a wall plane the engine can enable/disable
at runtime. The collision code in `collide.c` checks every segment's
enabled flag during a raycast; disabled segments are skipped as if
they don't exist.

Use cases:

- Doors. Closed door = enabled, open door = disabled.
- The bathroom can / stall toggling in s07a (see
  [enemy/meryl7.md](../enemy/meryl7.md)).
- Destroyed bridges in s14a.

Each `HZD_HDL` has a fixed pool (`max_dynamic_segments`) of slots
allocated at HZD-init. Queueing past the limit returns -1.

## Dynamic floors — toggleable triangles

```c
int  HZD_QueueDynamicFloor(HZD_HDL *hzd, HZD_FLR *floor);
void HZD_DequeueDynamicFloor(HZD_HDL *hzd, HZD_FLR *floor);
```

Similar to dynamic segments but for floor triangles. Used for:

- Holes in the ground that open up (e.g. trap doors).
- Conveyor sections that come and go.
- Cinematic-only floors (added during a cutscene to constrain
  cinematic-walking, removed afterward).

## Flag storage — `dynamic_flags`

The `HZD_HDL` carries a `char *dynamic_flags` array — one byte per
dynamic segment. Bytes encode the same flags as static segments
(`HZD_SEG_NO_PLAYER` etc.) so dynamic walls can be filtered by
the same bitmask logic.

## `HZD_SetDynamicSegment` — copy

Given two segments, copies metadata (vertex coords, flags) from one
to the other. Used when a single physical "door" cycles through a
few stored configurations (open / half-open / closed positions).

## `hzdd.c` — handler + loader

The per-stage manager.

```c
void     HZD_StartDaemon(void);
int      HZD_LoadInitHzd(void *buf, int id);
HZD_HDL *HZD_MakeHandler(HZD_MAP *hzd, int areaIndex,
                          int dynamic_segments, int dynamic_floors);
void     HZD_FreeHandler(void *ptr);
void     HZD_MakeRoute(HZD_MAP *hzd, char *arg1);
```

### `HZD_LoadInitHzd` — file loader

Registered with `cache.c` for extension `'h'`. Called when a `.hzd`
file is loaded; processes the file in place via `OFFSET_TO_PTR`
fixups so internal offsets become real pointers.

### `HZD_MakeHandler`

Allocates a runtime `HZD_HDL` from the parsed HZD file. Allocates
the dynamic-floor / dynamic-segment pool to the requested sizes.

The same HZD file can be used with multiple handlers (e.g. for
sub-areas of a multi-room stage). Each handler has its own dynamic
state.

### `HZD_MakeRoute(hzd, arg1)`

Builds the per-stage route table from raw HZD route data. The
`arg1` is a remapping table (used when sub-areas have shifted zone
IDs).

### Globals

- `HZD_CurrentGroup` — currently active HZD group (sub-area).
  Walked by per-frame consumers to know which handler to query.

## Pitfalls

- **Pool sizes are fixed at handler creation.** Adding more dynamic
  segments at runtime than the pool allows just drops them.
- **Dequeue is by pointer match.** Calling Dequeue with a Seg that
  isn't queued is a no-op; calling on a Seg pointer that's been
  reused for something else corrupts state.
- **Handler lifecycle is tied to stage.** Don't hold an `HZD_HDL *`
  across stage transitions — `HZD_FreeHandler` will reclaim it.
- **The `HZD_addr_shift` inline.** Sometimes used as `addr |
  (addr<<8)` to expand 8-bit zone addresses; understand which
  context you're in before using it.

---

## Port notes

`HZD_LoadInitHzd` is the most heavily-modified file in the port.
The macro `OFFSET_TO_PTR(ptr, offset) = *(int*)offset = (int)ptr
+ *(int*)offset` is a 32-bit cast that breaks on 64-bit pointers,
so the port intercepts the loader to rebuild the HZD into a
64-bit-pointer-friendly representation. See
[project_32bit_pointer_blocker](#) for context.

The runtime behaviour after load is unchanged — `collide.c`,
`level.c`, `zone.c`, `event.c` all see identical data once
loading completes.

## See also

- [collide.md](collide.md) — segment-disable check happens in the
  collision raycaster.
- [`source/enemy/dymc_seg.c`](../../../../source/enemy/dymc_seg.c)
  — gameplay-side wrapper around dynamic-segment queueing.
- [`source/include/fmt_hzd.h`](../../../../source/include/fmt_hzd.h)
  — HZD file format.
