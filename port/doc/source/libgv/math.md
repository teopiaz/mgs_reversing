---
file: source/libgv/math.c + math_near.c + math_quat.c + strcode.c
---

# `libgv/math*.c` — vector / angle / interpolation helpers

Three math files plus the strcode hash. All operate on `SVECTOR`
(short[4]) or 12-bit fixed-point angles (4096 = 360°).

## `math.c` — vector + scalar primitives

| Fn | Body | Notes |
| -- | ---- | ----- |
| `GV_AddVec3(v1,v2,d)` | component-wise add | |
| `GV_SubVec3(v1,v2,d)` | component-wise sub | |
| `GV_VecLen3(v)` | `SquareRoot0(Square0(v))` | uses GTE square+sqrt |
| `GV_LenVec3(in,out,denom,num)` | scale `in` by `num/denom` (12.12 fixed) | |
| `GV_DiffVec3(v1,v2)` | `len(v1-v2)` | |
| `GV_VecDir2(v)` | `ratan2(vx, vz) & 0xFFF` | 2D angle in xz plane |
| `GV_DirVec2(angle,r,out)` | `(r·sin, 0, r·cos)` / 4096 | flat circle |
| `GV_DirVec3(angle,len,out)` | `RotMatrixYXZ` then forward axis | full 3D |
| `GV_DiffDirU(from,to)` | `(to - from) & 0xFFF` | unsigned 12-bit angle delta |
| `GV_DiffDirS(from,to)` | signed-wrap to ±2048 | |
| `GV_DiffDirAbs(from,to)` | absolute, ≤ 2048 | |
| `GV_RandU(n)` | `rand() & (n-1)` — n must be power of 2 | |
| `GV_RandS(n)` | `rand() & (2n-1) - n` — signed range [-n, n) | |

### Angle convention

The MGS engine uses **12-bit fixed-point angles**: 4096 = 360°.
Internally `rsin`/`rcos`/`ratan2` are PSX BIOS calls that operate on
this scale. Bit-pattern `0x800` (= 2048) is 180°, `0x400` is 90°.

Helpers like `GV_DiffDirS` are essential because angle subtraction
naturally wraps; `(0x100 - 0xF00) & 0xFFF = 0x200` (forward 45°)
not `-0xE00` (backward 315°). Always use the helper instead of raw
arithmetic.

## `math_near.c` — interpolation helpers

A toolkit of "step `from` toward `to` and return the new value"
functions, used everywhere the engine needs smooth interpolation
without keeping per-actor history.

### Scalar variants

| Fn | Behaviour |
| -- | --------- |
| `GV_NearExp2(from,to)` | `from + (to-from)/2` — halfway each step |
| `GV_NearExp4(from,to)` | `/4` — quarter |
| `GV_NearExp8(from,to)` | `/8` — slow |
| `GV_NearPhase(from,to)` | wraps angles correctly via DiffDirS |
| `GV_NearRange(from,to,range)` | step toward `to` clamped by `±range` |
| `GV_NearSpeed(from,to,range)` | speed-aware step |
| `GV_NearTime(from,to,interp)` | linear with `interp` denominator |

The `*P` variants (Exp2P etc.) are *post-clamp* — they round to
nearest integer to avoid sub-unit oscillation.

### Vector variants

`*V` versions take pointer-to-shorts and a count, applying the same
operation across N elements. Used for joint-angle smoothing in
`MOTION_CONTROL`.

```c
GV_NearExp4V(work->joint_angles_current, work->joint_angles_target, 16);
// Pulls all 16 joint angles 25% closer to target each tick.
```

### Use pattern

```c
work->facedir = GV_NearPhase(work->facedir, target_face_dir);
// Smoothly turn the actor's facing toward target_face_dir.
```

Most enemy "look at player" code uses `GV_NearPhase` once per frame
with the desired angle.

## `math_quat.c` — quaternion helpers

A small set of quat-mul / lerp helpers used by the cutscene camera
system. Only ~50 lines; the bulk of camera math is in the GTE matrix
ops.

## `strcode.c` — the hash function

```c
int GV_StrCode(const char *s);
```

Returns a 16-bit hash used as the actor name / cache ID basis. The
algorithm:

```c
int hash = 0;
for each char c in s:
    hash = (hash << 5) - hash + c;     // hash * 31 + c
return hash & 0xFFFF;
```

Same family as DJB / Java `String.hashCode` truncated to 16 bits.

Used by:

- `GV_CacheID2(name, ext)` — to build a cache key.
- Actor address — `GV_StrCode("WATCHER") = 0xC356` is the address
  that `GV_ReceiveMessage` matches against.
- GCL bytecode constants — every `&NAME` literal in a GCL script
  expands to its strcode at compile time.

The port has tooling (`tools/mgs_tools/common/strcode.py`) that
mirrors this exact algorithm so Python-side tools can pre-compute
the same hashes the original game uses.

## Pitfalls

- **`GV_RandU` requires power-of-2 input** — masks with `n-1`. Pass
  non-power-of-2 and you'll get a buggy distribution.
- **Angles wrap; subtractions don't.** Always use `GV_DiffDir*`
  for "is angle A close to angle B".
- **`SquareRoot0` returns 0 for negative input.** Don't pass it
  negative dot products; clamp first.
- **`rsin`/`rcos` return 12-bit fixed; multiplying then dividing by
  4096 is the only correct chain.** Watch for missed `>>12`.

---

## Port notes

`math.c`/`math_near.c`/`strcode.c` run unmodified. `math_quat.c`
relies on GTE behaviour that the port emulates in software (see
[`port/libdg/`](../../../../port/libdg/)).

## See also

- [03-control-and-motion.md](../03-control-and-motion.md) —
  `GV_NearPhase` is heavily used in motion smoothing.
- [`source/libdg/matrix.c`](../../../../source/libdg/matrix.c) —
  matrix-level math built on these primitives.
