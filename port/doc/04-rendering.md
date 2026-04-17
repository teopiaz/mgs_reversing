# Rendering Pipeline: PSX GPU vs Port Software Renderer

This document explains the PSX GPU rendering pipeline in detail, then describes how the
port replaces it with a CPU-based software renderer. Understanding the PSX pipeline is
essential because the port still runs most of the original pipeline code — only the final
GPU submission is replaced.

## 1. PSX GPU Hardware Overview

### 1.1 VRAM Layout (1024 x 512 x 16-bit)

The PSX GPU has 1MB of video RAM organized as a 1024x512 pixel grid of 16-bit words.

```
  0       320     640     768    1024
  +-------+-------+-------+------+  0
  | Frame | Frame |       |      |
  | Buf 0 | Buf 1 | Tex   | Tex  |
  |320x224|320x224| Pages | Pages|
  |       |       |       |      |
  +-------+-------+       +------+ 196
  |               |       |CLUT  |  <-- Palette backup area
  |  Texture      |       |backup|
  |  Pages        |       +------+ 226
  |  (4-bit and   |       |CLUT  |  <-- Active palettes (used by GPU)
  |   8-bit       |       |active|
  |   indexed)    |       |      |
  +---------------+-------+------+ 512
```

**Framebuffers**: Two 320x224 buffers at (0,0) and (320,0) for double-buffering.

**Texture pages**: 64x256 pixel blocks. In 4-bit mode, one page holds 256x256 logical
pixels. In 8-bit mode, 128x256. In 16-bit mode, 64x256.

**CLUTs (Color Lookup Tables)**: 16 or 256 entries of 16-bit RGB555 colors, stored in
VRAM. The palette system uses two regions:
- (768, 196): Backup palettes (original colors preserved)
- (768, 226): Active palettes (may be tinted by goggles/effects)

### 1.2 Pixel Format: RGB555 + Semi-Transparency

```
  15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
 +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 |STP|    Blue (5)    |    Green (5)   |     Red (5)    |
 +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
```

- Bit 15 (STP): Semi-transparency flag. When set AND semi-trans is enabled for the
  primitive, the pixel blends with the background.
- Color 0x0000 is fully transparent (never drawn).

### 1.3 Ordering Table (OT)

The PSX GPU renders via **ordering tables** -- linked lists of GPU command packets sorted
by depth. Each OT entry is a 32-bit word where:
- Bits 0-23: Pointer to next packet (24-bit PSX RAM address)
- Bits 24-31: Packet size in 32-bit words

```
  OT[0] (far)  --> prim_a --> prim_b --> ...
  OT[1]        --> prim_c --> ...
  ...
  OT[N] (near) --> prim_d --> terminator (0xFFFFFF)
```

`DrawOTag(&OT[N])` sends the entire chain to the GPU in back-to-front order (painter's
algorithm). On PSX, this is a DMA transfer -- the GPU processes packets asynchronously.

### 1.4 GPU Primitive Types

| Code  | Type      | Description                     | Size (words) |
|-------|-----------|---------------------------------|-------------|
| 0x20  | POLY_F3   | Flat-shaded triangle            | 4           |
| 0x24  | POLY_FT3  | Flat-textured triangle          | 7           |
| 0x28  | POLY_F4   | Flat-shaded quad                | 5           |
| 0x2C  | POLY_FT4  | Flat-textured quad              | 9           |
| 0x30  | POLY_G3   | Gouraud-shaded triangle         | 6           |
| 0x34  | POLY_GT3  | Gouraud-textured triangle       | 9           |
| 0x38  | POLY_G4   | Gouraud-shaded quad             | 8           |
| 0x3C  | POLY_GT4  | Gouraud-textured quad           | 12          |
| 0x40  | LINE_F2   | Flat-shaded line                | 3           |
| 0x60  | TILE      | Filled rectangle (variable)     | 3           |
| 0x64  | SPRT      | Textured sprite (variable)      | 4           |
| 0x68  | TILE_1    | 1x1 pixel                       | 2           |
| 0x70  | TILE_8    | 8x8 filled rect                 | 2           |
| 0x74  | SPRT_8    | 8x8 sprite                      | 3           |
| 0x78  | TILE_16   | 16x16 filled rect               | 2           |
| 0x7C  | SPRT_16   | 16x16 sprite                    | 3           |
| 0xE1  | DR_TPAGE  | Set texture page                | 1           |
| 0xE3  | --        | Set draw area top-left          | 1           |
| 0xE4  | --        | Set draw area bottom-right      | 1           |
| 0xE5  | --        | Set draw offset                 | 1           |

