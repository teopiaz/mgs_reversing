---
file: source/game/item.c
---

# `game/item.c` — inventory system

Owns the player's carried-items list, pickup logic, equip / use
state, and the item ↔ menu bridge. 912 lines.

## Item types

Items are either *weapons* (FAMAS, SOCOM, …) or *items* (rations,
MO disc, key card …). Different inventory slots, different menu
tabs.

```c
typedef struct ITEM_ENTRY {
    short  id;             // strcoded name
    short  count;          // # in inventory (or -1 = not held)
    short  capacity;       // max for this item type
    short  flags;
} ITEM_ENTRY;
```

The whole inventory is a fixed-size array `ITEM_ENTRY items[N]`,
indexed by item id.

## API

```c
void  GM_InitItem(void);
int   GM_GiveItem(int item_id, int count);     // add to inventory
int   GM_DropItem(int item_id, int count);      // remove
int   GM_HaveItem(int item_id);                 // count or -1
void  GM_EquipItem(int item_id);                // set as held
int   GM_GetEquippedItem(void);
void  GM_UseItem(int item_id);                  // trigger use callback
int   GM_GetItemCapacity(int item_id);
```

## Use callbacks

Each item id has a registered use-callback:

```c
typedef int (*ITEM_USE_FN)(int item_id);

void GM_RegisterItemUse(int item_id, ITEM_USE_FN fn);
```

Examples:

| Item | Callback |
| ---- | -------- |
| RATION | restore life by N, decrement count |
| MEDICINE | heal cold/poison status |
| MO_DISC | trigger plot event in s11d |
| KEY_CARD | check current door's auth level, open if matched |

## Pickup detection

Items in the world are spawned by `chara &KEY_ITEM -i ID -p ...`.
The actor (`okajima/key_item.c`) has a TARGET_TOUCH that the player
collides with; on collision, calls `GM_GiveItem(id, 1)` and destroys
itself.

## Save / load

The inventory array is serialised to memcard. Save format:

```
[item_count u16][ITEM_ENTRY entries[N]]
```

`memcard.c` knows the offset; restore writes back.

## Weapon-specific quirks

Weapons are items but have an extra `ammo` field stored separately
in `weapon.c` per weapon type (each weapon has its own ammo struct).

The pickup chain:

```
WORLD: chara &SOCOM_PICKUP -p ...
TOUCH: GM_GiveItem(SOCOM, 1)
       GM_RegisterItemUse(SOCOM, weapon_use_socom)
USE:   weapon_use_socom() → GM_EquipItem(SOCOM) + load weapon
```

## Pitfalls

- **GM_HaveItem returns -1 for "not held".** Don't compare
  `GM_HaveItem(x) > 0` — use `>= 0` or check against -1.
- **Capacity vs count.** Capacity is the max; count is current.
  Adding past capacity caps silently.
- **Weapons need both inventory + ammo init.** Just `GM_GiveItem`
  the weapon doesn't fill ammo. Call into `weapon.c::GM_GiveAmmo`
  separately.

## See also

- [`source/menu/item.c`](../menu/index.md) — UI for the items
  tab.
- [`source/menu/weapon.c`](../menu/index.md) — UI for the
  weapons tab.
- [`source/equip/`](../equip/index.md) — wearable items.
- [`source/okajima/key_item.c`](../okajima/index.md) — pickup
  actor.
