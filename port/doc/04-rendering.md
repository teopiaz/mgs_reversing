# Rendering Pipeline

This document describes the PSX GPU rendering model, how it is emulated in the
macOS port, and the software renderer that replaces hardware-accelerated
GPU primitives.

---

## 1. PSX GPU Hardware Overview

- **VRAM**: 1 MB, organized as 1024x512 16-bit pixels (1555 BGR + STP bit).
- **Framebuffer**: Double-buffered at y=0 (draw) and y=224 (display), swapped
  each frame. Each buffer is 320x224 (standard NTSC).
- **Textures**: Stored in VRAM alongside the framebuffer, typically at y>=224.
  Three color depths: 4-bit CLUT (paletted, 4 pixels per VRAM word), 8-bit CLUT
  (2 pixels per word), 16-bit direct.
- **Ordering Table (OT)**: Linked list of GPU command packets, sorted by depth.
  Each node's first `u_long` encodes a 24-bit "next" pointer (lower 24 bits)
  and an 8-bit packet length (upper 8 bits).
- **DMA**: GPU primitives are sent via DMA channel 2 from the OT.

---

## 2. OT System on 64-bit

### The Problem

PSX OT nodes store 24-bit addresses in the lower bits of a `u_long` tag.
These are physical RAM addresses on PSX (which has a 2MB address space fitting
in 24 bits). On 64-bit, pointers are 8 bytes and cannot be squeezed into 24
bits.

### The Solution: Handle Table

**File**: `port/psx/libgpu.h`

A lookup table maps 24-bit indices to full 64-bit pointers:

```c
#define PORT_OT_TABLE_SIZE (1 << 18)  /* 256K entries */
void *port_ot_table[PORT_OT_TABLE_SIZE];
int port_ot_next;
```

**`_ptr_to_handle(ptr)`**: Registers a pointer, returns a 24-bit index.
Uses `__sync_fetch_and_add` for atomicity (the OT may be built from multiple
pipeline stages):

```c
static inline u_long _ptr_to_handle(const void *p) {
    if (!p) return 0x00ffffff;            /* terminator sentinel */
    int idx = __sync_fetch_and_add(&port_ot_next, 1);
    if (idx >= PORT_OT_TABLE_SIZE) { port_ot_next = 1; idx = 0; }
    port_ot_table[idx] = (void *)p;
    return (u_long)idx;
}
```

**`_handle_to_ptr(handle)`**: Resolves a 24-bit index back to a pointer:
```c
static inline void *_handle_to_ptr(u_long handle) {
    if (handle == 0x00ffffff || handle >= PORT_OT_TABLE_SIZE) return NULL;
    return port_ot_table[handle];
}
```

All standard PSYQ macros are reimplemented using these functions:
- `setaddr(p, _addr)` stores a handle.
- `nextPrim(p)` resolves a handle.
- `isendprim(p)` checks for the `0x00ffffff` terminator.
- `addPrim(ot, p)` chains a primitive into the OT using handles.

### ClearOTagR

`ClearOTagR(ot, n)` builds a REVERSE linked list: `ot[n-1] -> ot[n-2] -> ... ->
ot[0] -> terminator`. This is the PSX convention; `DrawOTag` walks from the
last entry (farthest depth) to the first (nearest), ensuring back-to-front
rendering.

### Cycle Guard

`port_DrawOTag` aborts after 100,000 nodes to prevent infinite loops from
corrupted OTs:
```c
if (node_count > 100000) {
    printf("[ot] ABORT: >100000 nodes, likely cycle\n");
    break;
}
```

---

## 3. Channel System

**File**: `port/libdg/libdg_stub.c` (DG_InitChanlSystem, DG_ClearChanlSystem)

The engine uses 3 rendering channels (`DG_CHANL DG_Chanls[3]`):

| Channel | Purpose         | OT size | Max objects |
|---------|----------------|---------|-------------|
| ch0     | Background     | 32      | 8           |
| ch1     | 3D primitives  | 256     | 256         |
| ch2     | Overlay/menu   | 1       | 0           |

Channels are linked in `DG_ClearChanlSystem` via `addPrims`:
- `ch0[16] -> ch1` (3D renders in front of background at OT depth 16)
- `ch0[8] -> ch2` (overlay renders in front of 3D at OT depth 8)

The final `DrawOTag(ch0->ot[idx])` traverses all three channels in one pass:
background nodes -> 3D primitives -> overlay/menu.

---

## 4. DG Pipeline Stages

The original PSX rendering pipeline has 7 stages, executed in order for each
frame. The port bypasses most of these with the direct software renderer but
retains the structure for OT-based UI primitives.

### Stage 1: DG_ScreenChanl
Compute per-bone world matrices from animation data. For each `DG_OBJS`,
multiply the base world matrix with per-bone local matrices to get
`DG_OBJ.world` for each sub-model.

### Stage 2: DG_BoundChanl
Bounding-box visibility test. If a model's AABB is entirely outside the view
frustum, skip it. Allocate GPU packet memory for visible models.