**Semi-transparency bit**: Bit 1 of the code byte (0x02) enables blending. The blend
mode (ABR) is set via the texture page register:

| ABR | Formula         | Effect         |
|-----|-----------------|----------------|
| 0   | (B + F) / 2     | 50% blend      |
| 1   | B + F           | Additive       |
| 2   | B - F           | Subtractive    |
| 3   | B + F/4         | Quarter add    |

Where B = background pixel, F = foreground pixel. Each channel (R5G5B5) is computed
independently and clamped to [0, 31].

## 2. PSX Rendering Pipeline (DG System)

The original MGS rendering uses a 7-stage pipeline called the **DG system**. Each stage
is a "channel function" called by `DG_RenderPipeline()`:

```
DG_RenderPipeline(GV_Clock)
   |
   +-- Stage 0: DG_ScreenChanl    <-- Camera transform (eye_inv x world)
   +-- Stage 1: DG_BoundChanl     <-- Frustum culling (bounding box test)
   +-- Stage 2: DG_TransChanl     <-- Batch vertex transformation via GTE
   +-- Stage 3: DG_ShadeChanl     <-- Per-vertex lighting via GTE
   +-- Stage 4: DG_PrimChanl      <-- Build GPU packets (POLY_GT4, etc.)
   +-- Stage 5: DG_DivideChanl    <-- Subdivide large polygons
   +-- Stage 6: DG_SortChanl      <-- Z-sort packets into ordering table
```

### 2.1 Stage 0: DG_ScreenChanl -- Camera Transform

For each queued object set (`DG_OBJS`), computes the screen-space matrix:

```
obj->screen = eye_inv x objs->world x per_model_offset
```

Uses GTE hardware: `gte_CompMatrix()` for matrix composition. The result is stored
in each `DG_OBJ.screen` matrix.

**DG_AdjustOverscan**: After computing the screen matrix, the Y-axis is scaled by
58/64 to account for PAL/NTSC overscan. This scaling MUST happen AFTER the matrix
multiply (scaling the view matrix before multiply gives different results).

```c
// CORRECT (PSX behavior) -- port uses this:
screen = eye_inv x world;
screen.m[1][0..2] *= 58/64;
screen.t[1] *= 58/64;

// WRONG (port had this bug previously):
eye_adj = eye_inv;
eye_adj.m[1][0..2] *= 58/64;  // Scaling view before multiply!
screen = eye_adj x world;     // Different result!
```

### 2.2 Stage 1: DG_BoundChanl -- Frustum Culling

Tests each object's bounding box against the view frustum:

1. Transform 8 AABB corners via GTE `gte_rtpt()` (3 vertices at a time, 3 batches)
2. Compute screen-space min/max from the 8 projected points
3. Test against viewport bounds: X in [-160, 161], Y in [-112, 113]
4. Check Z-buffer coverage (8 depth values at scratchpad 0x6C)

Returns `bound_mode`:
- 0 = completely outside (culled)
- 1 = partially visible (clip edges)
- 2 = fully visible

**Scratchpad layout during culling** (offsets from SCRPAD_ADDR):
```
0x00-0x17: DG_BOUND (24 bytes) -- min/max AABB as 6 ints
0x18-0x2F: SVECTOR[3] -- 3 corner vertices for GTE batch input
0x30-0x5B: DG_VECTOR[4] -- screen XY output (first entry is garbage)
0x3C:      <-- DVECTOR[8] read starts here (skips garbage at 0x30)
0x60-0x8B: DG_VECTOR[4] -- screen Z output
0x6C:      <-- int[8] Z-test read starts here
```

**Port status**: Works correctly on 64-bit. All types used in scratchpad
(`DG_VECTOR`=12 bytes, `SVECTOR`=8 bytes, `DVECTOR`=4 bytes) have identical sizes
on both 32-bit and 64-bit.

### 2.3 Stage 2: DG_TransChanl -- Vertex Transformation

Batch-transforms all vertices for visible objects using GTE:

```c
gte_SetRotMatrix(&obj->screen);
gte_SetTransMatrix(&obj->screen);
gte_ldv3c(vertices);     // Load 3 vertices
gte_rtpt_b();            // Transform 3 vertices in parallel
gte_stsxy3c(output_xy);  // Store screen XY
gte_stsz3c(output_z);    // Store Z for depth sorting
```

