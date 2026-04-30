---
file: source/stage/ — opaque areas
---

# `stage/` — opaque areas

`stage/` is the per-stage overlay code. Each stage (`s00a`, `s01a`,
…) gets its own folder with its own `.c` files. ~50 stages × 5–20
files each.

## What's in each stage

Each per-stage folder has:

- `chartbl.c` — stage-private CHARA table (extends `MainCharacterEntries`)
- One or more `s##_*.c` files for stage-specific actors
- Init function `s##_Init()` that sets up the stage

## Stage-private actor variants

Many guards have stage-specific tweaks (`s07a_meryl7_*`, see
[enemy/meryl7.md](../enemy/meryl7.md)). These are NOT
documented per-stage — too many of them, mostly small diffs from
shared code.

## Magic constants

Each stage has its own pile of hard-coded positions, timing
values, message hashes. Untyped, address-named (`s01a_dword_800XXXXX`).

## Per-stage GCL

Each stage has a `scenerio.gcx` script that drives spawning. These
have been decompiled to text in `port/gcl/decompiled/<stage>/` but
the *content* isn't documented — script-by-script analysis would
be a multi-month effort.

## Status

This folder is comprehensively decompiled (matches PSX byte-by-byte)
but per-stage *narrative* documentation doesn't exist. A future
pass could write per-stage walkthroughs covering:

- What actors are spawned at boot.
- Which sub-areas exist and how transitions work.
- Which cinematics fire and when.
- Which events have unique handlers.

## See also

- [index.md](index.md) — file map.
- [`port/gcl/decompiled/`](../../../../port/gcl/decompiled/) —
  per-stage GCL in readable text form.
- [`source/enemy/meryl7.md`](../enemy/meryl7.md) — example of
  per-stage actor variant.
