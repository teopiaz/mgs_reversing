---
file: source/game/camera.c
---

# `game/camera.c` — camera system

The single biggest file in `game/` — 1238 lines. Owns the in-game
camera that tracks the player + handles cinematic / scripted
transitions.

## Globals

```c
GM_CAMERA  GM_Camera;          // the active in-game camera state
GM_CAMERA  gUnkCameraStruct2;  // secondary / DMO-supplied camera
```

`GM_CAMERA` is a fairly fat struct holding:

- `eye` (camera position SVECTOR)
- `center` (look-at target SVECTOR)
- `up` (rolled in some cinematics)
- `clip_distance`
- `fov` / `zoom` (12-bit fixed)
- `first_person` (bool flag)
- per-mode timing/interp state

## How it drives `DG_LookAt`

Once per frame, after the player's CONTROL update, `GM_ActCamera`
copies state from `GM_Camera` into the `DG_Chanl(0)` channel:

```c
DG_LookAt(DG_Chanl(0), &GM_Camera.eye, &GM_Camera.center, GM_Camera.clip_distance);
```

This is the sole consumer of `GM_Camera`'s eye/center fields by the
time the renderer runs.

## Camera modes

The camera has multiple sub-modes selected by zone / GCL / cinematic:

| Mode | Source | Behaviour |
| ---- | ------ | --------- |
| Default tracking | zone-camera in HZD | Follow player at fixed offset |
| Fixed | zone-camera with explicit eye | Stationary; look at player from fixed viewpoint |
| First-person | player toggled | Camera at player's eye level, looking forward |
| Free-look | player + R-stick | Player can look around (fixed eye) |
| DMO playback | demo file | Eye/center fed from DMO_DAT keyframes |
| GCL `cam` command | `script.c` | Manual override |

## Zone-camera transitions

The HZD file includes a per-zone camera config (`HZD_CAM`). When
the player crosses a zone boundary, `GetAddress` returns the new
zone, the matching `HZD_CAM` is read, and `GM_Camera` is set up to
interpolate from the old config to the new.

The interpolation is critical: snapping the camera between zones
would be jarring. Camera.c blends eye + center over ~30 frames
using `GV_NearTime`-style helpers.

## DMO playback

Cinematic camera is animated keyframes:

- DMO_DAT contains a list of `{frame, eye, center, up, fov}`.
- Each frame, the engine interpolates between adjacent keyframes
  via a smooth Hermite curve.
- Result is written into `GM_Camera`; then `DG_LookAt` runs as
  normal.

So cinematic and gameplay cameras share the same downstream path —
the only difference is how `GM_Camera` is populated.

## Camera shake (`takabe/camshake.c`)

Camera shake is applied as a *post-process* offset on the eye
position. `camshake.c` exposes a "shake (intensity, duration)" API
that camera.c reads each frame:

```c
GM_Camera.eye.x += sin(t * freq) * intensity * envelope(t)
```

Used during explosions, certain footsteps, alarm flashes.

## `first_person` flag

When the player toggles first-person view (R1 + holding still):

- `GM_Camera.first_person = 1`.
- Player's snake body becomes invisible (or low-poly).
- Camera's eye = player's head bone matrix translation; center =
  eye + 1000 * forward.
- The `field_B7B` KMD-swap mechanism (see
  [enemy/meryl7.md](../enemy/meryl7.md)) is keyed off this.

## GCL `cam` command (in script.c)

Lets a stage's GCL force a specific camera config:

```
cam -p (eye_x, eye_y, eye_z) -d (center_x, center_y, center_z) -r 30
```

Sets `GM_Camera` directly with a 30-frame interpolation curve.
Used heavily in cinematics for non-DMO camera changes.

## Pitfalls

- **Don't mutate `GM_Camera` mid-frame.** Race conditions between
  control update and camera update — pick one place to write.
- **Zone camera boundaries can flip rapidly** if the player walks
  along a boundary. The camera.c code has hysteresis to prevent
  flickering between two zone configs.
- **The Y-up axis can flip.** Some cinematics rotate the camera
  upside-down by inverting `up`; gameplay code that assumes Y-up
  breaks (e.g. ground-shadow placement). `up` is mostly ignored
  in gameplay but cinematic uses it.

---

## Port notes

The port substitutes `DG_LookAt`'s body with a host-matrix builder,
but the upstream camera code is unchanged. See
[camera_debug_findings.md](#) for a port-specific zone-transition
fix.

## See also

- [`source/libdg/display.c`](../libdg/display.md) — `DG_LookAt`.
- [03-control-and-motion.md](../03-control-and-motion.md) — player
  position drives camera.
- [doc/demo/](../../demo/) — DMO format.
- [`source/takabe/camshake.c`](../../../../source/takabe/camshake.c)
  — shake module.
