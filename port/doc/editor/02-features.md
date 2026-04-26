# Features

The inspector window has four tabs, a status header, a few floating
overlays, and the modal `G`-shortcut popup. This page enumerates each.

## Status header

Top of the inspector panel, always visible:

```
s01a   60 fps   1024t 412l
cursor: -3581 0 -5180
```

- **Stage name** + `(none)` if no stage loaded.
- **fps** from `ImGui::GetIO().Framerate`.
- **`Nt Ml`** — submitted triangle and line counts (per frame), fed by
  `gl_renderer_stats()`.
- **`cursor`** — world-space coordinate where the mouse cursor is
  pointing on the y=0 plane. `-` if the ray doesn't intersect.

## Scene tab

| Widget          | Function                                                    |
| --------------- | ----------------------------------------------------------- |
| `filter stage`  | Substring match on stage names in the combo (case-sensitive — STAGE.DIR uses lower-case names anyway). |
| Stage combo     | Picks any of the disc's 95 stages or any stage discovered in `port/editor/extra_stages/`. |
| `Reload` button | Re-reads the currently-loaded stage from disk. |
| `Reimport` button | Only shown when the current stage has a `manifest.json` (i.e. came from the OBJ→KMD pipeline). Shells out to `tools/import_stage.py --manifest …` and reloads on success. |
| `axes`          | Toggle the X/Y/Z gizmo at world origin. |
| `wireframe`     | GL `glPolygonMode(GL_LINE)` for the 3D pass. |
| `untextured`    | Skip the texture sample in the fragment shader. Useful for sanity checks. |
| `VRAM viewer`   | Replaces the 3D output with a 1024×512 dump of VRAM. |
| `Cache entries` | Collapsible table listing every entry in the GV cache (ext, id, name hash). Useful when authoring a custom stage and you need to confirm a KMD/HZD/PCX is loaded. |

## Camera tab

State readout (live):

```
pos    0  20000  0
yaw    0.0°    pitch -85.9°
FOV   [============o====]  1.00
```

| Widget               | Function                                                  |
| -------------------- | --------------------------------------------------------- |
| `Top-down`           | Resets camera to the default top-down view.               |
| `First-person`       | Drops camera to player-eye height (`y=-1500, z=-3000`, level pitch). |
| `Screenshot`         | Calls `gl_renderer_save_ppm()` — writes `screenshot-<stage>-<timestamp>.ppm` into the editor's CWD. |
| Goto inputs `X Y Z`  | Type three ints and click `Goto` to warp the camera there. The cursor's floor coords are shown next to the button as a hint. |
| Bookmarks `1 2 3 4`  | Left-click loads the saved view, right-click overwrites with the current view. Tooltip shows usage. Empty slots render disabled. |
| `?` button           | Toggles the `Help & keybindings` window (also `F1`). |

## Actors tab

| Widget         | Function                                                          |
| -------------- | ----------------------------------------------------------------- |
| `display` combo | `Hidden` / `Cubes` / `Models`. Cubes draw a coloured AABB per actor; Models render the actor's KMD when the curated lookup or humanoid heuristic resolves one (cube fallback otherwise). |
| `rotations`    | Toggle yaw arrows from the actor's `b:N` rotation field.          |
| `filter type`  | Case-insensitive substring filter on actor type (e.g. `WATCHER`). |
| `Reload`       | Re-reads `data/<stage>_actors.tsv` from disk.                     |
| `Save`         | Disabled when clean; labelled `Save*` with an `unsaved` suffix once any field has been edited. Writes back the same TSV. |
| Actor table    | Columns: `Type`, `Inst` (instance id from GCL `$s:XXXX`), `Pos`, `Src` (`scen` for `scenerio.gcl` entries, `demo` for `demo.gcl`). Click any row to focus the camera and select. |

## HZD tab

| Widget                        | Function                                                |
| ----------------------------- | ------------------------------------------------------- |
| `walls`/`floors`/`traps`/`cameras`/`zones`/`routes` | Layer visibility for the 3D wireframe overlay. |
| `labels`                      | Project trap names to screen-space using `ed_world_to_screen`. |
| Per-category collapsible tables | One per layer; click a row to focus the camera on that entity and highlight it in the wireframe. |

## Floating overlays

- **Selected actor card** — top-right, only when an actor is selected.
  Editable `pos` (`InputInt3`) and `rot_b` (slider with a checkbox to
  add/remove rotation). Shows the colour swatch, type, instance,
  source (scen/demo), proc, model status. `Focus` re-centres the
  camera; `Clear` deselects.

- **Trap labels** — drawn on the background draw list (no input
  capture). HZD_TRP names rendered at the projected centre of each
  trigger volume.

- **Help window** — toggled by `F1` or the `?` button. Lists every
  shortcut and feature in one screen.

- **Goto modal** — opened by `G`. Three int inputs + `from cursor`
  shortcut + Goto/Cancel. Enter applies; Esc cancels.

## Picker

Left-click in the viewport (anywhere not over an ImGui window):

1. Try `ed_actors_pick_ray` — CPU ray vs. AABBs around each actor's
   `pos[3]`.
2. If no actor hit, try `ed_hzd_pick_ray` — ray vs. HZD trap and camera
   AABBs.

A hit selects the entity and focuses the camera on it.

`Tab` and `Shift-Tab` cycle through the actor list (respecting the
filter), focusing the camera on each.

## Render layers

In z-order from back to front, every frame:

1. Map KMDs (one or more, picked from the GV cache by bbox heuristic).
2. Optional actor KMD models (Models display mode).
3. World axes gizmo (line3d).
4. HZD wireframes — walls / floors / trap AABBs / camera frustums /
   zones / routes (line3d).
5. Actor markers — cubes plus rotation arrows (line3d).
6. ImGui frame including the trap-label overlay.

Everything 3D goes through the unlit `port_force_gouraud_neutral=1`
path — no lighting math regardless of the stage's GCL light config.
