---
file: source/kojo/ — opaque areas
---

# `kojo/` — opaque areas

Kojo's per-author folder houses streamed cinematics + special-purpose
actors. Most are decompiled with stage-specific magic constants.

## Streamed-cinematic format

`kojo/` contains code for "streamed" cinematics — cinematics with
pre-rendered FMV plus interactive elements. The streaming format
(how FMV and game state interleave) is partially documented but
the per-frame command stream isn't formally typed.

## Per-actor magic constants

Several actors use hard-coded position / timing values that look
like hand-tuned magic. Not documented.

## Cross-references with takabe

Some functionality overlaps with `takabe/` (cinematic helpers).
The split between the two folders is by author, not by
responsibility — same job done two ways.

## See also

- [README.md](README.md) — file map.
- [`source/takabe/effects.md`](../takabe/effects.md) — sister
  folder.
