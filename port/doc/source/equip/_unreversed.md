---
file: source/equip/ — opaque areas
---

# `equip/` — opaque areas

`equip/` is small — wearable items (suits, goggles, masks, etc.).
Decompiled.

## Per-equip stat tables

Each equippable has hard-coded modifier values:

- BANDANA: infinite ammo
- CAMO_*: stealth modifier
- BODY_ARMOR: damage reduction %
- IR_GOGGLES: enable IR vision

The values are present in code but not centrally tabulated. A
`equip_stats.md` would be useful but doesn't exist.

## Equip slot count

The player has multiple "slots" (head / body / accessory) but the
exact count and per-slot constraints aren't formally typed.

## Activation timing

Some equipments take effect immediately; others gate on player
state (BANDANA only valid after game-clear flag). The full gating
logic is per-item; not aggregated.

## See also

- [README.md](README.md) — file map.
- [`source/menu/item.c`](../menu/menuman.md) — equip UI.
