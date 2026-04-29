---
file: source/libgv/ — opaque areas
---

# `libgv/` — what's still by-address / opaque

`libgv/` is the most thoroughly reversed subsystem (it's the engine
backbone — every actor depends on it). What remains opaque:

## Heap-internal globals

| Symbol | Likely meaning | Where used |
| ------ | -------------- | ---------- |
| `MemorySystems_800AD2F0` | The `GV_HEAP[3]` array | All of `memory.c` — known but address-named |
| `dword_800AB93C` | unused short in `.sbss` | `memory.c` — possibly ex-ECC field |
| `dword_800AB92C` | unused; in `actor.c` | unknown |
| `gActorsList_800ACC18` | `ActorList[GV_ACTOR_LEVEL]` (= 9 entries) | All of `actor.c`; the canonical list — known but still address-named |
| `gPauseKills_8009D308` | `PauseKill[GV_ACTOR_LEVEL]` initialiser | `actor.c` |

## Cache: 32-bit ID layout

The 32-bit `GV_CACHE_TAG.id` packs:

```
bit 24       : RESIDENT_FLAG
bits 23..16  : extension (lowercase 0..25, uppercase 26..51)
bits 15..0   : strcode hash low 16 bits
```

Bits 25..31 are *reserved* but never decoded — the field is set to
zero on all known paths. Could be a future "version" bit, or just
header padding.

## Pad system

`GV_800AB37C` and `dword_800AB950`/`dword_800AB954` are static
counters in `pad.c` whose meaning is unclear. The `OriginPadSystem`
mechanism uses them to track frames-since-takeover, but the exact
counting semantics aren't pinned down.

`GV_DemoPadAnalog` is a `u_long` packing left-stick X/Y for VR demo
playback; the format (just two bytes packed?) is reverse-engineered
empirically — no doc.

## Math

Several `GV_Near*` variants (`GV_NearTimeP`, `GV_NearTimePV`) are
implemented but not called from any current overlay — possibly used
by removed code. Their exact tuning curves haven't been written up.

## resident.c

`resident.c` is so small it's already documented inline; no opaque
areas.

## Math quat

`math_quat.c` is 49 lines, two functions. The exact convention
(left- vs right-handed, w-component sign) needs verification before
any port-side rewrite.

## Pad: dword_800B05A8[6]

Used by `GV_UpdatePadSystem` for state but the layout (6 ints) isn't
documented — looks like `[prev_status[2], prev_press[2], …]` for the
edge-detector but not formally typed.

## Message: `dword_800AB94C`

Static int in `.sbss`, in `message.c`. Always 0 in dumps. Possibly
removed feature.

## See also

- [README.md](README.md), [actor.md](actor.md), [memory.md](memory.md),
  [cache.md](cache.md), [message.md](message.md), [pad.md](pad.md),
  [math.md](math.md) — the documented surfaces.
