---
file: source/libdg/matrix.c
---

# `libdg/matrix.c` — matrix helpers

Per-frame matrix math built on top of GTE primitives. Most callers
invoke these from screen-stage / control-update code.

## Functions

| Fn | Action |
| -- | ------ |
| `DG_MatrixRot(mat, svec)` | Build YXZ rotation matrix from Euler triple |
| `DG_MatrixRotYXZ(mat, svec)` | Same, explicit YXZ ordering |
| `DG_MatrixRotZYX(mat, svec)` | ZYX-ordered rotation (used for camera) |
| `DG_TransposeMatrix(in, out)` | Transpose 3x3 — used for inverse-rotation matrix |
| `DG_ShadowMatrix(out, in, p3)` | Build a flatten-onto-floor matrix |
| `DG_ReflectVector(in, t, out)` | Reflect a SVECTOR across a plane |
| `DG_ReflectMatrix(svec, in, out)` | Reflect entire matrix (for mirror surfaces) |

## Rotation order convention

The engine uses **YXZ** (yaw, pitch, roll) for character orientation:
the player's `facedir` is the Y rotation; pitch is X; roll Z.

Cameras use **ZYX** instead (roll, then pitch, then yaw) — see
`DG_MatrixRotZYX`. This matches the cinematic camera's DMO data
which stores ZYX-ordered Euler triples.

Mixing the two (using YXZ for a camera or vice versa) gives subtly
wrong rotation in non-axis-aligned cases.

## `DG_ShadowMatrix(out, in, height)`

Used for character ground shadow rendering — produces a matrix that
projects the character down onto Y = floor + height. Outputs a matrix
that can be applied to the body OBJS to render a flattened black
quad of the character's silhouette on the floor.

The third parameter is the ground Y; the second arg's translation
becomes the shadow's anchor point.

## `DG_ReflectMatrix(plane_normal, in, out)`

Reflects a matrix across an arbitrary plane. The plane_normal is
the normalised SVECTOR perpendicular; the plane passes through the
origin. Used by certain effect actors (water reflections,
mirror-room cinematics) but rare in shipped stages.

## GTE inlines (libdg.h)

```c
#define DG_MulRotMatrix0(r1, r2)      // mul rot without GTE state mutation
#define DG_CompMatrix(r1, r2)         // compose matrices in GTE
```

These are *macros* using inline GTE assembly that don't touch the
GTE's "current" matrix register — necessary when composing per-OBJ
chains where state has to be preserved.

## Pitfalls

- **GTE matrices are 3x4 (3x3 rotation + 3 translation).** The
  PSX `MATRIX` struct stores rotation in `m[3][3]` and translation
  in `t[3]`. Don't read `m[3]` — there is no fourth row.
- **Rotations are in 12-bit fixed.** `svec->vx = 0x800` is 180°.
  Pass values outside [-2048, 2048) and `rsin`/`rcos` wrap (which
  is correct mathematically but beware sign).
- **Transpose only works for pure-rotation matrices.** If the
  matrix has translation, transpose alone won't give you the
  inverse — you need to also negate-and-rotate the translation.

## Port notes

The port emulates GTE `gte_*` macros in software (see
`port/libdg/gl_renderer.c`). The matrix helpers themselves run
unmodified.

## See also

- [pipeline.md](pipeline.md) — uses these helpers through stage 0
  (screen).
- [`source/libgv/math_quat.c`](../../../../source/libgv/math_quat.c) —
  quat representation also used for matrix inputs.
