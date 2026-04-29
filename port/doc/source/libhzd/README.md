# `source/libhzd/` — collision & event system

The collision-detection + region-event-trigger library. HZD =
"Hazard" / "Hazardous". A `.hzd` blob per stage holds:

- **Walls** — vertical line segments (`HZD_SEG`).
- **Floors** — flat polygons (`HZD_FLR`).
- **Zones** — region polygons (`HZD_ZONE`).
- **Routes** — path waypoint chains (`HZD_PAT` / `HZD_PTP`).
- **Traps** — area triggers bound to GCL procs (`HZD_TRP`).
- **Cameras** — zone-camera definitions (`HZD_CAM`).

## Files

| File | Role |
| ---- | ---- |
| [`hzdd.c`](../../../../source/libhzd/hzdd.c) | HZD loader — parses the `.hzd` blob, fixes up offset → pointer, registers cache entry. |
| [`level.c`](../../../../source/libhzd/level.c) | `HZD_GetAddress(hzd, &pos, level)` — the spatial-partition lookup. Given a world point, find the bucket (level) it's in. |
| [`collide.c`](../../../../source/libhzd/collide.c) | Wall + floor + ceiling collision tests — used by `GM_ActControl`'s `CheckCollide`, `CheckHeight`, `CheckNear`. |
| [`zone.c`](../../../../source/libhzd/zone.c) | Zone-polygon point-in-region tests. |
| [`event.c`](../../../../source/libhzd/event.c) | Trap firing — `HZD_SetEvent`, `HZD_ReExecEvent`. |
| [`dynamic.c`](../../../../source/libhzd/dynamic.c) | Runtime modification — enabling / disabling walls / traps mid-game. |

## Public API

```c
typedef struct HZD_HDL {
    HZD_FILE_HEADER *header;     /* parsed sub-table pointers */
    /* …other state… */
} HZD_HDL;

HZD_ADDR  HZD_GetAddress(HZD_HDL *hzd, SVECTOR *pos, int level);
int       HZD_CheckCollide(HZD_HDL *hzd, SVECTOR *pos, int radius_sq, ...);
HZD_FLR  *HZD_GetFloor(HZD_HDL *hzd, SVECTOR *pos);
int       HZD_GetCeil(HZD_HDL *hzd, SVECTOR *pos);

void HZD_SetEvent(HZD_EVT *evt, int script_data);
void HZD_ReExecEvent(HZD_HDL *hzd, HZD_EVT *evt, int mask);
```

## How a frame's collision works

```c
/* In game/control.c::GM_ActControl */
hzd = control->map->hzd;
CheckCollide(control, hzd);    /* call into libhzd */
control->mov.vx += control->step.vx;
control->mov.vz += control->step.vz;
CheckNear(control, hzd);
CheckHeight(control, hzd);
```

`CheckCollide` queries every wall in the spatial bucket
(`HZD_GetAddress` first to find the bucket); for each wall, tests
distance to the actor's circle (`step_size² = radius²`). If a wall
overlaps, push the actor out along the wall normal.

## Trap dispatch

When an actor enters a trap region:

1. `GM_ActControl` writes `control->event.pos = control->mov`
   each tick.
2. The trap dispatcher (in `event.c`) tests every trap polygon
   against `event.pos`.
3. On entry, fires the trap's bound GCL proc id via
   `GCL_ExecProc`.
4. On exit, may fire a different proc with mask 2.

This is how stage scripts react to "Snake walked into the
hangar" etc.

## Routes

`HZD_PAT` and `HZD_PTP` define authored paths. Used by:

- Doll patrol (`animal/doll/doll.c::s01a_doll_800DBF28`).
- Some enemy patrol routes.
- Cinematic camera paths in some overlays.

## Zones + cameras

`HZD_ZONE` is a polygon region with associated metadata. `HZD_CAM`
binds a zone to a camera config (eye / center / track values).
When Snake enters a camera zone, `game/camera.c` swaps to that
camera's framing.

## Dynamic modifications

`dynamic.c` provides:

- `HZD_EnableSeg` / `HZD_DisableSeg` — turn walls on/off (used
  for breakable walls, opening doors).
- `HZD_EnableTrap` / `HZD_DisableTrap` — toggle event regions.

Used by GCL `mesg` events targeting wall / door actors.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [collide.md](collide.md) | LineCheck / PointCheck / StepCheck — wall + segment collision |
| [level.md](level.md) | Floor-level testing — slope, hazards, stacked floors |
| [zone.md](zone.md) | Zone graph + AI navigation, GetAddress, LinkRoute (A*) |
| [event.md](event.md) | Events / traps / GCL bindings, lifecycle |
| [dynamic.md](dynamic.md) | Runtime-toggleable segments / floors + hzdd loader |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [03-control-and-motion.md](../03-control-and-motion.md) — the
  `GM_ActControl` flow that calls into here.
- [`source/include/fmt_hzd.h`](../../../../source/include/fmt_hzd.h)
  — `HZD_*` struct definitions.
- [`port/libhzd/hzd_loader.c`](../../../../port/libhzd/hzd_loader.c)
  — port-side HZD loader (handles 32-bit-pointer fixup).