### Stage 3: DG_TransChanl
Transform model vertices from local space to screen space using the GTE
(Geometry Transform Engine). On PSX, this is hardware-accelerated.

### Stage 4: DG_ShadeChanl
Apply lighting. Compute vertex colors based on light sources, normal vectors,
and material properties.

### Stage 5: DG_PrimChanl
Generate GPU primitive packets (POLY_GT4, POLY_FT3, etc.) from the transformed
and shaded vertices.

### Stage 6: DG_DivideChanl
Subdivide large polygons that span a wide depth range. PSX lacks sub-pixel
precision, so large polygons must be split to avoid texture warping.

### Stage 7: DG_SortChanl
Sort generated primitives into the ordering table by depth. Then call
`DrawOTag` to send the OT to the GPU.

---

## 5. Software Renderer (port_RenderObjects)

**File**: `port/libdg/libdg_stub.c`

The port's direct renderer bypasses the PSX OT pipeline for 3D objects and
rasterizes textured triangles directly to the VRAM framebuffer.

### Per-Frame Flow

```
port_RenderObjects(idx)
  |
  +-- Clear Z-buffer (uint16_t zbuf[224][320], filled with 0xFFFF)
  |
  +-- For each DG_OBJS in chanl->queue[0..objs_index-1]:
  |     |
  |     +-- Skip if group_id mismatch (objs->group_id & DG_CurrentGroupID == 0)
  |     |
  |     +-- For each DG_OBJ in objs->objs[0..n_models-1]:
  |           |
  |           +-- Select world matrix:
  |           |     obj->world if it has non-zero rotation,
  |           |     else objs->world (parent matrix)
  |           |
  |           +-- mat_mul(eye_inv, world, screen_mat)
  |           |     (eye_inv = inverse camera matrix from DG_CHANL)
  |           |
  |           +-- For each face:
  |                 |
  |                 +-- Read vertex indices from vindices (packed 7-bit x4)
  |                 +-- Project 4 vertices via perspective division
  |                 +-- Backface cull (cross product Z component)
  |                 +-- Offset to screen center (+160, +112)
  |                 +-- Compute average Z for depth test
  |                 +-- Look up texture from materials[] -> DG_GetTexture
  |                 +-- Set up UV coordinates from texcoords[]
  |                 +-- Rasterize as 2 triangles (v0,v1,v3) + (v1,v2,v3)
  |
  +-- OT-based primitives still rendered via DrawOTag for UI
```

### Perspective Projection

```c
static int project(MATRIX *screen, SVECTOR *vert, int dist, int *sx, int *sy, int *sz)
{
    int cx, cy, cz;
    mat_transform(screen, vert, &cx, &cy, &cz);
    if (cz <= 4) return 0;     /* behind camera */
    *sx = (cx * dist) / cz;    /* perspective divide */
    *sy = (cy * dist) / cz;
    *sz = cz;
    return 1;
}
```

`dist` is `chanl->clip_distance` (default 256), matching the PSX GTE projection
plane distance.

### Triangle Rasterization

**File**: `port/libdg/vram.c` (`draw_flat_tri`)

Scanline rasterizer:
1. Sort 3 vertices by Y coordinate (top to bottom).
2. For each scanline Y from y0 to y2:
   - Interpolate X endpoints along the two active edges.
   - If textured, interpolate U,V along edges and across scanline.
   - For each pixel, Z-test against `port_zbuf[vy][vx]`.
   - If textured: `sample_vram_texel(tpage, clut, u, v)`.
   - If texel is 0x0000: skip (PSX transparency convention).
   - If STP bit set and model is semi-transparent: alpha blend with
     background using the ABR mode (0-3).
   - Write pixel to `vram[vy][vx]` and update Z-buffer.

### Semi-Transparency Modes

PSX supports 4 blend modes via the ABR field in the texture page:

| ABR | Formula           | Description      |
|-----|-------------------|-----------------|
| 0   | (B+F)/2           | 50% blend       |
| 1   | B+F               | Additive        |
| 2   | B-F               | Subtractive     |
| 3   | B+F/4             | 25% additive    |

Implemented in `draw_flat_tri` when `port_tex_semi_trans` is set and the texel
has the STP bit (bit 15) set.

---

## 6. Texture System

### Texture Table

**512-slot hash table**: `DG_TEX TexSets[512]`.

`FindTexture(id)`: Hash = `id % 512`, linear probe on collision. Each `DG_TEX`
stores:
- `id`: texture hash ID
- `tpage`: PSX texture page (encodes VRAM base X/Y, color depth, ABR mode)
- `clut`: CLUT (Color Look-Up Table) position in VRAM
- `off_x`, `off_y`: UV offset within the texture page
- `w`, `h`: texture dimensions

`DG_SetTexture(id, bpp, abr, image_rect, clut_rect, n_colors)`: Called by the
PCX loader after uploading pixels to VRAM. Computes `tpage` and `clut` from
the VRAM coordinates and stores the texture entry.

### PCX Loader

**File**: `port/libdg/pcx_loader.c`

