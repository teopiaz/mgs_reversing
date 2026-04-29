---
file: source/memcard/ — opaque areas
---

# `memcard/` — opaque areas

## Save thumbnail format

The save header includes a 64×96 (or similar) thumbnail rendered
into PSX memcard "icon" format. The exact pixel format / palette
mapping isn't fully documented — port skips thumbnail generation.

## Checksum algorithm

The save checksum is some XOR-based digest. Reverse-engineered;
the polynomial/seed isn't named. Port uses the same algorithm
verbatim so saves verify correctly.

## Per-slot file naming

The "MGS_DATA" string at offset 0 is the slot title shown on the
PSX memcard manager. The port stores files as `0.bin` etc. and
ignores the title.

## Region-locked saves

The PSX memcard format has region-marker bytes; saves from JP
discs may not load on US discs. MGS Integral specifically appears
to be region-permissive — save format hasn't been tested across
all combinations.

## See also

- [README.md](README.md), [savefile.md](savefile.md) — documented
  surfaces.
