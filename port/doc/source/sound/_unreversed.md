---
file: source/sound/ — opaque areas
---

# `source/sound/` — opaque areas

The sound stack is mostly decompiled but several internal globals
and SPU register sequences are still address-named.

## `sd_drv.c` — SPU register layout

The driver writes to PSX SPU registers (0x1F801C00..0x1F801E00).
The exact register sequences for some envelopes (release decay,
sustain rate) are reverse-engineered from PSYQ behaviour and not
fully verified against the official docs — works in practice but
edge cases (very-short notes) may differ.

## `sd_main.c` task list

The "main pump" runs ~5 sub-tasks in a fixed order; the priority
ordering and yield points are observable but not formally
documented. Reorder = audio stutter.

## `se_tbl.c`

The sound-effect ID → (sample, pitch, volume) mapping table. Each
SE is a tuple but the field meanings (especially `flags` and
`pan_pattern`) aren't fully named.

## Sequence playback (`sd_main.c::SDD_Play_Seq`)

The note-events table format inside `.seq` files is the original
PSYQ format. Documented in PSYQ docs but no wrapper writeup — the
port's [`port/sound.c`](../../../../port/sound.c) reads it directly.

## Reverb send

The PSX SPU has a global reverb unit; some game-tier code sets the
reverb amount via a side-channel that's been observed but the
exact register sequence is `sd_ioset_reverb_*` functions whose
parameter mapping is unclear.

## VRAM ring buffer offsets

`sd_str.c` keeps per-voice ring offsets in a static array, but
the array layout (separate left/right channels? interleaved?) is
not formally documented.

## See also

- [index.md](index.md), [architecture.md](architecture.md) —
  documented surfaces.
- [`port/sound.c`](../../../../port/sound.c) — port replacement.
- [reference_testvox.md](#) — working VOX/codec testbed.
