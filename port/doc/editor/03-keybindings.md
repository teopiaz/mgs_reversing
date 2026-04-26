# Keyboard and mouse reference

## Camera

| Key                | Action                                                |
| ------------------ | ----------------------------------------------------- |
| `W` / `A` / `S` / `D` | Move forward / left / back / right (camera-relative) |
| `Q` / `E`          | Move up / down (PSX `-Y` / `+Y`)                       |
| `←` / `→` / `↑` / `↓` | Yaw / pitch without using the mouse                |
| `Right-click + drag` | Look (mouse yaw/pitch)                              |
| `Shift` (held)     | 4× movement speed                                     |
| `Ctrl` (held)      | 0.25× movement speed                                  |
| `Home`             | Reset to the top-down preset                          |

## Selection

| Key / mouse        | Action                                                |
| ------------------ | ----------------------------------------------------- |
| `Left-click` viewport | Pick the closest actor or HZD entity under the cursor |
| `Tab` / `Shift+Tab`   | Cycle forward/backward through the (filtered) actor list. Focuses the camera on each. |
| Click row in Actors / HZD table | Select + focus camera                       |

## Modal & windows

| Key                | Action                                                |
| ------------------ | ----------------------------------------------------- |
| `G`                | Open the *Goto coord* modal                           |
| `F1`               | Toggle the help window                                |
| `Esc`              | Quit the editor (or close the goto modal)             |
| `F11` / `Alt+Enter` | Toggle fullscreen                                    |

## Bookmarks (Camera tab)

| Mouse              | Action                                                |
| ------------------ | ----------------------------------------------------- |
| Left-click `1`–`4` | Load that bookmark slot                               |
| Right-click `1`–`4` | Overwrite the slot with the current view             |

## Editing actors

When an actor is selected, the floating *Selected* card on the top right
shows three editable widgets:

- `pos` — `InputInt3`, accepts world coordinates directly.
- `has rot` checkbox — toggles whether the actor's TSV row writes a
  `b:N` rotation. Off = "rot column blank".
- `rot` slider — 0..255, with degrees in the label.

Any change flips `g_actors_dirty`. The Actors tab's `Save` button
becomes `Save*` and lights up; click it to write the modified TSV
back to disk. Reload restores the disk version.

## Goto modal (`G`)

```
┌── Goto coord ───────────────────┐
│  X  Y  Z   [from cursor] [Goto] │
│                       [Cancel]  │
└─────────────────────────────────┘
```

- Type the three ints and press `Enter` (or click `Goto`) to warp.
- `from cursor` populates the inputs with the mouse-cursor floor coords
  (only visible when the cursor is over the y=0 plane).
- `Esc` cancels.

## Re-importing a custom stage

When the currently-loaded stage came from the OBJ→KMD pipeline, the
Scene tab shows a `Reimport` button next to `Reload`. Click it after
re-exporting from Blender to rebuild the stage and reload it without
restarting the editor.
