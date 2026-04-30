---
file: source/libdg/display.c + dgd.c + loader.c
---

# `libdg/display.c` + `dgd.c` + `loader.c` — top-of-pipeline driver

## `dgd.c` — the render daemon

The DG library installs itself as a top-priority actor (the "render
daemon") on `GV_ACTOR_DAEMON`. Its act callback runs the full
pipeline once per frame.

```c
DG_StartDaemon():
    register an actor at level 0 with act = render_daemon_act
    init pipeline, light, palette, channels

render_daemon_act():
    DG_RenderPipeline_Init()         // start of frame
    for each chanl in [0..2]:
        DG_RenderPipeline(chanl)     // run 7 stages
    DG_DrawOTag(idx)                 // submit GPU work
    DG_SwapFrame()                   // V-sync + buffer flip
```

Globals exposed by dgd.c:

- `DG_FrameRate` — 30 (NTSC) / 25 (PAL).
- `DG_HikituriFlag` / `DG_HikituriFlagOld` — "hold" (引き釣り = hang/freeze) flag, set during pause/codec to skip frame swap.

## `display.c` — display environment + frame swap

Owns the PSX `DRAWENV` / `DISPENV` setup, double-buffering, and the
clipping rectangle:

```c
void DG_InitDispEnv(int x, short y, short w, short h, int clipH);
void DG_ChangeReso(int);                    // toggle 320x224 vs 320x240
void DG_RenderPipeline_Init(void);
void DG_SwapFrame(void);                    // VBlank + buffer swap
void DG_RenderFrame(void);                  // submit GPU + draw
void DG_LookAt(DG_CHANL *chanl,
               SVECTOR *eye, SVECTOR *center, int clip_distance);
void DG_AdjustOverscan(MATRIX *matrix);
void DG_Clip(RECT *clip_rect, int dist);
void DG_FadeScreen(int amount);
DISPENV *DG_GetDisplayEnv(void);
```

### `DG_LookAt(chanl, eye, center, clip)`

The canonical "set the camera" call. Computes the channel's
`eye` and `eye_inv` matrices from the eye/center positions:

- forward = normalize(center - eye)
- up = (0, 1, 0) (or derived from rotation if specified elsewhere)
- right = up × forward
- builds 3x3 view matrix into `chanl->eye`
- transposes into `chanl->eye_inv`
- sets `chanl->clip_distance = clip`

All subsequent screen-stage transformations use `chanl->eye` to
project world-space into camera-space.

### `DG_FadeScreen(amount)`

Tints the screen by stippling: writes a fade overlay using
`DG_BackgroundColor` × amount. Used by `takabe/fadeio.c` for
between-stage fades.

### `DG_AdjustOverscan(matrix)` / `DG_Clip` / `DG_DisableClipping`

The framebuffer can have a few-pixel overscan; these adjust the
projected matrix and clip rectangle to compensate.

### `DG_OffsetDispEnv(offset)` / `DG_ClipDispEnv(x, y)`

Used by VR / split-screen modes to shift the display environment
without moving the framebuffer.

## `loader.c` — file-format dispatchers

The loaders that `cache.c` invokes when an asset's extension matches.
`loader.c` registers them at boot:

```c
GV_SetLoader('k', DG_LoadInitKmd);    // *.kmd → KMD models
GV_SetLoader('o', DG_LoadInitOar);    // *.oar → motion archives
GV_SetLoader('n', DG_LoadInitNar);    // *.nar
GV_SetLoader('p', DG_LoadInitPcx);    // *.pcx → texture
GV_SetLoader('i', DG_LoadInitImg);    // *.img → texture mosaic
GV_SetLoader('s', DG_LoadInitSgt);    // *.sgt → ?
GV_SetLoader('l', DG_LoadInitLit);    // *.lit → light data
GV_SetLoader('r', DG_LoadInitKmdar);  // KMD archive
```

### KMD loader (`DG_LoadInitKmd`)

The most-called loader. Reads a KMD header, validates magic, fixes
up vertex / index pointer offsets so they're relative to the
loaded buffer, registers any embedded textures.

### PCX loader (`DG_LoadInitPcx`)

Reads a PCX, decompresses RLE-style scanlines, uploads to VRAM at
the assigned tpage, registers the resulting `DG_TEX` via
`DG_SetTexture`. PCX header carries the destination tpage.

### OAR loader (`DG_LoadInitOar`)

OARs are motion archives — many `MOTION_ARCHIVE` records bundled
together. Loader registers the table for later lookup.

## Globals

| Symbol | Source | Role |
| ------ | ------ | ---- |
| `DG_CurrentGroupID` | display.c | Current group tag for `DG_GroupObjsEx` |
| `DG_ClipMin[2]`, `DG_ClipMax[2]` | display.c | Active screen-space clip rect |
| `DG_UnDrawFrameCount` | display.c | Frames since last successful draw (debug) |

## Pitfalls

- **Don't call `DG_LookAt` mid-frame.** Eye matrix is read by every
  stage downstream. Set it before queueing OBJS for that channel.
- **`DG_SwapFrame` blocks on VSync.** Don't call it unless you're
  certain you want to stall on V-sync (the daemon does this once
  per frame; nobody else should).
- **PCX→VRAM upload overlaps with previous-frame draw.** Loaders
  that upload to VRAM mid-frame can stomp on a still-being-drawn
  packet. The cache loaders only run during stage transition
  (gamed.c::WAIT_LOAD) when no draw is in flight.

---

## Port notes

`display.c` is heavily replaced by
[`port/libdg/gl_renderer.c`](../../../../port/libdg/gl_renderer.c) —
the PSX DRAWENV / DISPENV concepts don't map to OpenGL. The port
keeps the `DG_LookAt` API but rewrites the body to compute a host
view-projection matrix.

`loader.c` is *partly* replaced — the KMD loader rewrites pointer
fields from PSX 32-bit → host 64-bit and re-`GV_SetCache`s the
buffer. Without this, the inline `OFFSET_TO_PTR` conversions would
crash on 64-bit hosts. PCX, OAR, and the other loaders run
unmodified. See [project_32bit_pointer_blocker](#) for context.

## See also

- [pipeline.md](pipeline.md) — what runs after display init
  per-frame.
- [`source/libfs/`](../libfs/index.md) — feeds bytes into loaders.
- [`source/libgv/cache.md`](../libgv/cache.md) — the loader
  registry that `loader.c` populates.
