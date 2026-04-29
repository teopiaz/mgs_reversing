---
file: source/mts/ — opaque areas
---

# `mts/` — opaque areas

The MTS scheduler is decompiled. Several internals are abstract:

## mts_new vs older

There's a "new" task system (`mts_new.c`) and a deprecated older
one. The diff is observable but the migration history isn't
documented — some files reference both APIs.

## Task priority levels

The 6-task setup runs at fixed priorities, but the exact priority
numbers (vs PSX hardware IRQ levels) aren't formalised.

## Stack sizes

Each task has a fixed stack size. The values are observable but
rationale isn't documented — some are bigger than they need to be
(safety margin?).

## V-blank timing

`mts_wait_vbl(n)` blocks until N V-blanks. The exact relationship
between V-blank and the PSX `RCntCNT*` counters depends on PAL/NTSC
mode and isn't centrally captured.

## Port replacement

`port/mts.c` stubs all tasks to no-op or single-callback. The
threading semantics are completely flattened. This means certain
PSX behaviours (overlapping tasks) can't reproduce on the port.

## See also

- [README.md](README.md) — file map.
- [`port/mts.c`](../../../../port/mts.c) — port replacement.
