# Shading, Lighting, and Vertex Colors

This document describes how Metal Gear Solid's PSX lighting system works, how the port
implements it, and the known 64-bit issues that affect color rendering.

## 1. PSX Lighting Architecture

MGS uses the GTE (Geometry Transform Engine) coprocessor for all lighting calculations.
The system has three layers:

### 1.1 Light Matrices (Environment Lighting)

Each `DG_OBJS` has a `light` field pointing to two MATRIX structs:

```
objs->light[0] = Light Direction Matrix (rotation part: 3 light direction vectors)
objs->light[1] = Color Matrix (3 light colors, scaled by 16)
objs->light[0].t[] = Ambient/back color (if DG_FLAG_AMBIENT is set)
```

The light system (`source/libdg/light.c`) manages:
- **DG_LightMatrix**: Global 3x3 rotation matrix storing the main light direction
- **DG_ColorMatrix**: 3x3 matrix storing 3 directional light colors
- **DG_Ambient**: Global ambient color as SVECTOR (vx=R, vy=G, vz=B)
- **FixedLights** (8 max): Persistent lights per scene
- **TempLights** (8 max): Dynamic lights per frame (double-buffered)

### 1.2 GTE Normal Color Operations

The shade pipeline uses GTE NCS/NCT to compute per-vertex lighting:

```
Input:  vertex normal (SVECTOR), light matrix, color matrix, back color
Output: RGB color (CVECTOR, 0-255 per channel)

Formula:
  IR = LightMatrix * Normal           (light intensity per direction)
  RGB = ColorMatrix * IR + BackColor   (colored lighting + ambient)
  Clamp each channel to [0, 255]
```

Key GTE operations:
- `gte_ncs()` / `gte_nct()`: Normal Color Single/Triple (1 or 3 normals at once)
- `gte_ncds()` / `gte_ncdt()`: Normal Color with depth fog interpolation
- `gte_SetRotMatrix(light_dir)`: Load light direction matrix
- `gte_SetColorMatrix(light_color)`: Load color matrix
- `gte_SetBackColor(r, g, b)`: Set ambient color

### 1.3 PSX GPU Texture Modulation

The PSX GPU multiplies texture colors by vertex colors when rendering POLY_GT4:

```
output_channel = texel_channel * vertex_color / 128

Where:
  texel_channel: 5-bit (0-31) from VRAM texture lookup
  vertex_color:  8-bit (0-255), where 128 = neutral (1.0x)
  output:        5-bit (0-31), clamped

Effect:
  vertex_color = 0   -> black (fully shadowed)
  vertex_color = 128 -> original texture color (neutral)
  vertex_color = 255 -> ~2x brightness (overbright, clamped)
```

## 2. Pipeline Stages for Lighting

The rendering pipeline (`DG_ChanlUnits[]` in `source/libdg/chanl.c`) has 7 stages.
Lighting involves stages 1, 3, and game-code calls:

### 2.1 Stage 1: DG_BoundChanl (Packet Allocation)

`source/libdg/bound.c`

- Allocates POLY_GT4 packets for visible objects
- Calls `DG_MakeObjPacket()` which:
  - `DG_InitPolyGT4Pack()`: Sets GPU command code (0x3C)
  - `DG_WriteObjPacketUV()`: Writes texture UVs (if `DG_FLAG_TEXT`)
  - `DG_WriteObjPacketRGB()`: Copies `obj->rgbs` to pack colors (if `DG_FLAG_PAINT`)

### 2.2 Stage 3: DG_ShadeChanl (Environment Shading)

`source/libdg/shade.c`

Processes objects with `DG_FLAG_SHADE`:

```c
for each OBJS in queue:
    if (objs->bound_mode == 0) skip       // frustum culled
    if (!(objs->flag & DG_FLAG_SHADE)) skip

    SetRotMatrix(objs->light[0])           // light directions
    SetColorMatrix(objs->light[1])         // light colors
    if (DG_FLAG_AMBIENT)
        SetBackColor(objs->light.t[])      // per-object ambient

    for each model:
        if (obj->bound_mode == 0) skip
        SetLightMatrix(obj->world * light)  // world-space light directions

        // Compute per-normal colors via GTE
        for each normal (3 at a time):
            gte_nct_b()                    // Normal Color Triple
            store RGB to scratchpad

        // Copy colors from scratchpad to POLY_GT4 packs
        DG_ShadePacks(nindices, packs, n_packs)
```

