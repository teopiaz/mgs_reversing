# Changelog

Chronological log of major fixes and milestones in the PSX-to-macOS port.

## Port Foundation
1. Initial skeleton: SDL2 window, stub functions, compilation framework
2. Memory system: malloc'd pools replacing PSX hardcoded addresses (2MB normal, 512KB packet)
3. GTE emulation: all COP2 operations in C (gte_math.c + inline_n.h)
4. VRAM simulation: 1024x512 uint16_t array, LoadImage/StoreImage/ClearImage
5. Filesystem: stdio replacement for CD-ROM (STAGE.DIR parsing, DATACNF tag processing)

## Input & Pad
6. Signed char analog center bug: `(unsigned char)(128 - center)` instead of `(char)`
7. DG_HikituriFlagOld: changed from function stub to variable (was blocking DrawOTag)
8. GM_GameStatus STATE_PADRELEASE: added clearing to prevent permanent pad lockout

## Collision (HZD)
9. HZD binary loader: 32-bit offset-to-pointer conversion for 64-bit (hzd_loader.c)
10. HZD_VEC.long_access: changed from `long` to `int` for GTE compatibility
11. Dynamic collision: fixed 4-byte-per-entry allocation (sizeof(ptr) was 8 on 64-bit)
12. Stale nears[] pointers: reset when step_size==0 (macOS unmaps freed pages)
13. port_ptr_readable: Mach VM API guard for dangling HZD pointers
14. sna_8004E71C stack layout: vec/vec_saved must be contiguous array for line[1] access

## Rendering
15. Software 3D renderer: affine textured quads, Z-buffer, backface culling
16. OT handle table: 262K-entry pointer table for 64-bit OT addresses
17. DG_FrameRate: init changed from 2 to 1 (was permanently in codec mode)
18. DG_AdjustOverscan: Y-scaling (58/64) moved to AFTER eye_inv*world multiply
19. DG_FLAG_INVISIBLE: added to port_RenderObjects for FPV hide Snake
20. Semi-transparent TILE: port_DrawTileSemiTrans for goggle overlays (blend mode 0)
21. LoadImage2/StoreImage2: implemented (were no-ops), enabling palette callbacks
22. Deferred PutDrawEnv clear: applied at frame start, not after OT draw
23. DG_UnDrawFrameCount: capped to 10 (demothrd sets to 0x7FFF0000)
24. Texture table overflow: resident cache cleared properly after save

## Sound
25. SPU emulator: ADPCM decoder, 24 voices, ADSR from pcsx-redux rate tables
26. Gaussian interpolation: 512-entry coefficient table matching PSX SPU
27. Wave/SE/Song loading: .wvx/.e/.mdx file parsing with 64-bit struct conversion
28. Sequence command fix: `(unsigned char)mdata1 >= 128` (was signed, always false)
29. sd_mem_alloc: replaced PSX hardcoded 0x801E0000 with static buffer

## Cutscenes & Codec
30. 29 character types registered in MainCharacterEntries
31. Codec: SELECT button opens, DG_FrameRate==2 skips 3D rendering
32. Subtitles: jimctrl parses SubtitleHeader manually for 64-bit
33. FS_StreamGetSize: reads from header tag (stream-4), not data
34. str_tick_count: reset to 0 after StartStream for subtitle timing
35. Stream buffer: capped to 4MB (was loading entire rest of DEMO.DAT)
36. StartStream header: cast to unsigned char* to prevent sign extension

## Crash Fixes
37. litmdl_dg_def: wrapped in struct for flex array sizeof overflow (ASan)
38. DMO_ADJ buffer: increased from 16 to 32 entries
39. sna_8004E808: NULL map guard during goggles usage
40. RotMatrix: guard for freed DG_OBJS during stage transitions
41. Scratchpad alignment: __attribute__((aligned(16))) prevents ARM64 SIGBUS

## Stage & GCL
42. 88 stage overlays compiled statically (3 R-variants excluded)
43. GCL pointer table: 256-entry indirection for pointer-in-int storage
44. GCL NULL guards: GCL_GetOption, GCL_GetParam safety checks
45. Overlay symbol deconfliction: per-overlay -D prefix renames in Makefile

## OT Primitive Rendering (April 2026)
46. POLY_GT3/GT4: implemented in OT walker (was stubbed — broke water, reflections, title)
47. POLY_G3/G4: per-vertex Gouraud interpolation (was using flat first-vertex color)
48. POLY_FT3/FT4: split into separate cases (FT3 drew garbage second triangle)
49. Double draw_x/draw_y offset: removed caller-side add in polygon cases (draw_flat_tri adds internally)
50. POLY_F3/F4: set neutral vertex colors to prevent stale modulation
51. POLY_FT3/FT4: check code&0x02 for semi-trans (was hardcoded off)
52. POLY_G3/G4: added semi-transparency support + ABR blend mode from port_current_tpage
53. draw_flat_tri non-textured path: added semi-transparency blending (4 PSX blend modes)
54. DG_ShadePacksIndirect: PORT_BUILD path uses GTE colors (avoids 32-bit pointer cast in r0/g0/b0)
55. port_RenderObjects: iterate all 3 channels (0=bg, 1=main, 2=overlay) — was channel 1 only
56. port_DrawOTag: reset port_current_z=0 so 2D OT prims pass z-test (fixes codec face corruption)

## GTE & 64-bit Fixes (April 2026)
57. Radar walls: manual OT linking `(int)(pLine)&0xffffff` replaced with setlen/addPrim handle-based
58. gte_ldv0h: also set IR1/IR2/IR3 (was only setting V0, causing gte_rt to read stale IR)
59. gte_stlvnl: shift MAC >> 12 to match PSX sf=1 output — fixes Nikita missile spawn position
