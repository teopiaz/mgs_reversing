---
file: source/libdg/ — opaque areas
---

# `libdg/` — what's still by-address / opaque

`libdg/` is fully decompiled and matches PSX byte-for-byte. The
remaining opacity is in *which fields mean what* and a handful of
intentionally-encoded values.

## Tpage / CLUT format

`DG_TEX::tpage` and `::clut` are pre-computed PSX GPU descriptors.
The exact bit layout:

```
tpage:  bits 0..4   = X tex base (in 64-px units)
        bit  4      = Y tex base low (in 256-px units)
        bits 5..6   = blend mode (00=avg, 01=add, 10=sub, 11=add quarter)
        bits 7..8   = colour mode (00=4bit, 01=8bit, 10=15bit)
        bit  9      = dither
        bits 10..11 = drawing area (clip)
        bit  12     = Y tex base high
clut:   bits 0..5   = X (in 16-px units)
        bits 6..14  = Y
```

Documented but not sanity-checked field-by-field across all live
PCXs — a corrupt PCX could in principle produce a malformed tpage
that the engine still accepts.

## DG_OBJS::flag bits

```
0x0001 TEXT
0x0002 PAINT
0x0004 TRANS
0x0008 SHADE
0x0010 BOUND
0x0020 GBOUND
0x0040 ONEPIECE
0x0080 INVISIBLE
0x0100 AMBIENT
0x0200 IRTEXTURE
0x0400 UNKNOWN_400      ← still opaque
```

`UNKNOWN_400` is set by some KMDs but the consumer is unclear. Could
be additive blending, or a no-cull marker.

## DG_MDL::flags bits

The `flags` field is consulted by `DG_MakeObjs_helper` for the
`raise` calculation:

```
flags & 0x300 → "this model has a depth bias"
flags >> 12 & 3 → bias amount index
flags & 0x100 → bias sign
```

The exact mapping (which mesh authors set which bits) isn't
formalised — bits 8..15 are mostly opaque.

## Pipeline timing

`N_ChanlPerfMax` and `word_800AB982` (in chanl.c) are tied to root-
counter 1, used to measure draw time. The unit (V-sync ticks?
microseconds?) and the exact threshold for "too slow" haven't been
pinned down.

## DG_FreePrim's `DG_PRIM_FREEPACKS`

Bit `0x2000` on a DG_PRIM's type tells the prim freer to also free
the packs. Without it, packs leak. The exact rules for when this
should be set per `DG_PRIM_TYPE` aren't enumerated.

## Light system

```c
DG_TmpLightList     // 8 temporary lights
DG_FixedLight       // permanent scene lights
```

`DG_SetTmpLight(svec, brightness, radius)` returns an index that
the calling actor must remember (no automatic cleanup). The
"clear all temp lights at frame end" mechanism is observable but
the exact cadence (after V-sync? before next frame's shade stage?)
isn't precisely documented.

## Sort

`DG_SortChanl` averages Z over the OBJ vertices but the *which*
vertices sample is opaque — likely `model->pos + obj->screen.t[]`
but the exact arithmetic chain hasn't been verified.

## OAR/NAR archive layouts

Both are documented in `source/include/fmt_mot.h` but the runtime
selection between motion entries (which clip plays at which time)
is in `motionconv.c` (anime/) and not formally captured here.

## See also

- [README.md](README.md), [pipeline.md](pipeline.md), [obj.md](obj.md),
  [matrix.md](matrix.md), [text.md](text.md), [display.md](display.md)
  — the documented surfaces.
- `source/anime/animconv.md` — the bytecode interpreter that
  drives many DG_OBJS state changes.
