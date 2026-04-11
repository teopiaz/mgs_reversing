# Rendering Pipeline Findings

Findings from reviewing the port's graphics pipeline against the original PSX code.

## Fixed Issues

### 1. POLY_GT3/GT4 stubbed in OT walker (`vram.c`)
Gouraud-textured polygons were completely skipped. Used by water surfaces, environment maps, title screen, binoculars, katana, cape physics, IR sensor. Now fully implemented with per-vertex color + texture.

### 2. POLY_G3/G4 flat instead of Gouraud (`vram.c`)
Used first vertex color for entire polygon. Now properly interpolates per-vertex colors and splits quads into two triangles.

### 3. POLY_FT3 drew garbage second triangle (`vram.c`)
FT3 (triangle) and FT4 (quad) shared the same case, always drawing two triangles. FT3 read past its data into adjacent OT memory for the non-existent 4th vertex. Split into separate cases.

### 4. Double draw_x/draw_y offset (`vram.c`)
FT3/FT4, GT3/GT4, G3/G4 cases added draw_x/draw_y to coordinates before calling `draw_flat_tri`, which also adds the offset internally. Polygons were shifted when draw offset was non-zero (e.g., radar E5 commands). Removed the caller-side addition.

### 5. POLY_F3/F4 stale vertex color modulation (`vram.c`)
F3/F4 never set `port_tri_r/g/b`, so previous Gouraud primitive colors leaked through the modulation formula. Fixed by setting neutral 128 before drawing.

### 6. POLY_FT3/FT4 semi-transparency disabled (`vram.c`)
`port_tex_semi_trans` was hardcoded to 0. Transparent FT prims (glass, fades) rendered opaque. Now checks `code & 0x02` and reads ABR blend mode from tpage.

### 7. DG_MODEL_INDIRECT shading crash on 64-bit (`shade.c`)
`DG_ShadePacksIndirect` dereferences `r0/g0/b0/code` (4 bytes) as a 32-bit pointer on PSX. On 64-bit this is impossible. Added PORT_BUILD path that always uses GTE-computed normal colors, losing the override mechanism but avoiding crashes. Removed the blanket skip of INDIRECT models in the renderer.

### 8. Only channel 1 rendered for 3D (`libdg_stub.c`)
`port_RenderObjects` only iterated `DG_Chanls[1]`. Channels 0 (background) and 2 (overlay) were ignored. Extracted per-channel logic into `port_RenderChanl` and loop over all 3 channels.

### 9. Codec face corruption (`vram.c`)
During codec (`DG_FrameRate == 2`), `port_RenderObjects` is skipped so `port_zbuf` is never cleared. Stale z-values from the previous 3D frame caused `draw_flat_tri`'s z-test to reject face pixels. Fixed by setting `port_current_z = 0` at the start of `port_DrawOTag`.

### 10. Radar walls not appearing (`radar.c`)
Two issues:
- **OT linking**: `*ot2 = (int)(pLine) & 0xffffff` truncated 64-bit pointers. Replaced with `setlen`/`addPrim` handle-based linking under PORT_BUILD.
- **GTE gte_ldv0h**: Local macro loaded V0 but didn't copy to IR1/IR2/IR3. Since `gte_rt()` reads from IR registers, all wall line endpoints got stale values producing zero-length degenerate lines. Added IR1/IR2/IR3 assignment.

### 11. Indirect vertex faces missing (e.g., s02c floor) (`libdg_stub.c`)
PSX models use "indirect" vertex indexing (bit 7 set in vindices byte) for sub-models that share vertices with a parent model. The original code reads projected vertex positions from `SPAD->parent_packs` — the parent object's already-transformed POLY_GT4 packs. The port renderer used `& 0x7F` which gave wrong vertex indices from the wrong vertex buffer. Fixed by detecting indirect vertex flags and projecting using the parent model's vertex buffer and world matrix (looked up via `mdl->parent` index).

## Remaining Known Gaps

### Major — s02c Missing Floor/Walls
Stage s02c has a map object (`DG_FLAG_ONEPIECE`) where `objs->world` = identity with `t=(0,0,0)`, and model vertices are in absolute world coordinates (e.g., 19500, 4250, -2000). The port's projection `eye_inv * identity` produces screen coords in the thousands (way beyond ±320/±224 cull range). On PSX, `DG_ScreenChanl` stores the adjusted eye_inv into scratchpad and uses GTE `CompMatrix` to compute `obj->screen`, which produces correct results. The port's scratchpad mmap fails, so `DG_ScreenChanl`'s scratchpad-based computation is unreliable for non-ONEPIECE objects. Using `obj->screen` for ONEPIECE fixes s02c but breaks Snake (whose `obj->screen` is garbage due to scratchpad failure). **Root cause: scratchpad mmap failure.** Fixing the scratchpad allocation would likely fix this and other subtle GTE pipeline issues.

### Minor
- **DG_FLAG_IRTEXTURE** (thermal goggles) — not handled
- **DR_TWIN** (texture window) — not processed in OT walker  
- **LINE_G2 interpolation** — uses flat first-vertex color instead of interpolated
- **No dithering** — PSX used ordered dithering for 15-bit color banding

### Not Applicable
- **DG_DivideChanl** (polygon subdivision) — PSX used this for painter's algorithm Z-accuracy; port has Z-buffer so not needed
- **Perspective-correct texture mapping** — PSX also used affine, so this matches
