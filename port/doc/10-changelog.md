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

## 3D Gouraud Lighting (April 2026)
60. push_rgb_fifo: use IR>>4 instead of MAC>>4 — the port stores pre-(>>12) MAC so
    the original formula was off by 4096x, saturating every NCS output channel to
    0 or 255 (actors rendered either pitch-black or full-bright). Now produces
    correct 8-bit color bytes.
61. port_RenderObjects: call DG_BoundChanl / DG_TransChanl / DG_ShadeChanl per
    channel before submission. Previously none of the lighting stages ran, so
    POLY_GT4 packs stayed at DG_InitPolyGT4Pack's 0x80 neutral and every SHADE
    object rendered unmodulated. Wired-up pipeline fills r0..b3 with real Gouraud
    values for every DG_FLAG_SHADE object.
62. DG_BoundChanl GBOUND test: the PSX GTE rtpt_b + scratchpad store used by the
    group-level frustum test produces wrong screen coords on 64-bit, culling
    every GBOUND-flagged `DG_OBJS` to `bound_mode=0`. That blocked
    DG_MakeObjPacket from allocating packs, which in turn blocked shade from
    having anywhere to write. DG_BoundObjs already had a PORT_BUILD skip for the
    same reason; mirrored it in DG_BoundChanl so the group flag is trusted
    unconditionally.

## Widescreen (April 2026)
63. ImGui toggle for 16:9 Hor+ widescreen mode: when enabled, internal render
    width grows from 320 to 400, the GL 3D shader's uHalfScreen widens to
    (200,112), and the 2D shader gets a uXScale uniform that pillar-boxes HUD
    into the 4:3 centre of the wider frame. Toggled via Renderer tab →
    Quality/Output → "Widescreen (16:9 Hor+)". No PSX-side changes required
    (3D Hor+ is achieved purely via the GL shader's half-screen uniform).

## GL Renderer Polish (April–May 2026)
64. Standalone GLSL files (`libdg/shaders/{blit,tri3d,tri2d}.{vert,frag}`)
    with on-disk loading + embedded fallbacks. F5 in the ImGui debug panel
    triggers `gl_renderer_reload_shaders` for iterating on shaders without
    a rebuild.
65. fb-readback detection centralised in `port_is_fb_readback_tpage`
    (tp=2, base_y=0, base_x<640 = pointer into displayed framebuffer).
    Both `gl_submit_tri2d` and `gl_submit_tri3d` use it to OR bit 5 into
    the vertex flags; the 2D shader takes the blur sample path, the 3D
    shader takes the Stealth/Optical-Camo sample path.
66. 3D framebuffer-readback (kogaku2 Optical Camo): 3D run-batcher
    snapshots the FBO into `g_prev_fb_tex` mid-frame, right before the
    fb-readback run draws, so Stealth samples the scene without Snake
    (no positive-feedback accumulation across frames).
67. 2D framebuffer-readback (NewBlur / NewBlurPure): end-of-frame
    `capture_prev_fb` captures the final FBO into `g_prev_fb_tex` for the
    next frame's blur sample. Soft alpha-gated blend (smoothstep on
    per-pixel brightness) so a black-cleared FBO doesn't fade the scene
    out at cutscene starts. ImGui Effects panel exposes a checkbox +
    strength slider (default 1.4, PSX-exact = 2.0, 0 = off).
68. PSX struct static_asserts: `port/psx_static_asserts.c` pins the size
    and field offsets of `MATRIX`, `SVECTOR`, `VECTOR`, `POLY_GT4`,
    `DG_CHANL`, etc. so 32-bit→64-bit drift fails the build instead of
    failing in software.
69. `Square0(VECTOR*)` signature: fixed in `c15fdf248` — the PSX SDK
    declares its argument as `VECTOR*` (16 bytes, with a pad field), not
    `SVECTOR*` (8 bytes). Calling code passed a `VECTOR*` and the
    mismatched read produced bogus length; broke the elevator-panel
    text rendering and anywhere `GV_VecLen3` was used.

## Demo / Cutscene Debugging (May 2026)
70. ImGui Demo tab: per-frame scrubber that drives the streaming cinema
    forward/backward from disk, "dump snake render state" button, and
    `PORT_DEMO_PAUSE_AT=NNN` env var to freeze cinema at a specific
    frame for visual diffing.
71. `str_tick_count` slaved to the audio cursor (cb5a3b987 / 7063ea697):
    fixes lipsync drift and out-of-sync subtitles. The PSX scheduler
    advances `str_tick_count` 1-for-1 with audio samples drained — the
    port's coarser game-tick increment let video and audio drift apart
    over a long cutscene.
