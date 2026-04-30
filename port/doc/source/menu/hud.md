---
file: source/menu/{life,radar,debug,jimaku}.c
---

# `menu/` — HUD overlays

The always-on game UI: life bar, radar, subtitles, debug.

## `life.c` — life + stamina bars

The two horizontal bars in the top-left.

- Reads `GM_PlayerLife` and `GM_PlayerStamina` (or alert version).
- Bar colour:
  - Life: green → yellow → red as it depletes.
  - Stamina: blue (decay) / yellow (regenerating from rations).
- Renders to `DG_Chanl(1)` (HUD).

Bar length is a fixed pixel value scaled by `current/max`. A small
"crosshatch" texture overlays for the depleted portion.

The bars hide during cinematics (`GM_GameStatus & STATE_CINEMA`)
and during pause (`GV_PauseLevel & 2`).

## `radar.c` — radar minimap

The corner mini-map showing guards + vision cones.

### Layout

- Circle clipped to the corner — diameter ~64 pixels.
- Player at centre, north up (rotates as player rotates).
- Each guard rendered as a coloured dot:
  - Green: peaceful patrol
  - Yellow: caution
  - Red: alert
- Vision cone rendered as a pie-slice from each guard.

### Per-frame update

```
for each WatcherWork in active list:
    project guard pos to radar (subtract player, rotate)
    draw dot + vision cone
end
```

The cone uses `vision.facedir / .angle / .length` — exposed by
each watcher's GCL params.

### Hide conditions

- Alert-state-driven: red border flashes during ALERT.
- Hidden during cinematics, codec, pause.
- Replaced with a "soliton scan" effect during the radar-jamming
  sub-stages.

## `debug.c` — debug overlays

A debug-only set of overlays. Toggled by debug menu / dip switches:

- Coordinate display (player x/y/z + facedir).
- TARGET hitboxes drawn as wireframe boxes.
- HZD floor mesh wireframe.
- Frame-time graph.

Compiled out in RELEASE builds; PORT_BUILD keeps them and has an
ImGui menu to toggle.

## `jimaku.c` — JIMAKU subtitles

The bottom-of-screen subtitle text used during cinematics + key
gameplay moments. Separate from codec subtitles (`radiomes.c`).

```
JIMAKU actor at GV_ACTOR_LEVEL2
   ↓
read text + duration from current GCL command
draw text in DG_Chanl(1) at bottom of screen
fade in/out over `duration_frames` * 0.1
```

Triggered by GCL command `jimaku TEXT_ID DURATION`. Text strings
are stored per-stage in the GCX's string-data block.

The jimaku actor self-destroys when its text expires.

## See also

- [menuman.md](menuman.md) — the pause menu.
- [codec.md](codec.md) — the codec sub-system.
- [`source/font/`](../font/index.md) — text rendering.
- [`source/game/alert.md`](../game/alert.md) — drives radar
  alert-state colours.
