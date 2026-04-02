# Changelog

Chronological log of major fixes and milestones in the PSX-to-macOS port.

---

### 1. Initial macOS Port Skeleton

Created `port/` directory with Makefile, SDL2 window, main loop, and stub implementations for all PSX SDK functions. Established `psx/port_overrides.h` force-include to redefine `u_long`, `u_short`, `u_char` to fixed-width types.

### 2. DG_HikituriFlagOld: Function Stub to Variable

`DG_HikituriFlagOld` was declared as a function stub but is actually an `int` variable checked by rendering code. Changed from function to `int DG_HikituriFlagOld = 0`. The function `DG_DrawOTag` that would have set it was never called in the port (replaced by software OT walker).

### 3. mts_PadRead Returned 0

`mts_PadRead` in `port/mts/mts.c` was stubbed to return 0, meaning no button input ever reached the game. Fixed to return the SDL2-polled pad state, enabling keyboard and gamepad input.

### 4. Signed Char Analog Center Value (128 to -128)

Analog stick center value 128 was stored in a `signed char`, which wraps to -128. The game interpreted -128 as a large negative displacement, producing phantom UP+LEFT input every frame. Fixed by using `unsigned char` for analog values and biasing correctly.

### 5. KMD/OAR Loaders: GV_NORMAL_MEMORY to malloc

The KMD (model) and OAR (animation) loaders initially allocated their 64-bit converted data from `GV_NORMAL_MEMORY`. This memory pool is cleared on every stage transition, destroying loaded model data while actors still referenced it. Actors would render garbage or crash. Fixed by allocating converted model data with `malloc` so it persists until explicitly freed.

### 6. All 92 Stage Definitions Compiled

Created or verified all 92 `source/stage/*.c` files so every stage has a `_StageCharacterEntries[]` table. 61 files use proper function pointers; 31 use raw PSX addresses (actors from undecompiled overlay code).

### 7. All 88 Overlay Source Files Compiled

Compiled all overlay source from `source/overlays/` statically into the port binary. Three R-variant overlays (d18ar, s08br, s19br) excluded due to duplicate symbol conflicts.

### 8. FS_ResidentCacheDirty Lifecycle Fix

`FS_ResidentCacheDirty` was not being cleared after the resident cache was saved, causing the filesystem to repeatedly re-save unchanged data. Fixed the flag lifecycle so it is set on modification and cleared after save.

### 9. FS_StreamIsForceStop = 1

Set `FS_StreamIsForceStop = 1` to automatically skip all FMV stream playback attempts. Without this, the game would hang waiting for stream data that the port cannot provide (no CD-ROM streaming).

### 10. tex_scrl.c GCL Parameter Crash

`tex_scrl.c` (texture scroll actor) crashed when parsing GCL parameters because `GCL_GetOption` returned NULL on 64-bit. Added a NULL check with `break` to skip the parameter gracefully.

### 11. wt_view.c Raw Scratchpad to SCRPAD_ADDR

`wt_view.c` (water view effect) used raw PSX scratchpad address `0x1F800000`. Replaced with the `SCRPAD_ADDR` macro that maps to the port's scratchpad emulation buffer.

### 12. 16 link_stubs Variables Wrongly Stubbed as Functions

The link stubs file declared 16 global variables as function stubs (`void foo(void) {}`). These are actually `int` or pointer variables referenced by game code. Mismatched declarations caused crashes when game code wrote to them. Fixed by declaring them as proper variables with correct types.

### 13. thing.c Lvalue Cast Fix

`thing.c` used GCC lvalue casts (`(type)x = value`) which are a non-standard extension not supported by clang. Rewrote as standard C assignments with temporary variables.

### 14. blood.c / splash.c applyVector Macro Expansion

`blood.c` and `splash.c` used an `applyVector` macro that expanded to MIPS-specific register assignments. Replaced with standard C struct member assignments.

### 15. action.c Const Array Fix

`action.c` had a const array initialization that relied on PSX-specific addressing. Converted to use standard C initializers.

### 16. envmap3.c / sub_efct.c / searchli.c / radar.c: MIPS ASM to C

Four source files contained inline MIPS assembly for GTE operations and scratchpad access. Rewrote all assembly blocks as portable C using the GTE emulation functions (`gte_ldv0`, `gte_rtps`, etc.) and `SCRPAD_ADDR` macro.

### 17. All Scratchpad Addresses Replaced Across Codebase