Decodes PCX images (the format used by the PSX toolchain):
- **4-bit planar**: 4 bit-planes (R,G,B,A) combined into 4-bit palette indices.
  Uses `DG_PcxRead4Bpp`.
- **8-bit RLE**: Run-length encoded 8-bit paletted image. Uses `DG_PcxRead8Bpp`.
- **Palette**: RGB888 triplets converted to PSX 1555 BGR via `DG_PcxReadPalette`.

After decoding, the image data and palette are uploaded to VRAM via
`LoadImage(&rect, data)`.

### Resident Textures

Textures loaded from the 'r' (resident) stage archive persist across stage
transitions. `DG_SaveResidentTextureCache()` snapshots current textures marked
with `RESIDENT_FLAG`. `DG_LoadResidentTextureCache()` restores them after a
stage change clears the texture table.

### Texture Sampling

**File**: `port/libdg/libdg_stub.c` (`sample_vram_texel`)

```c
uint16_t sample_vram_texel(uint16_t tpage, uint16_t clut, int u, int v)
```

Decodes the tpage to determine:
- Color mode: `tp = (tpage >> 7) & 3` (0=4bpp, 1=8bpp, 2=16bpp)
- VRAM base: `base_x = (tpage & 0xF) * 64`, `base_y = ((tpage >> 4) & 1) * 256`

For 4-bit mode: Reads the 16-bit VRAM word at `(base_x + u/4, base_y + v)`,
extracts the 4-bit nibble for the pixel, then looks up the 16-bit color from
the CLUT at `(clut_x + nibble, clut_y)`.

For 8-bit mode: Reads the 16-bit VRAM word, extracts the 8-bit index, looks up
the CLUT color.

For 16-bit mode: Reads the 16-bit color directly from VRAM.

---

## 7. VRAM Simulation

**File**: `port/libdg/vram.c`

```c
uint16_t vram[512][1024];       /* Full PSX VRAM */
uint16_t port_zbuf[224][320];   /* Z-buffer for framebuffer area */
```

### Layout

```
     0                                            1024
   0 +--------------------------------------------+
     | Framebuffer 0: 320x224 (draw target)        |
 224 +--------------------------------------------+
     | Framebuffer 1: 320x224 (display target)     |
     | (not used in port — single buffer)           |
 448 +--------------------------------------------+
     | Texture / CLUT area                         |
 512 +--------------------------------------------+
```

Textures and CLUTs are stored at y>=224. The PCX loader uploads decoded image
data to these regions. `port_ClearImage` clamps `y1` to 224 to prevent
accidental texture erasure.

### SDL Display

`port_vram_init(renderer)` creates an SDL texture (`SDL_PIXELFORMAT_ABGR1555`,
1024x512). Each frame, `port_vram_display()` uploads the relevant VRAM region
to the texture:
- Normal mode: copies the display region (320x224 at `disp_x, disp_y`).
- Debug mode (`MGS_VRAM_DEBUG=1`): copies the entire 1024x512 VRAM.

The texture is then rendered to the SDL window via `SDL_RenderCopy`.

---

## 8. OT Traversal and GPU Primitive Rendering

**File**: `port/libdg/vram.c` (`port_DrawOTag`)

Walks the OT linked list and renders each GPU primitive based on its command
byte (`code & 0xFC`):

| Code   | Type       | Rendering                                      |
|--------|-----------|------------------------------------------------|
| 0x20   | POLY_F3   | Flat-colored triangle                          |
| 0x24   | POLY_FT3  | Textured triangle (currently flat fallback)    |
| 0x28   | POLY_F4   | Flat-colored quad (2 triangles)                |
| 0x2C   | POLY_FT4  | Textured quad (currently flat fallback)        |
| 0x30   | POLY_G3   | Gouraud triangle (uses first vertex color)     |
| 0x34   | POLY_GT3  | Gouraud textured tri (currently marker)        |
| 0x38   | POLY_G4   | Gouraud quad                                   |
| 0x3C   | POLY_GT4  | Gouraud textured quad (currently marker)       |
| 0x60   | TILE      | Variable-size filled rectangle                 |
| 0x64   | SPRT      | Textured sprite (used by font system)          |
| 0x68   | TILE_1    | 1x1 filled rectangle                           |
| 0x70   | TILE_8    | 8x8 filled rectangle                           |
| 0x74   | SPRT_8    | 8x8 textured sprite                            |
| 0x78   | TILE_16   | 16x16 filled rectangle                         |
| 0x7C   | SPRT_16   | 16x16 textured sprite                          |
| 0xE0   | DR_TPAGE  | Set current texture page (state change)        |
| 0xE4   | DR_AREA   | Set drawing area (ignored)                     |
| 0xE8   | DR_OFFSET | Set drawing offset (ignored)                   |

SPRT primitives sample texels from VRAM using the current tpage (set by
DR_TPAGE commands) and the per-primitive CLUT. This is how the font/text system
renders glyphs.

Safety limits: abort after 100,000 OT nodes or 10,000 rendered primitives.
