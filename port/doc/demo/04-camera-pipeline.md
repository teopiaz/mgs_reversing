# Camera pipeline

The cutscene camera that gets baked into every triangle's eye-space
position is the end of a four-step chain. Knowing the chain is the
shortest route to debugging any "camera doesn't move / wrong angle /
black screen" problem.

```
WT_VIEW.Act()              writes  →  gUnkCameraStruct2_800B7868.{eye, center, zoom}
camera.c Act()             reads   →  same struct
                           calls   →  DG_LookAt(DG_Chanl(0), eye, center, zoom)
DG_LookAt                  writes  →  DG_Chanls[0].eye_inv (the world→eye matrix)
port_RenderObjects(idx)    consumes →  DG_Chanls[0].eye_inv
                                       projects every actor mesh's verts
                                       through the matrix → gl_submit_tri3d
gl_renderer_present                  →  GL draw → window swap
```

Each step has a single observable side effect; if any breaks, the
chain stalls in a different way.

## Step 1 — WT_VIEW animates the camera struct

`source/takabe/wt_view.c`'s `Act()` runs every tick. It reads its
own `Work` struct (populated at spawn from the GCL's `-b` and `-c`
options), advances internal animation timers, and writes the result
into the global

```c
struct {
    SVECTOR eye;       // world-space camera position
    SVECTOR center;    // world-space look-at target
    int     zoom;      // PSX H register / FOV mapping
} gUnkCameraStruct2_800B7868;
```

For a static framing, the same eye + center is written every frame.
For a moving shot (a slow dolly, a pan), eye/center change on a per-
tick smooth curve. The interpolation lookup tables live in
`wt_view.c`'s `.rodata`.

If WT_VIEW is **missing** (factory not registered → `chara: func not
found`), nothing writes the struct. It stays at whatever the C
runtime zero-initialises it to: `eye = center = (0,0,0)`, `zoom = 0`.
That's a *degenerate* camera — `DG_LookAt` will produce a
forward-vector of zero, fall through its `right.vx|y|z == 0` branch,
and bake an identity-ish matrix. The 3D pane appears frozen looking
into the world origin from nowhere.

## Step 2 — camera.c reads the struct, calls DG_LookAt

`source/game/camera.c`'s `Act()` is one of the simpler engine actors:

```c
static void Act(GV_ACT *work) {
    if (GM_GameStatus >= 0) {
        if (GV_PauseLevel == 0) {
            // … helpers that update GM_Camera and gUnkCameraStruct2 …
        }
        DG_LookAt(DG_Chanl(0),
                  &gUnkCameraStruct2_800B7868.eye,
                  &gUnkCameraStruct2_800B7868.center,
                  gUnkCameraStruct2_800B7868.zoom);
    }
}
```

Two gates control execution:

| Condition | What's gated |
| --- | --- |
| `GM_GameStatus < 0` | Whole body skips. `DG_LookAt` doesn't fire. Cause: `STATE_PADRELEASE \| STATE_ALL_OFF` flipped the high bit. |
| `GV_PauseLevel != 0` | The helpers that mutate `GM_Camera` skip. `DG_LookAt` *does* still run, but with the same input as last tick → matrix doesn't change. Cause: a game-state actor pushed a pause flag. |

The editor's Demo Player exposes both values in its **Diagnostics**
tree under the Demo tab — see [05-editor-player.md](05-editor-player.md).

## Step 3 — DG_LookAt builds eye_inv

`source/libdg/display.c`'s `DG_LookAt`:

```c
void DG_LookAt(DG_CHANL *chanl, SVECTOR *eye, SVECTOR *center, int clip_distance) {
    chanl->clip_distance = clip_distance;
    // forward = (center - eye), normalised
    // right   = up × forward (with fallback to last-good when degenerate)
    // up      = forward × right
    // chanl->eye      = { rotation rows = right/up/forward, translation = eye }
    // chanl->eye_inv  = inverse of eye (transpose for rotation, -m^T·t for translation)
}
```

`chanl->eye_inv` is the matrix the rest of the engine multiplies into
every world-space vertex to land in eye space:

```
eye_pos_xyz = eye_inv.m * world_pos + eye_inv.t   (then divide eye_pos.z to NDC)
```

`DG_Chanls[0]` is the player / cutscene channel, used for the main
3D scene. `DG_Chanls[1]` is for HUD-style overlays (cinema bars,
fade rectangles). `DG_Chanls[2]` is used by the radar / map.

## Step 4 — port_RenderObjects projects with the new matrix

`port/libdg/libdg_stub.c`'s `port_RenderObjects(GV_Clock)`:

1. `gl_renderer_begin_3d()` — drops last frame's submitted tris.
2. Bound / Trans / Shade pass on each `DG_Chanls[ci]` — runs
   frustum culling + the world→eye + per-vertex shading using
   `chanl->eye_inv`.
3. Walks `DG_OBJS` list, submits each face via `gl_submit_tri3d` —
   the call carries the eye-space coordinates already computed.

`gl_renderer_present()` later issues the GL draw with all submitted
tris. The cutscene's framing shows up because every face was
projected through the matrix that started its life as
`gUnkCameraStruct2_800B7868.eye/center` four steps earlier.

## How to verify each step in the editor

The editor's Demo Player tab exposes channel-dirty bits + raw camera
values. The expected progression on a working d00a Play:

| Step | Symptom of failure | Diagnostic |
| --- | --- | --- |
| WT_VIEW spawned | `chara: func not found (hash=0x8E45)` in stderr | "Live actors" doesn't include `wt_view.c` after **Dump actors** |
| WT_VIEW Act runs | `gUnkCameraStruct2` stays zero | n/a — would need a printf inside wt_view.c |
| camera.c Act runs | "Channels updated" stays `[- - -]` | `Live actors` includes `camera.c`; `GM_GameStatus` is non-negative |
| DG_LookAt fires | `Channels updated` stays `[- - -]` | `GV_PauseLevel == 0` |
| Matrix actually changes | `Channels updated` shows `[0 - -]` flickering | "Camera (chanl 0) pos / yaw" values change frame-to-frame |
| Render uses the matrix | Camera readout updates but 3D pane looks frozen | the editor's render path uses `g_demo_active_chanl`; check that it picked 0 |

## Channel auto-detect

Disc-shipped cutscenes consistently write `DG_Chanls[0]` (player
channel). Some overlays may write 1 or 2 instead — early prototypes,
nonstandard scenes. The editor tracks per-tick CRC of all three
channels' eye_inv and adopts whichever changed last as the "active"
one:

```c
// port/editor/ed_demo.c:update_camera_snapshot()
for (int ci = 0; ci < 3; ci++) {
    if (eye_inv_crc(DG_Chanls[ci]) != s_chanl_crc[ci]) {
        g_demo_active_chanl = ci;  // last write wins
        s_chanl_crc[ci] = new_crc;
    }
}
```

`g_demo_active_chanl` is read by `ed_render_frame_demo` to pick the
projection used for the 3D pane. Single-camera cutscenes (the
common case) all converge on the same channel each tick.