72. d00a snake-position investigation captured in
    `doc/demo/11-d00a-snake-position-investigation.md` — port renders
    f847 ~3× tighter than PSX despite identical math; demo data flow,
    reproduction, hypotheses written up. Investigation reverted; doc
    kept for resumption.

## Pre-game Menu + Persisted Config (May 2026)
73. Splash → Main → Options/Controls ImGui screens running in a
    pre-game loop before `game_init()`. `port_menu.cpp` owns its own
    ImGui frame and exits to either `PORT_MENU_GAME` (start engine) or
    `PORT_MENU_QUIT`.
74. `port_config.h` / `port_config.c` — `PortConfig` struct (video,
    audio, kb_map, pad_map, language, …) persisted to
    `./port_config.ini`. Hand-editable; values are raw SDL enum
    integers for portability. INI section: `[video]`, `[audio]`,
    `[game]`, `[keyboard]`, `[gamepad]`.
75. Per-button remapping: `port_update_pad` (`port/mts/mts.c`) now
    iterates `g_port_config.kb_map[]` / `pad_map[]` instead of the old
    hard-coded SDL_SCANCODE_* / SDL_CONTROLLER_BUTTON_* tables.
    Defaults match the previous values verbatim. PORT_BTN_COUNT = 14
    PSX buttons.
76. Language selector (`OPTION_ENGLISH`, 0x0100 in `GM_OptionFlag`):
    applied right before `game_init()` so the title screen and codec
    boot in the chosen language. In-game Options screen + save-load
    still override at runtime.
77. GL init safe-defaults fallback: if the saved video config produces
    a broken window/context, `main.c` resets to 1280×896 / windowed /
    GL scale 4 / no widescreen, rewrites the INI, and retries — so a
    saved 4K-fullscreen-on-a-laptop config can't permanently lock the
    user out of the menu.
78. `port_overrides.h`'s `#define fprintf(stream, ...) printf(...)`
    macro was silently rewriting our INI writes into stdout prints
    (zero-byte file on disk). Fixed by `#undef fprintf` at the top of
    every port-native file that needs real-FILE* output (port_config.c,
    photo_export.c, port_tex_dump.c).

## Input Mapping Fixes (May 2026)
79. Switch Pro Controller `DPAD_LEFT → DOWN` bug: SDL's controller
    mapping for the Nintendo Switch Pro Controller routes DPAD HAT
    presses through both `BUTTON_DPAD_LEFT` and `AXIS_LEFTY = +32767`.
    Our stick-deadzone fallback then ORed `BTN_DOWN` (`ly > 16000`),
    and `mts_get_pad` always reports `MTS_PAD_ANALOG` whenever a
    controller is plugged in — `GV_AnalogToDirection` cleared UDLR and
    rebuilt from the polluted `ly` (=DOWN). Fix: skip the analog-stick
    read entirely if any d-pad bit is already set in `b` from
    keyboard arrows or a gamepad DPAD button (`port_update_pad`).
80. d-pad bits overlayed onto `port_pad_lx/ly` after every read. The
    engine's `GV_AnalogToDirection` clears UDLR bits and rebuilds them
    from the analog stick when the pad reports analog; force-writing
    `lx/ly` from the digital bits is what makes keyboard-arrow and
    gamepad-DPAD input survive the rebuild.

## Widescreen Coverage (June 2026)
81. NewBlur / NewBlurPure quad stretches to the full FBO in widescreen
    instead of pillarboxing to the central 80 %. `flush_2d_buf`
    overrides `uXScale = 1.0` for the fb-readback batch (gated on
    `port_blur_enabled`) and the FS skips the PSX clip-rect test for
    fb-readback fragments so widescreen edges aren't culled.
82. Gas-mask sight black peripheries: `gl_renderer_present` clears the
    pillarbox extras to opaque black via `glScissor` + `glClear` while
    `word_800BDCC0 != 0` (set by `source/equip/gmsight.c` while the
    mask is equipped). The mask is vision-restricting by design, so
    blacking the peripheries fits its intent and hides the 3D leak.
83. Cinema letterbox bar extension: `gl_submit_tri2d` detects the
    cinema actor's top/bottom bars (full PSX width, narrow vertical
    range at y≤50 or y≥174, all-grayscale verts — catches both the
    opaque RGB(0,0,0) phase and the subtractive RGB(col,col,col) fade)
    and rewrites the vertex x-positions outward to span
    `[-extra..320+extra]`. The widened verts naturally cover the full
    FBO via the standard `uXScale` projection during cutscenes
    (e.g. d00a).