**Important**: `DG_ShadePacks` checks `pack->tag & 0xFFFF` and skips faces where
this is 0. On PSX, the trans stage sets the tag to `area|Z`. In the port, the trans
stub sets `tag |= 1` to ensure shade processes all faces.

#### DG_ShadePacksIndirect (64-bit broken)

For models with `DG_MODEL_INDIRECT` flag (extend chains), `DG_ShadePacksIndirect`
reads `r0,g0,b0,code` as a **32-bit pointer** and dereferences it:

```c
color = **(int **)&packs->r0;  // reads 4 bytes as pointer address
```

On 64-bit, pointers are 8 bytes. This reads a truncated address and dereferences
garbage memory, producing random color values. **The port skips shade colors for
DG_MODEL_INDIRECT models**, falling back to neutral (128,128,128).

### 2.3 Preshading (Dynamic Character Lighting)

`source/libdg/pshade.c`

Called by game actors (e.g., `source/takabe/lit_mdl.c`) for characters near dynamic
light sources. Unlike the shade pipeline, preshading considers point lights:

```c
DG_MakePreshade(objs, lights, n_lights):
    1. For each vertex, find up to 2 nearby lights (distance check)
    2. Store light intensity + color per vertex in DG_LitVertex
    3. For each face vertex:
       - Build custom light/color matrices from the 2 nearest lights
       - gte_ncs() to compute final color
       - Store to cvec array (obj->rgbs)
    4. DG_WriteObjPacketRGB() copies rgbs to POLY_GT4 packs
```

Preshaded objects have `DG_FLAG_PAINT` set and `DG_FLAG_SHADE` cleared (via
`DG_UnShadeObjs`), so the shade pipeline skips them. Their colors come exclusively
from `obj->rgbs`.

**Output format**: 4 CVECTORs per face (one per quad vertex), in KMD vertex order:
```
rgbs[fi*4 + 0] = color for vertex 0 (vindices byte 0)
rgbs[fi*4 + 1] = color for vertex 1 (vindices byte 1)
rgbs[fi*4 + 2] = color for vertex 2 (vindices byte 2)
rgbs[fi*4 + 3] = color for vertex 3 (vindices byte 3)
```

## 3. Port Implementation

### 3.1 Vertex Color Sources (Priority Order)

The port renderer (`port/libdg/libdg_stub.c`, `port_RenderObjects`) reads per-vertex
colors from three sources in priority order:

1. **`obj->rgbs` + `DG_FLAG_PAINT`**: Preshaded characters with dynamic lighting.
   Read directly from the CVECTOR array (4 per face, KMD vertex order).

2. **POLY_GT4 packs + `DG_FLAG_SHADE`**: Level geometry shaded by the pipeline.
   Read from pack fields `r0/g0/b0` through `r3/g3/b3`.
   Skipped for `DG_MODEL_INDIRECT` models (64-bit pointer bug).
   Pack vertex order has v2/v3 swapped vs KMD: `r2=KMD_v3, r3=KMD_v2`.

3. **Neutral 128**: Fallback when no color data is available.
   Produces unmodified texture colors (128/128 = 1.0x modulation).

### 3.2 Gouraud Interpolation

`port/libdg/vram.c`, `draw_flat_tri()`

Per-vertex colors are interpolated across each triangle using the same scanline
approach as texture UV interpolation:

- **Vertical**: Colors interpolated along triangle edges (long edge + short edges)
- **Horizontal**: Colors interpolated across each scanline via 16.16 fixed-point
- **Modulation**: At each pixel, `output = texel * interp_color / 128` per channel
- **Clamping**: Interpolated colors clamped to >= 0 (prevents underflow at edges)
- **Semi-transparency bit**: Preserved from original texel through modulation

### 3.3 Quad Splitting and Vertex Order

