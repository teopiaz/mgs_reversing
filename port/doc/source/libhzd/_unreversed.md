---
file: source/libhzd/ — opaque areas
---

# `libhzd/` — what's still by-address / opaque

`libhzd/` is fully decompiled. Most opacity is in the *file format*
side (`fmt_hzd.h` types) — the HZD authoring tool predates the
public source release and the field semantics aren't all named.

## `HZD_BIND` field semantics

```c
short   field_0;             // entity name              ✓
short   field_2_param_m;     // mask                     ✓
short   field_4;             // trap name                ✓
u_short map;                 // map id                   ✓
u_char  field_8_param_i_c_flags;     // ?               opaque
u_char  field_9_param_s;             // ?               opaque
u_char  field_A_param_b;             // ?               opaque
u_char  field_B_param_e;             // ?               opaque
u_short field_C_param_d;             // ?               opaque
u_short field_E_param_d_or_512;      // ?               opaque
int     field_10_every;              // re-fire every N  ✓
int     field_14_proc_and_block;     // proc id + flags  ✓
```

The `_param_X` suffixes correspond to GCL bind option letters
(`-i`, `-c`, `-s`, `-b`, `-e`, `-d`) but the exact field layout
isn't rigorously typed.

## `HZD_TRP` (trap)

The trap struct (`HZD_TRP *traps;` in `HZD_HDL`) is partially typed
in `fmt_hzd.h` but several fields are still opaque. Traps aren't
fully distinguished from events; the engine uses both
interchangeably in some paths.

## Bind firing order

Binds are fired in *registration order* — the order they appeared
in the GCL. This is observable but undocumented; some stage GCLs
rely on it (e.g. setup binds must register before clear binds).

## `HZD_LineCheck` private state

The `LineNearSurface` / `LineNearFlag` / `LineNearDir` /
`LineNearVec` getters all read from a single global last-hit
record. The exact lifetime of that record (cleared on next
LineCheck? on stage change?) is observable but not documented.

## Hazard flags

`HZD_LevelTestHazard(hzd, point, flags)` — the meaning of `flags`
is per-stage. Some bits represent "lava floor", some "electrified",
some custom. There's no central enum; each stage tags floors with
arbitrary bits and the gameplay code matches them up.

## Dynamic-flag size

`dynamic_flags` is a `char *` of size `max_dynamic_segments`. The
exact bit layout per byte (which bits are flags, which are state)
isn't formally documented — empirically only a few low bits are
used.

## Camera count

`HZD_HDL.n_cameras` exists but its consumer isn't well understood —
might be related to the per-stage camera-zone table.

## `HZD_ExecBindX` arg layout

`HZD_ExecBindX(bind, event, phase, arg2)` — what `arg2` represents
varies by call site. Could be a "secondary entity" (the *other*
participant in an interaction), but isn't formally typed.

## See also

- [index.md](index.md), [collide.md](collide.md), [level.md](level.md),
  [zone.md](zone.md), [event.md](event.md), [dynamic.md](dynamic.md)
  — documented surfaces.
- [`source/include/fmt_hzd.h`](../../../../source/include/fmt_hzd.h)
  — the file-format header.
