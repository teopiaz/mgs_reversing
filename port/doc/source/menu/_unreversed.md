---
file: source/menu/ — opaque areas
---

# `menu/` — opaque areas

Decompiled but several details still terse.

## Pause-menu state IDs

`menuman.c` enumerates ~6 sub-states (OPENING / NORMAL / DETAIL /
CONFIRM / CLOSING / something else); the exact transition matrix is
observable but no symbolic-name table.

## Map tab implementation

The "MAP" tab reads from a per-stage `MAP_DATA` blob whose layout
isn't formally documented — it's parsed inline in menuman.c.
Stages register the data via `GM_SetMapData(...)`.

## Codec face texture cache

`radiotex.c` uses ~32 slots in the resident texture region; the
exact slot allocation policy (LRU? round-robin?) needs auditing.

## Radar jamming sub-state

The "soliton scan" effect during radar-jamming has its own state in
`radar.c` — `radar_jam_phase` advances over time. The exact
animation curve isn't named.

## Jimaku text-id space

JIMAKU IDs are strcoded but the master list of (id → string) lives
in each stage's GCX. There's no central registry; identifying
which strings are jimaku vs codec vs error-print requires
per-context inspection.

## Debug overlay flags

`debug.c` has a global `debug_flags` int; the bit layout (which
bit toggles which overlay) isn't enumerated.

## See also

- [README.md](README.md), [menuman.md](menuman.md), [codec.md](codec.md),
  [hud.md](hud.md) — documented surfaces.