The port splits each POLY_GT4 quad into 2 triangles using KMD vertex order:

```
KMD quad vertices: v0, v1, v2, v3

Triangle 1: (v0, v1, v3)  with colors cr[0], cr[1], cr[3]
Triangle 2: (v1, v2, v3)  with colors cr[1], cr[2], cr[3]
```

PSX POLY_GT4 vertex order (v2/v3 swapped vs KMD):
```
Pack field:  r0    r1    r2    r3
KMD vertex:  v0    v1    v3    v2
```

## 4. Shadow System

`source/chara/snake/shadow.c`

Snake's shadow is a separate actor that renders a projected quad:
- Flags: `DG_FLAG_TEXT | DG_FLAG_TRANS | DG_FLAG_SHADE | DG_FLAG_AMBIENT | DG_FLAG_ONEPIECE`
- Uses GTE matrix transforms to project shadow vertices onto the ground plane
- Rendered as a semi-transparent textured quad (ABR blend mode from tpage)
- The shadow DG_OBJS has its own `light[2]` matrices for ambient control

## 5. Known 64-bit Issues

### 5.1 DG_ShadePacksIndirect Pointer Truncation

**File**: `source/libdg/shade.c:77,88,99,110`
**Status**: Worked around (skip DG_MODEL_INDIRECT in renderer)

The indirect shade path reads pack color fields as 32-bit pointers. On 64-bit,
this produces truncated addresses. Affects character extend-chain models.

### 5.2 DG_PVECTOR Struct Size

**File**: `port/libdg/libdg.h:39-43`
**Status**: Fixed (`long` -> `int`)

`DG_PVECTOR` must be 8 bytes (matching SVECTOR) for trans.c vertex array casting.
`long` is 8 bytes on 64-bit, making the struct 16 bytes. Fixed to use `int`.

### 5.3 DG_WriteObjVerticesIndirect Scratchpad Pointers

**File**: `source/libdg/trans.c:32-38`
**Status**: Fixed (use SPAD struct access)

Raw scratchpad offsets 0x3F8/0x3FC read wrong bytes on 64-bit because the
`parent_packs` pointer at offset 0x3F8 is 8 bytes, pushing `vertices` to 0x400.

### 5.4 gte_stsxy3 Macro Offsets

**File**: `port/psx/inline_n.h:547-559`
**Status**: Fixed

The PSX inline assembly writes screen XY at type-specific offsets:
- F3/F4: consecutive at offsets 8, 12, 16
- G3/G4/FT3/FT4: offsets 8, 16, 24 (stride 8)
- GT3/GT4: offsets 8, 20, 32 (stride 12)

The port previously aliased all types to the F3 version (consecutive), which
corrupted UV and color fields in POLY_GT4 packets.

### 5.5 Trans Stage Not Yet Enabled

**Status**: Stubbed (sets pack tags only)

DG_TransChanl is stubbed because the GTE projection formula produces different
screen coordinates than the port's software projection. The stub sets `tag |= 1`
for all packs so the shade pipeline processes every face. Full trans.c enablement
requires resolving the GTE projection scale factor (H * 0x20000 vs H * 0x10000).

## 6. File Reference

| File | Role |
|------|------|
| `source/libdg/light.c` | Light matrix management, fixed/temp lights |
| `source/libdg/shade.c` | Per-vertex shading via GTE (pipeline stage 3) |
| `source/libdg/pshade.c` | Character preshading with dynamic point lights |
| `source/libdg/opack.c` | POLY_GT4 packet allocation and UV/color writing |
| `source/chara/snake/shadow.c` | Snake shadow projection actor |
| `source/enemy/glight.c` | Enemy gun light effect |
| `source/takabe/lit_mdl.c` | Dynamic light actor (calls DG_MakePreshade) |
| `port/libdg/libdg_stub.c` | Port renderer (reads colors from packs/rgbs) |
| `port/libdg/vram.c` | Software rasterizer with Gouraud shading |
| `port/psx/gte_math.c` | Software GTE (NCS, NCT, NCDS lighting ops) |
| `port/psx/inline_n.h` | GTE macro implementations (stsxy3 fixes) |