Processes up to 126 vertices per batch (scratchpad size constraint).

### 2.4 Stage 3: DG_ShadeChanl -- Lighting

Applies per-vertex lighting using GTE normal color operations:

```c
gte_ldv0(normal);  // Load vertex normal
gte_ncds();        // Normal Color with Depth Shading
gte_strgb(color);  // Store computed RGB
```

The light matrix and background color are loaded into GTE control registers.
MGS uses 3 directional lights per object.

### 2.5 Stage 4: DG_PrimChanl -- GPU Packet Building

Builds POLY_GT4 packets with:
- Screen-space XY coordinates (from DG_TransChanl)
- Per-vertex colors (from DG_ShadeChanl)
- Texture UV coordinates and CLUT/tpage references
- Semi-transparency flags

Also handles NCLIP (backface culling): `gte_nclip()` computes the signed area of the
triangle formed by 3 screen vertices. Negative area = back-facing = skip.

### 2.6 Stages 5-6: DG_DivideChanl + DG_SortChanl

- **Divide**: Splits polygons that span a large depth range into smaller pieces to
  reduce sorting artifacts (PSX has no Z-buffer -- relies on painter's algorithm).
- **Sort**: Inserts each GPU packet into the ordering table at its Z-depth index.

## 3. Port Software Renderer

The port **does not replace** the DG pipeline. Stages 0-6 still run from the original
source code. The port adds a **parallel software renderer** (`port_RenderObjects`) that
reads the same object data and renders directly to the VRAM framebuffer.

### 3.1 Why a Parallel Renderer?

The original pipeline builds GPU packets (POLY_GT4, etc.) and inserts them into the OT.
On PSX, `DrawOTag()` sends these to the GPU. On the port, `port_DrawOTag()` walks the
OT and renders 2D primitives (TILE, SPRT, text) to VRAM. But the GPU packet format for
3D primitives (POLY_GT4) uses screen-space vertices from the PSX pipeline. The port's
software renderer re-projects vertices from world space each frame using the same
`eye_inv` and `world` matrices.

### 3.2 Rendering Flow

```
port_RenderObjects(idx)
  |
  +-- For each DG_OBJS in channel queue:
  |    +-- Skip if: NULL, freed (poison pointer), n_models invalid
  |    +-- Skip if: DG_FLAG_INVISIBLE set
  |    +-- Skip if: group_id doesn't match DG_CurrentGroupID
  |    |
  |    +-- For each DG_OBJ (sub-model):
  |         +-- Skip if: no model data, no vertices
  |         |
  |         +-- Compute screen_mat:
  |         |    if obj->world.m[] is non-zero:
  |         |        screen_mat = eye_inv x obj->world
  |         |    else:
  |         |        screen_mat = eye_inv x objs->world (parent)
  |         |    Apply Y-overscan AFTER multiply:
  |         |        screen_mat.m[1][*] *= 58/64
  |         |        screen_mat.t[1] *= 58/64
  |         |
  |         +-- For each face (quad):
  |              +-- Read 4 vertex indices from vindices[]
  |              +-- Project all 4 vertices: world -> screen
  |              +-- Off-screen rejection: skip if all outside [-160,160]x[-112,112]
  |              +-- Backface cull: skip if signed area <= 0
  |              |   (unless DG_MODEL_BOTHFACE flag set)
  |              +-- Lookup texture (DG_TEX) from materials[fi]
  |              +-- Compute UV coordinates from texcoords[fi*8]
  |              +-- Split quad into 2 triangles: (v0,v1,v3) and (v1,v2,v3)
  |              +-- Rasterize via draw_flat_tri()
  |
  +-- Statistics: drawn_faces / total_faces / objs count
```

### 3.3 Perspective Projection

```c
static int project(MATRIX *screen, SVECTOR *vert, int dist, int *sx, int *sy, int *sz)
{
    int cx, cy, cz;
    mat_transform(screen, vert, &cx, &cy, &cz);
    if (cz < 4) cz = 4;  // Clamp near plane (no rejection -- prevents pop-in)
    *sx = (int)((long long)cx * dist / cz);
    *sy = (int)((long long)cy * dist / cz);
    *sz = cz;
    return 1;
}
```

- `dist` = `chanl->clip_distance` (projection distance, set by `DG_Clip`)
- `mat_transform` = 3x3 rotation + translation (fixed-point 4.12)
- Uses `long long` for intermediate products to prevent 32-bit overflow
- Near-plane clamped to 4 instead of rejecting (PSX uses DG_DivideChanl for this)
- Screen center is (0,0); framebuffer offset (+160, +112) applied after culling

### 3.4 Triangle Rasterizer (draw_flat_tri)

Located in `vram.c`. Implements scanline-based affine texture mapping:

```
draw_flat_tri(x0,y0, x1,y1, x2,y2, color)
  |
  +-- Sort vertices by Y (top to bottom)
  +-- Interpolate UV if textured (16.16 fixed-point)
  |
  +-- For each scanline Y:
       +-- Compute left/right X from edge interpolation
       +-- Clip to draw area (E3/E4 GPU commands)
       |
       +-- For each pixel X:
            +-- If textured:
            |    +-- Compute U,V from fixed-point interpolation
            |    +-- Lookup texel from VRAM (4-bit, 8-bit, or 16-bit)
            |    |    4-bit: vram[base_y+v][base_x+(u>>2)], nibble, CLUT
            |    |    8-bit: vram[base_y+v][base_x+(u>>1)], byte, CLUT
            |    |   16-bit: vram[base_y+v][base_x+u] (direct color)
            |    +-- Skip if texel == 0 (transparent)
            |    +-- Z-test: skip if z > port_zbuf[y][x]
            |    +-- If semi-transparent && texel bit 15 set:
            |    |    Apply ABR blend mode
            |    +-- Write pixel + update Z-buffer
            |
            +-- If flat-shaded:
                 +-- Z-test
                 +-- Write solid color
```

**Texture mapping is affine** (not perspective-correct), matching PSX hardware exactly.
PSX has no perspective correction -- this is why textures "swim" on large polygons.

### 3.5 OT Walker (port_DrawOTag)

Handles 2D primitives added to the ordering table by game code (menus, HUD, subtitles,
codec UI, effects). Located in `vram.c`.

Decodes GPU command code byte and dispatches to appropriate renderer. Handles all
primitive types listed in section 1.4, plus GPU state commands (E1/E3/E4/E5).

For SPRT primitives, applies color modulation: each texel RGB is multiplied by the
primitive's RGB values and divided by 128 (neutral = 128,128,128).

**Primitive coverage**:
- `POLY_F3/F4` (0x20/0x28): flat-colored, vertex RGB reset to 128 before draw (prevents stale modulation from prior Gouraud prims)
- `POLY_G3/G4` (0x30/0x38): Gouraud-shaded with per-vertex color interpolation, passes `0x4210` as base color so modulation maps 0-255 → 0-31 correctly
- `POLY_FT3/FT4` (0x24/0x2C): textured flat-shaded; FT3 draws one triangle, FT4 draws two (split as v0-v1-v2 and v1-v3-v2)
- `POLY_GT3/GT4` (0x34/0x3C): textured Gouraud-shaded with per-vertex color + UVs
- All polygon cases check `code & 0x02` for semi-transparency and read ABR blend mode from `tpage` (or from `port_current_tpage` for non-textured prims)

**Coordinate convention**: Polygon cases pass raw primitive coordinates to `draw_flat_tri`.
Do NOT add `draw_x`/`draw_y` at the call site — `draw_flat_tri` applies the draw offset
internally. Doing both causes a double offset when the OT contains E5 commands (e.g., radar).

**Semi-transparency in `draw_flat_tri` non-textured path**: when `port_tex_semi_trans` is
set, the modulated color is blended with the framebuffer background using the PSX ABR mode:
- Mode 0: (B + F) / 2
- Mode 1: B + F (additive)
- Mode 2: B - F (subtractive)
- Mode 3: B + F/4 (quarter additive)

This is what makes radar vision cones render semi-transparent instead of opaque.

**z-test reset**: `port_DrawOTag` sets `port_current_z = 0` before walking the chain.
Without this, during codec (`DG_FrameRate == 2`) `port_RenderObjects` is skipped so
`port_zbuf` retains stale values — causing face pixels to be z-rejected and the codec
face to render corrupted. 2D OT prims should always pass z-test (they're drawn on top).

### 3.6 OT on 64-bit: Handle Table

PSX OT entries encode 24-bit pointers. On 64-bit, addresses are 8 bytes. The port uses
a **handle table** to map small indices to full pointers:

```c
void *port_ot_table[PORT_OT_TABLE_SIZE];  // 2^18 = 262144 entries
int   port_ot_next = 0;                   // Allocation counter, reset each frame
```

### 3.7 Double-Buffer Lifecycle

```
Frame N (GV_Clock=0):              Frame N+1 (GV_Clock=1):
  Actors add prims -> OT[0]         Actors add prims -> OT[1]
  DG_RenderPipeline processes        DG_RenderPipeline processes
  port_RenderObjects: 3D render      port_RenderObjects: 3D render
  DG_DrawOTag(0): 2D render          DG_DrawOTag(1): 2D render
  DG_ClearChanlSystem(0)             DG_ClearChanlSystem(1)
```

### 3.8 DG_FrameRate and Codec Mode

`DG_FrameRate` controls rendering mode:
- **1** (normal): 3D rendering + OT rendering
- **2** (codec mode): OT rendering only (3D skipped)

When the codec (radio) opens, `DG_FrameRate` is set to 2. The port skips
`port_RenderObjects` when `DG_FrameRate == 2` so the codec UI is visible.

### 3.9 Deferred Clear

On PSX, `PutDrawEnv()` with `isbg=1` immediately clears the framebuffer. On the port,
this would clear the buffer AFTER the OT has been drawn (wrong order). Solution:

```
PutDrawEnv(isbg=1) -> stores deferred_clear flag + rect + color
game_tick start    -> port_apply_deferred_clear() fills VRAM region
```

## 4. Palette Effect System

### 4.1 How It Works on PSX

The goggle effects (night vision green, thermal red) transform CLUT palette entries
in VRAM each frame:

```
DG_StorePalette()           -> Copy active CLUTs (768,226) to backup (768,196)
DG_SetExtPaletteMakeFunc()  -> Register callback + conversion function
PaletteCallback()           -> Each frame:
                               Read backup CLUTs via StoreImage2
                               Transform each entry via PaletteConvert(color)
                               Write back to active CLUTs via LoadImage2
```

### 4.2 Port Implementation

`LoadImage2` and `StoreImage2` were previously no-ops. They now delegate to
`port_LoadImage`/`port_StoreImage` which read/write the VRAM array. This allows the
PSX palette callback code to run unmodified.

The goggle screen overlay (`scn_mask.c`) creates full-screen semi-transparent TILE
primitives. The port implements `port_DrawTileSemiTrans()` for PSX blend mode 0
(average of background and foreground per channel).

## 5. KMD Model Format

```
KMD File Structure:
+------------------+
|  flags, n_faces  |  DG_MDL header
|  min (3 ints)    |  AABB minimum
|  max (3 ints)    |  AABB maximum
|  pos (3 ints)    |  Local position offset
|  parent, extend  |  Skeleton hierarchy
|  n_verts         |
|  vertices_off    |  -> SVECTOR[] (4.12 fixed-point)
|  vindices_off    |  -> packed 4x7-bit indices per face
|  n_normals       |
|  normals_off     |  -> SVECTOR[] (unit normals)
|  nindices_off    |
|  texcoords_off   |  -> 8 bytes per face (U,V for 4 corners)
|  materials_off   |  -> u16[] texture hash IDs per face
+------------------+
```

**Vertex index packing**: Each face stores 4 vertex indices in one 32-bit word:
```
bits  0-6:  v0 (0-127)
bits  8-14: v1
bits 16-22: v2
bits 24-30: v3
```

**64-bit loading**: The port's `kmd_loader.c` reads 32-bit offsets from the binary and
converts them to 64-bit pointers.

## 6. Texture System

### 6.1 Texture Table

512-slot hash table: `DG_TexHashTable[512]`. Key = 16-bit hash of texture name.
Linear probing for collisions. Each entry is a `DG_TEX`:

```c
typedef struct {
    u_short id;      // Hash ID
    u_char  used;    // Reference count
    u_char  col;     // Color depth (0=4bit, 1=8bit, 2=16bit)
    u_short tpage;   // GPU tpage register value
    u_short clut;    // CLUT address in VRAM
    u_char  off_x;   // Texture rect X offset in page
    u_char  off_y;   // Texture rect Y offset
    u_char  w;       // Width - 1
    u_char  h;       // Height - 1
} DG_TEX;
```

### 6.2 Resident vs Nocache Textures

- **Resident** (`'r'` tag): Persists across stage transitions. Cached.
- **Nocache** (`'n'` tag): Loaded fresh each stage. Uploaded directly to VRAM.
