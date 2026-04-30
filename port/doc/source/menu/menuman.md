---
file: source/menu/menuman.c + item.c + weapon.c + datasave.c
---

# `menu/menuman.c` — pause menu

The in-game pause menu. Owns the four-tab interface (item / weapon /
map / option) and the per-tab UI flow.

## Lifecycle

```
press START
  ↓
GM_TogglePauseScreen()                 (in gamed.c)
  ↓
GV_PauseLevel |= PAUSE_BIT_GAME (= 2)
spawn MENU_MAN actor                    ← menuman.c
  ↓
menu actor consumes input, draws tabs
  ↓
on close: GV_PauseLevel &= ~2
DestroyActor(MENU_MAN)
```

`PauseLevel & 2` freezes gameplay tiers (level 4–6 actors); UI
tiers (level 1–2) keep ticking.

## Tab layout

| Tab | File | Content |
| --- | ---- | ------- |
| ITEM | `menu/item.c` | Grid of pickup items, equip/use buttons |
| WEAPON | `menu/weapon.c` | Grid of weapons, equip + ammo info |
| MAP | (in `menuman.c`) | Top-down stage map with player marker |
| OPTION | (in `menuman.c`) | Sub-menu: button mode, vibration, language, etc. |

L1/R1 cycles between tabs; D-pad navigates the grid; CIRCLE
selects, CROSS exits.

## Sub-menu states

```c
enum {
    MENUMAN_STATE_OPENING,    // tab fade-in
    MENUMAN_STATE_NORMAL,     // grid navigation
    MENUMAN_STATE_DETAIL,     // detail panel for selected entry
    MENUMAN_STATE_CONFIRM,    // "use" / "drop" yes/no
    MENUMAN_STATE_CLOSING,    // fade-out
};
```

State machine transitions on button presses; each state has its
own draw + input handler.

## Item tab (`menu/item.c`)

- Reads `GM_HaveItem(id)` to fill the grid (only items with count
  > 0 are shown).
- Highlight cursor moves through visible items.
- CIRCLE on a pickup item triggers `GM_UseItem(id)` if applicable
  (rations, MO disc), otherwise just equips.
- Uses face textures from `radiotex.c` for icons.

## Weapon tab (`menu/weapon.c`)

- Reads `GM_HaveWeapon(id)` similarly.
- Shows current ammo / capacity per weapon.
- CIRCLE equips the weapon (writes to `GM_EquippedWeapon`).
- Some weapons have an *attachment* sub-grid (suppressor, scope).

## Save tab (`menu/datasave.c`)

The save-slot picker — accessed through OPTION → SAVE during
pause, or via dedicated save points (radio call from Mei Ling).

- Lists 15 memcard slots.
- Cursor-driven; CIRCLE confirms.
- Calls `memcard.c::MC_SaveData(slot, &saveBuffer)` which
  serialises GCL vars + inventory + map state.

## Render path

All menu UI draws to **`DG_Chanl(1)`** (the HUD channel) which has
an identity-ish projection — pixel coordinates land where expected.
The channel is rendered after the 3D scene, so it overlays.

Text uses `font/` rasteriser; backgrounds + borders are DG_PRIMs
allocated per-frame.

## Pitfalls

- **Pause level bit 2 only.** Don't set bit 1 (codec-pause) from
  here — codec freeze is different.
- **Menu doesn't pause the codec.** A codec call running concurrently
  with the menu is still active; `radio.c` ticks at level 1.
- **Navigation cursor is per-tab.** Don't share cursor index across
  tabs.

## See also

- [02-game-loop.md](../02-game-loop.md) — `GV_PauseLevel` semantics.
- [`source/menu/radio.c`](index.md) — codec system (separate path).
- [`source/font/`](../font/index.md) — text rasteriser.
- [`source/memcard/`](../memcard/index.md) — save backend.
