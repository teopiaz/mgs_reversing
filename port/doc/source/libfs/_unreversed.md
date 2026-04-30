---
file: source/libfs/ — opaque areas
---

# `libfs/` — opaque areas

`libfs/` is small and decompiled. Remaining unknowns are in the
*disc-format* side rather than the API side.

## DAR header layout

The DAR (data archive) header has fields whose exact width is
inferred from values, not from a published spec. The port re-derives
the layout from the disc image — a different DAR variant might not
be parsed correctly.

## Sector-read alignment

PSX CD reads are 2048-byte sectors. libfs assumes XA Mode 2 Form 1.
Other modes (Mode 1, Mode 2 Form 2) aren't tested — could break on
some discs.

## Async read completion order

The async API (`FS_ReadFileAsync`) doesn't formally guarantee FIFO
completion. The release relies on observed behaviour but doesn't
sort.

## See also

- [index.md](index.md), [streaming.md](streaming.md) — documented
  surfaces.
- [`port/libfs/libfs.c`](../../../../port/libfs/libfs.c) — port.
