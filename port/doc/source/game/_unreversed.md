---
file: source/game/ — opaque areas
---

# `source/game/` — what's still by-address / opaque

`game/` is fully decompiled and matches PSX byte-exact. The
remaining ambiguities:

## CONTROL field placeholders

Some `CONTROL` and `MOTION_CONTROL` fields still have generic names:

- `field_X` placeholders in older copies — most are now named.
- The exact semantics of `time2` / `count2` ↔ `time` / `count` are
  context-dependent (they're scratch counters re-used by
  individual actors).

## TARGET edge cases

`GM_Target_8002DCCC` and `GM_Target_8002DCB4` are still address-
named (just `Target_*` for their PSX address). They configure
damage/faint params; the parameter list is decoded but the names
of each param are still terse (`life`, `faint`, `force_*` —
several sub-fields opaque).

## Camera: `gUnkCameraStruct2`

A second camera struct exists alongside `GM_Camera`. Used by DMO
playback and some special cinematic modes. The exact split between
the two (which one drives `DG_LookAt` when?) is observable but not
fully documented.

## evpanel.c — event panel

1048 lines registering event-handler callbacks for every
`HZD_EVT.name`. Each handler has a strcoded id; the full list of
ids isn't enumerated — they're discovered case-by-case by grepping.

## Lamp / point-light system

`lamp.c` (390 lines) handles cinematic point lights, but the runtime
lifetime + per-frame integration with `DG_SetTmpLight` is partially
opaque.

## tobcnt.c

627 lines, name unclear (maybe "to-be-counted"?). Implements some
counter-actor used during cinematics. Unreversed semantics.

## sound.c BGM table

The list of (alert_level → song_id) mappings is hard-coded in
`sound.c`. Fully readable but not formally documented.

## strctrl.c — "strange control"

281 lines. A special control mode for rope-climbing / prone-fire.
The full state machine is decompiled but the transition triggers
are subtle (multi-button combos + context).

## script.c command set

While the *categories* are documented, some individual commands
(particularly debug-only ones) have terse names and unclear
arguments. Examples: `chrtbl`, `tobctl`, `2ndctl`.

## See also

- [index.md](index.md), [chara.md](chara.md), [camera.md](camera.md),
  [target.md](target.md), [item.md](item.md), [script.md](script.md),
  [alert.md](alert.md), [map.md](map.md) — documented surfaces.
