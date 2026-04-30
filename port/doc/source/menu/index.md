# `source/menu/` — UI overlays (HUD, menus, codec, subtitles)

Everything 2D the player sees on top of the 3D scene. Inventory,
codec, save-load, life bar, radar, subtitles, debug overlays.

## Files

### Main UI
| File | Role |
| ---- | ---- |
| [`menuman.c`](../../../../source/menu/menuman.c) | "Menu manager" — owns the in-game pause menu (item / weapon / map / option tabs). |
| [`item.c`](../../../../source/menu/item.c) | Item-tab UI — grid of pickup-able items, equip / use logic. |
| [`weapon.c`](../../../../source/menu/weapon.c) | Weapon-tab UI — same but for firearms. |

### HUD
| File | Role |
| ---- | ---- |
| [`life.c`](../../../../source/menu/life.c) | The life bar + stamina bar overlay. |
| [`radar.c`](../../../../source/menu/radar.c) | Radar (the corner mini-map showing guards + vision cones). |
| [`debug.c`](../../../../source/menu/debug.c) | Debug overlays — coordinate display, hitbox visualisation. |

### Save / load
| File | Role |
| ---- | ---- |
| [`datasave.c`](../../../../source/menu/datasave.c) | Save-slot picker UI. |

### Codec (radio call)
| File | Role |
| ---- | ---- |
| [`radio.c`](../../../../source/menu/radio.c) | Codec-call dispatcher — RADIO chara handler, voice queue. |
| [`radioanim.c`](../../../../source/menu/radioanim.c) | Codec animation (lip flap, blink). |
| [`radiofacedraw.c`](../../../../source/menu/radiofacedraw.c) | Portrait rendering — the side-of-screen face. |
| [`radiomem.c`](../../../../source/menu/radiomem.c) | Memory-frequency UI (the "press SEL to remember" prompt). |
| [`radiomes.c`](../../../../source/menu/radiomes.c) | Subtitle / message text rendering during a codec call. |
| [`radiotable.c`](../../../../source/menu/radiotable.c) | The radio frequency / contact name table (Snake's contacts). |
| [`radiotex.c`](../../../../source/menu/radiotex.c) | Codec texture management (which face textures are loaded). |
| [`face.h`](../../../../source/menu/face.h) | Face portrait struct. |

### Subtitle
| File | Role |
| ---- | ---- |
| [`jimaku.c`](../../../../source/menu/jimaku.c) | JIMAKU (subtitle) actor — the bottom-of-screen text. |

## Render path

Most menu UI draws to `DG_Chanls[1]` (the HUD channel) which has
an identity-ish projection — pixel coordinates land where
expected. The channel is rendered after the 3D scene, so it
overlays.

The font system (`source/font/`) is what rasterises text. The
codec / JIMAKU lookup their text from radio dialogue tables in
RADIO.DAT.

## Pause menu

`menuman.c` is the pause-menu state machine:

```
press START → MENU_StartPause → GV_PauseLevel |= 2
                              → spawn menu actor
                              → menu actor consumes input
                              → on close: GV_PauseLevel &= ~2
```

The four tabs cycle via L1/R1 within the menu actor.

## Codec system

Codec calls are surprisingly elaborate — they take input
(player can press buttons during a call), drive lip-flap from
the active voice's amplitude, render a portrait that scrolls in
from the side. Multiple files because:

- `radio.c` orchestrates.
- `radioanim.c` runs the per-frame face animation.
- `radiofacedraw.c` does the actual quad submission.
- `radiomes.c` types out the subtitle text character-by-character.
- `radiomem.c` handles the "remember frequency" UI.
- `radiotable.c` is the contact directory.
- `radiotex.c` swaps face textures as different speakers join.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [menuman.md](menuman.md) | Pause menu — tabs, item/weapon/save grid, lifecycle |
| [codec.md](codec.md) | Codec system — radio.c + radioanim/face/mes/mem/table/tex |
| [hud.md](hud.md) | Life bar, radar, debug, JIMAKU subtitles |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [02-game-loop.md](../02-game-loop.md) — `GM_TogglePauseScreen`
  is gamed.c's entry point into the pause menu.
- [`source/font/`](../font/index.md) — text rasteriser used by
  every UI element.
