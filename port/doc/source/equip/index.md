# `source/equip/` — wearable equipment

Inventory items the player *equips* (vs items consumed). Each is
an actor that runs while equipped and modifies Snake's behaviour
or rendering.

| File | Purpose |
| ---- | ------- |
| `bandana.c` | Bandana — infinite ammo when equipped (post-clear bonus). |
| `bodyarm.c` | Body armor — halves incoming damage. |
| `box.c` | Cardboard box — Snake disappears under a box; guards see only "a box". Several variants (BOX_01..05). |
| `effect.c` | Cross-cutting visual effects shared by equip items. |
| `gasmask.c` | Gas mask — protects against gas-room damage. |
| `gglmng.c` | Goggles — manager / dispatcher to specific goggle types. |
| `gglsight.c` | Goggle sight — first-person aim mode rendering. |
| `gmsight.c` | Gunsight — sniper-rifle scope view. |
| `jpegcam.c` | JPEG camera — Snake's photo equipment. |
| `kogaku2.c` | "Optical" — possibly the IR/thermal optical mode. |
| `scope.c` | Sniper scope (regular). |
| `tabako.c` | Cigarette — Snake's idle smoke (Tabako = タバコ). |

Total ~? lines (most are 100-300 each). Each follows the same
"chara actor that lives while equipped" pattern:

```c
typedef struct EquipWork {
    GV_ACT actor;
    /* …per-equipment state… */
    GV_ACT *parent;       /* Snake actor */
} EquipWork;

void *NewEquipBox(int item, …)
{
    EquipWork *work = GV_NewActor(LEVEL, sizeof(EquipWork));
    /* Wire to Snake's body, set state, return. */
}
```

When the player un-equips (via menu) or starts a stage that
forbids the equipment, the actor receives `HASH_KILL` and dies,
restoring Snake's normal behaviour.

## Cardboard box (`box.c`) — special case

The most loved equip item gets the most code. While equipped:

- Snake's KMD swaps to a box variant.
- Movement speed is reduced.
- The box's TARGET allows shooting *through* (transparent to
  guards' sight cones).
- Special interactions: trucks pick up boxed Snake to other
  stages.

Different box numbers (BOX_01..05) carry different stage labels
that affect which truck destinations the box offers.

## Goggle dispatch (`gglmng.c`)

Goggles are manageable as a unit. `gglmng.c` is the front desk —
it watches `GM_GameStatus` for STATE_NVG / STATE_THERMG and
spawns the right `gglsight.c` overlay actor.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (per-equip stat
  tables, slot constraints).

- [snake.md](../chara/snake.md) — Snake's `current_item` field
  is what tells these actors to spawn.
- [`source/menu/item.c`](../../../../source/menu/item.c) — the
  inventory UI that switches items.
- [`source/weapon/`](../../../../source/weapon/) — distinct from
  equipment: weapons are *fired*, equipment is *worn*.
