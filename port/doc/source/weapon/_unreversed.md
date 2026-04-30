---
file: source/weapon/ — opaque areas
---

# `weapon/` — opaque areas

Weapons are decompiled but per-weapon balance constants are not
formally documented.

## Per-weapon damage tables

Each weapon has hard-coded damage / faint values:

| Weapon | Damage | Faint | Range | Spread |
| ------ | ------ | ----- | ----- | ------ |
| SOCOM | ? | 0 | mid | small |
| FAMAS | ? | 0 | mid | medium |
| PSG-1 | high | 0 | long | none |

Values present in code but not auditable from one place.

## Stun-rifle special case

`stnsight.c` causes `faint` damage instead of `life` damage. The
exact mechanism (TARGET_POWER mode? separate force vector?) is
embedded in the actor and worth a deep diff.

## RC-M control-mode

The player-pause mechanism while controlling RC-M is implemented
via setting `control->attribute` to a special value. Exact
attribute number opaque.

## Weapon table

Each weapon is registered in a master table that maps weapon-id to
(constructor, ammo capacity, KMD, sound). Table is in
`game/item.c` but the per-entry layout isn't formally typed.

## See also

- [index.md](index.md), [weapons.md](weapons.md) — documented
  surfaces.
- [`source/bullet/`](../bullet/index.md) — projectile internals.
