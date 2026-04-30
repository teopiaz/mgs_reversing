---
file: source/thing/ — opaque areas
---

# `source/thing/` — opaque areas

Decompiled. Field-level details:

## door.c

The "auth" parameter (`-a` GCL flag) is a string that compares
against the player's inventory. The exact comparison logic
(string match? hash compare?) and the canonical id list (`L1`,
`L2`, `KEY_CARD_A`, etc.) aren't formally enumerated.

The door's open/close timing curves are hard-coded magic numbers.

## emitter.c

`emitter_spawn_one` is a callback selected by `effect_id`; the
mapping from `effect_id` to which constructor is called isn't
fully tabulated.

## sight.c

The cone-vs-point test uses dot products with hard-coded
normalisation. Edge case: when the cone is exactly axis-aligned
the test gives ambiguous results — observable but not documented.

## sgtrect3.c

Volumes are axis-aligned only; rotated rectangles aren't supported.
Some stage GCLs apparently emit rotated rectangles that get
treated as their AABB approximation — slight gameplay deviation.

## sphere.c

Sphere radius is single-int but the units aren't formalised — some
stages encode in PSX-units, some appear scaled.

## See also

- [index.md](index.md), [things.md](things.md) — documented
  surfaces.
