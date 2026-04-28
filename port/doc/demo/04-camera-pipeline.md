# Camera pipeline

The cutscene camera that gets baked into every triangle's eye-space
position is the end of a chain that depends on **which kind of demo**
is playing. There are two paths — pick the right one or you'll end up
chasing a frozen camera in the wrong source file.

```
streamed (demo -s / demo -f):
  demothrd.c StreamAct → FrameRunDemo(work, dat)
                          ↓
                          gUnkCameraStruct2_800B7868.{eye, center}  (per frame)

GCL-scripted only (no demo -s):
  per-stage overlay actor (democame.c, 11g_demo.c, intr_cam.c, …)
                          ↓
                          gUnkCameraStruct2_800B7868.{eye, center}

then for both paths:
  camera.c Act()           reads   →  same struct
                           calls   →  DG_LookAt(DG_Chanl(0), eye, center, clip_dist)
  DG_LookAt                writes  →  DG_Chanls[0].eye_inv (the world→eye matrix)
  port_RenderObjects(idx)  consumes →  DG_Chanls[0].eye_inv
                                       projects every actor mesh's verts
                                       through the matrix → gl_submit_tri3d
  gl_renderer_present                  →  GL draw → window swap
```

Each step has a single observable side effect; if any breaks, the
chain stalls in a different way.

## Step 1A — streamed: FrameRunDemo writes the camera struct

When the demo is streamed (`demo -s <code>` or `demo -f "*.dmo"`),
[`source/kojo/demothrd.c`](../../../source/kojo/demothrd.c) spawns a
`DemoWork` actor whose `StreamAct` / `FileAct` polls the FS streamer
for the next `DMO_DAT` record per cutscene frame. For each record,
it calls `FrameRunDemo` (in
[`source/kojo/demo.c`](../../../source/kojo/demo.c)), which writes:

```c
gUnkCameraStruct2_800B7868.eye.vx    = data->eye_x;       // s16, PSX world units
gUnkCameraStruct2_800B7868.eye.vy    = data->eye_y;
gUnkCameraStruct2_800B7868.eye.vz    = data->eye_z;
gUnkCameraStruct2_800B7868.center.vx = data->center_x;
gUnkCameraStruct2_800B7868.center.vy = data->center_y;
gUnkCameraStruct2_800B7868.center.vz = data->center_z;
DG_Chanl(0)->clip_distance           = data->clip_dist;   // FOV
```

The animation is **fully baked** — every frame of the cinematic ships
on disc as a `DMO_DAT` record. There's no interpolation or actor-
driven motion; eye/center are looked up by frame index.

This is what plays the long disc cinematics (the d00a opener, codec
calls with synced voice, boss intros). See
[09-streamed-demos.md](09-streamed-demos.md) for the full streamer
chain — it's not running in the editor today; the streamed path
stalls there.

## Step 1B — GCL-scripted: a per-stage overlay actor

When the demo is GCL-scripted only (no `demo -s` / `demo -f` directive
anywhere in the scenario / demo `.gcl`), there is **no built-in
cutscene camera animator** — none of CINEMA, WT_VIEW, DEMODOLL,
EMITTER, FADEIO, JIMAKU, or RADIO touches `gUnkCameraStruct2`. The
camera stays at whatever the previous gameplay tick set, which means
in-stage scenes (codec calls, environmental fly-bys, scene
transitions) reuse the last gameplay framing or use a stage-specific
override.

When a designer needed an animated camera in a non-streamed cutscene,
the disc shipped a custom actor in the per-stage overlay:

| Source | Stage | What it does |
| --- | --- | --- |
| [`source/overlays/s19b/takabe/democame.c`](../../../source/overlays/s19b/takabe/democame.c) | s19b (jeep ride) | spawns at script init, drives `gUnkCameraStruct2.eye/center` along a scripted path |
| [`source/overlays/s11g/okajima/11g_demo.c`](../../../source/overlays/s11g/okajima/11g_demo.c) | s11g (hind chase) | similar — moves camera with the hind |
| [`source/overlays/s12a/okajima/wolf/wolf2.c`](../../../source/overlays/s12a/okajima/wolf/wolf2.c) | s12a (wolves) | overrides camera during a scripted attack |
| [`source/chara/others/intr_cam.c`](../../../source/chara/others/intr_cam.c) | various | "introductory camera" — fly-in for stage entry |

These all write `gUnkCameraStruct2.eye/center` directly. They are
*not* what the chara hash `0x8E45` (`WT_VIEW`) refers to —
`NewWaterView` is a water visual effect (see
[03-key-actors.md](03-key-actors.md)).

So if a non-streamed cutscene's camera looks frozen, the question is
"does *this stage* have an overlay actor for camera animation?" — and
in most disc demos the answer is "no, the camera intentionally
doesn't move during this scene".

## Step 2 — camera.c reads the struct, calls DG_LookAt

[`source/game/camera.c`](../../../source/game/camera.c)'s `Act()` is
one of the simpler engine actors:

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
values. Expected progression depends on the demo type:

| Step | Symptom of failure | Diagnostic |
| --- | --- | --- |
| streamed: DemoWork spawned | "DemoWork at frame -1" log; "Channels updated" stays `[- - -]` forever | demothrd.c is in the dump, but `FS_StreamGetData` returns NULL — see [09-streamed-demos.md](09-streamed-demos.md) |
| GCL-only: stage has no camera actor | (expected for in-stage demos) | "Channels updated" `[- - -]` is the *correct* state — the camera is meant to be static |
| camera.c Act runs | "Channels updated" stays `[- - -]` even though something *should* drive it | `Live actors` includes `camera.c`; `GM_GameStatus` is non-negative |
| DG_LookAt fires | "Channels updated" stays `[- - -]` | `GV_PauseLevel == 0` |
| Matrix actually changes | "Channels updated" shows `[0 - -]` flickering | "Camera (chanl 0) pos / yaw" values change frame-to-frame |
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