Systematically replaced all raw scratchpad addresses (`0x1F800000` + offset) with `SCRPAD_ADDR(offset)` macro calls across the entire codebase. The scratchpad emulation buffer is a 1024-byte array in `port_memory.c`.

### 18. SCRPAD_ADDR Truncation Fix

`SCRPAD_ADDR` was defined as returning `unsigned long`, which on some configurations was 4 bytes. On 64-bit macOS with clang, this caused pointer truncation. Fixed by casting to `unsigned long long` (always 8 bytes on 64-bit).

### 19. collide.c Compiled from Source

Successfully compiled `source/libhzd/collide.c` (1363 lines) from the original decompiled source. Required replacing 161 scratchpad address references and 49 GTE inline assembly calls. Previously this was a large stub that returned dummy values.

### 20. DG_OBJ.extend Index Comparison Fix

`DG_OBJ.extend` field was compared as an index into an array, but the comparison used signed vs unsigned types inconsistently. This caused valid extend values to be treated as out-of-range, skipping model rendering. Fixed type to match.

### 21. GM_PlayerControl Default Buffer

`GM_PlayerControl` pointer was NULL when no player actor was active, causing crashes in code that unconditionally dereferenced it. Added a static default buffer so `GM_PlayerControl` always points to valid (zeroed) memory.

### 22. Texture Table Overflow Fix

The texture cache table had a fixed size and did not bounds-check insertions. When more textures were loaded than the table could hold, writes corrupted adjacent memory. Added bounds checking and `DG_ResetTextureCache` to clear the table on stage transitions.

### 23. HZD Route Pointer Fix

`HZD_MakeHandler` stored the route distance table pointer by writing it into the first 4 bytes of `HZD_MAP` via `ptr_access[0]`. On 64-bit this truncated the pointer. Replaced with a static cache variable (`cached_route` / `cached_hzd`) in `hzdd.c`.

### 24. GM_GameStatus STATE_PADRELEASE Clear

`GM_GameStatus` had the `STATE_PADRELEASE` bit stuck on, causing the game to ignore all pad input even after initialization. Fixed by clearing the bit when game state transitions to `WORKING`.

### 25. Sound Driver Compiled (10 files) + SPU Emulator

Compiled all 10 sound driver source files from `source/sound/`. Created `port/sound/spu_emu.c` with ADPCM decoder, 24-voice mixer, ADSR envelope emulation, and SDL2 audio callback. Voices play but many higher-level sound commands remain stubbed.

### 26. ImGui Debug Overlay

Integrated Dear ImGui with SDL2+software renderer backend. Press P to toggle an actor inspector that shows all actors across all 9 priority levels with name, tick count, and status. Press Tab for VRAM debug view showing the full 1024x512 16-bit VRAM contents.

### 27. DG_ResetTextureCache Implementation

Implemented `DG_ResetTextureCache` to clear all cached texture page entries on stage transitions. Without this, texture slots from the previous stage lingered and caused incorrect texture lookups.

### 28. Freed-Memory Detection in DG_BoundObjs

Added detection for freed-memory patterns (0xFC fill byte) in `DG_OBJS` entries found in the render queue. When a `DG_OBJS` pointer's fields contain 0xFCFCFCFC, the object has been freed but its queue entry was not removed. The detection logs the dangling entry and skips it instead of crashing during rendering.

### 29. GCL 64-bit Pointer Table

Implemented `gcl_ptr_table[]` (256 entries) in `port/libgcl_fix/` to store 64-bit pointers that GCL encodes as `int` values. Pointer values are stored with `gcl_store_ptr()` and recovered with `gcl_resolve_ptr()`. Index values use the `0x7F000000` marker prefix.

### 30. UNTAG_PTR Macro for collide.c

collide.c used `UNTAG_PTR` to strip PSX segment tags from pointers (PSX pointers have `0x80000000` in the high bit). On 64-bit, this macro was clearing bits that should not be touched. Fixed to only strip the PSX-specific tag bits while preserving the full 64-bit address.

### 31. Software Rasterizer

Implemented a full software rasterizer in `port/libdg/vram.c` that walks the PSX ordering table (OT), decodes GPU primitives (POLY_GT3, POLY_GT4, POLY_G3, POLY_G4, SPRT, TILE, etc.), and renders them into a framebuffer with Z-buffering. Supports textured triangles with PSX-accurate CLUT and texture page sampling from the 1024x512 VRAM array. Renders approximately 2000 faces per frame.
