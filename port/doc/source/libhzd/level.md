---
file: source/libhzd/level.c
---

# `libhzd/level.c` — floor-level testing

Y-axis collision: "what's the floor height under this actor", "is
this point in a hazardous zone", slope handling.

## Functions

| Fn | Body |
| -- | ---- |
| `HZD_LevelTestFloor(floor, point)` | Is point inside the floor's triangle (XZ plane)? |
| `HZD_SlopeFloorLevel(point, floor)` | Y-coordinate of the floor at point — handles slope |
| `HZD_LevelTestHazard(hzd, point, flags)` | Is point in a hazardous floor? |
| `HZD_LevelMinMaxFloors(floors)` | Bounding floors at point (top/bottom) |
| `HZD_LevelMinMaxHeights(levels)` | Y-min / Y-max of those floors |
| `HZD_LevelMaxHeight()` | Highest floor at last queried position |

## Mental model

The HZD floors are 2D triangles in XZ — they ignore Y for the
"is point inside" test (treating them as a 2D mesh seen from
above). Y comes from the slope formula:

```c
y = a*x + b*z + c                  // plane equation
```

`HZD_SlopeFloorLevel` evaluates that for a given point, returning
the precise floor Y at that XZ. This is what the player's
`CONTROL.mov.vy` snaps to during ground-walking.

## `MinMaxFloors` — stacked floors

A single XZ position can lie under multiple floor triangles
(stairs, balconies, multi-storey rooms). `MinMaxFloors` returns the
two relevant floors:

```
top floor    ← the highest one above the actor's current Y
bot floor    ← the highest one below or equal to the actor's Y
```

The "current floor" is `bot` (you stand on the highest floor that's
at or below you). `top` is the *ceiling* — used for jump-clearance.

## Hazardous floors

`HZD_LevelTestHazard(hzd, point, flags)` checks if the floor under
the point has a hazard flag matching `flags`:

| Hazard flag | Effect |
| ----------- | ------ |
| (set in `fmt_hzd.h`) | Damage / death zones (electrified, lava-equivalent) |

The flags are per-floor, encoded in the HZD file. Some stages have
floors that deal damage (e.g. radioactive zones) — the gameplay code
calls `HZD_LevelTestHazard` to detect them.

## `HZD_addr_shift` — address compression

The header defines:

```c
static inline int HZD_addr_shift(int addr) {
    int temp = addr & 0xFF;
    return temp | (temp << 8);
}
```

This expands an 8-bit "zone address" into a 16-bit "scratch
address" used by the navigation system. `addr` is 8-bit because
each stage has at most 256 zones.

## Pitfalls

- **`HZD_SlopeFloorLevel` works in PSX fixed point.** Y values are
  multiplied/divided in 12.4 fixed; large coordinates overflow.
- **The "current floor" assumption requires being inside the
  triangle.** If the actor steps off the edge, `MinMaxFloors`
  returns no floor; the actor must fall.
- **Multi-storey requires careful `top` / `bot` reasoning.** A
  jumping actor hits `top` when his head (Y + height) reaches it,
  not when his feet do. Common bug: testing wrong Y.

## Port notes

Pure math; runs unmodified.

## See also

- [collide.md](collide.md) — segment collision (X/Z walls).
- [zone.md](zone.md) — high-level zone graph navigation.
- [`source/include/fmt_hzd.h`](../../../../source/include/fmt_hzd.h)
  — `HZD_FLR` struct layout.
