# Metal Gear Solid — macOS Native Port

A native macOS (ARM64/x86_64) port of the decompiled PSX Metal Gear Solid Integral source code. This port compiles the original decompiled C code with clang, replacing PSX hardware dependencies (GPU, SPU, CD, controller) with SDL2-based equivalents.

## Build

```bash
# Prerequisites
brew install sdl2

# Clone imgui (for debug overlay)
cd port
git clone --depth 1 https://github.com/ocornut/imgui.git imgui

# Build
cd port
make

# Run (needs game disc data — see Data Files section)
./mgs

# Run with auto-input (headless testing, auto-navigates to s00a)
MGS_AUTO_INPUT=1 ./mgs
```

## Data Files

The port reads game data from `port/data/disc1/`. You need the original PSX disc image extracted:
- `MGS/STAGE.DIR` — Stage data (geometry, textures, scripts, overlays)
- `MGS/RADIO.DAT` — Radio codec data
- `MGS/FACE.DAT` — Radio face textures
- `MGS/VOX.DAT` — Voice audio
- `MGS/DEMO.DAT` — Demo playback data
- `MGS/BRF.DAT` — Briefing data
- `MGS/ZMOVIE.STR` — FMV streams (not yet used)

## Current State (2026-04-02)

### What Works
- **Stage loading**: All 92 stages compile, 88 overlay files compile
- **Game flow**: init → select → selectd → opening → select1 → s00a (full chain)
- **3D rendering**: ~2000 faces/frame on s00a, 27 objects queued, textured triangles with Z-buffer
- **Actor system**: Snake (16-bone), camera, doors, enemies, items all create without crashes
- **Collision**: Real HZD collision system compiled from source (collide.c)
- **Sound driver**: All 10 source/sound/*.c files compile with SPU emulator
- **Pad input**: Keyboard + SDL GameController, all buttons reach the game
- **Select menu**: Text rendering, navigation, stage selection
- **ImGui debug**: Press P to toggle actor inspector overlay

### What Doesn't Work Yet
- **Snake doesn't move**: HZD_GetAddress returns 0 (zone lookup fails), blocking movement
- **Snake mesh not visible**: Camera may not point at Snake's position
- **Sound**: SPU emulator plays nothing (PcmOpen/PcmRead return -1, no file I/O for sound data)
- **FMV**: Movie playback stubbed (auto-skipped via FS_StreamIsForceStop=1)
- **Some overlays**: Stages with PSX address function pointers (31 stages) crash on chara creation

### Excluded Source Files (5 remaining)
| File | Reason |
|------|--------|
| `game/movie.c` | FMV decoder (MDEC), no video output |
| `takabe/vib_edit.c` | PCopen conflicting types |
| `game/script.c` | Conflicts with port's `game_script_fix.c` |
| `enemy/merylaction.c` | Duplicate symbols with `animal/meryl72/action.c` |
| `enemy/merylcheck.c` | Depends on merylaction.c |

## Architecture

### Compilation
- **Compiler**: clang (macOS, ARM64)
- **Flags**: `-Wno-everything -O0 -g -DINTEGRAL -DDEV_EXE -DPORT_BUILD`
- **Force-include**: `psx/port_overrides.h` (type fixes, macro overrides)
- **Linker**: clang++ (for ImGui C++ linkage)

### Key Port Files

| File | Purpose |
|------|---------|
| `port/main.c` | SDL2 init, event loop, ImGui integration |
| `port/main_game.c` | Game init (replaces `source/main/main.c`) and per-frame tick |
| `port/psx/port_overrides.h` | Force-included: `u_long=uint32_t`, `fprintf→printf`, type fixes |
| `port/psx/psxdefs.h` | `SCRPAD_ADDR` macro (scratchpad buffer) |
| `port/psx/libgpu.h` | PSX GPU types (POLY_GT4, RECT, etc.) and OT macros |
| `port/psx/libgte.h` | PSX GTE types (MATRIX, SVECTOR, VECTOR) |
| `port/psx/gte_math.c` | C implementations of GTE hardware operations |
| `port/psx/inline_n.h` | GTE inline asm → C macro replacements |
| `port/psx/inline_x.h` | Additional GTE macro replacements (gte_read_opz, etc.) |
| `port/psx/gpu_stubs.c` | PSX GPU/CD/PAD function stubs |
| `port/psx/libspu.h` | Full SPU API declarations |
| `port/sound/spu_emu.c` | Software SPU: ADPCM decode, 24-voice mixer, SDL2 audio |
| `port/libdg/libdg_stub.c` | DG rendering pipeline: loaders, custom renderer, pipeline stubs |
| `port/libdg/vram.c` | PSX VRAM simulation (1024x512 uint16), SDL2 texture display |
| `port/libdg/kmd_loader.c` | 64-bit safe KMD model loader |
| `port/libdg/pcx_loader.c` | PCX texture decoder (4bpp planar, 8bpp RLE) |
| `port/libdg/hzd_loader.c` | 64-bit safe HZD collision data loader |
| `port/libfs/libfs.c` | File system: reads STAGE.DIR, processes DAR archives |
| `port/mts/mts.c` | MTS replacement: single-threaded, SDL2 pad input, gamepad |
| `port/libgcl_fix/parse.c` | GCL parser with 64-bit pointer table |
| `port/libgcl_fix/command.c` | GCL command dispatch with NULL proc guards |
| `port/link_stubs.c` | ~90 remaining stub functions/variables |
| `port/imgui_debug.cpp` | ImGui actor inspector overlay |

### Memory Layout

| Pool | Address | Size | Purpose |
|------|---------|------|---------|
| Normal memory | `port_normal_memory` (malloc) | 2 MB | GV_Malloc heap, per-frame allocations |
| Packet memory 0 | `port_packet_memory0` (malloc) | 512 KB | GPU packet double-buffer 0 |
| Packet memory 1 | `port_packet_memory1` (malloc) | 512 KB | GPU packet double-buffer 1 |
| Scratchpad | `port_scratchpad` (BSS) | 1 KB | Fast scratch memory (replaces PSX 0x1F800000) |
| VRAM | `vram[512][1024]` (BSS) | 1 MB | PSX VRAM simulation (16-bit pixels) |
| SPU RAM | `spu_ram` (BSS) | 512 KB | Sound sample storage |

### Frame Order (game_tick)

```
1. ClearImage(framebuffer)          — clear VRAM framebuffer region
2. memset(scratchpad, 0, 1024)      — clear scratchpad each frame
3. DG_SwapFrame()                   — draw previous frame's OT, clear OTs, update display
4. GV_UpdatePadSystem()             — read pad input from mts_PadRead
5. DG_RenderFrame()                 — run DG pipeline (Screen → Bound → Trans → Shade → Prim → Divide → Sort)
6. port_RenderObjects()             — software 3D renderer (direct VRAM writes)
7. GV_ExecActorSystem()             — run all actors (game logic)
8. GV_Clock flip                    — alternate double-buffer index
```

## 64-bit Porting Issues & Solutions

### Type Size Mismatches

| PSX Type | PSX Size | macOS Size | Solution |
|----------|----------|------------|----------|
| `u_long` | 4 bytes | 8 bytes | `#define u_long uint32_t` in port_overrides.h |
| `long` in MATRIX | 4 bytes | 8 bytes | MATRIX uses `int` (4 bytes) for `t[3]` |
| Pointers in structs | 4 bytes | 8 bytes | Custom loaders for KMD, OAR, IMG, HZD |

### Pointer-in-Int Storage

The PSX code stores pointers in `int` variables throughout. Key instances fixed:

| Variable | PSX Type | Fix |
|----------|----------|-----|
| `GM_PlayerPosition` | `int` (stored SVECTOR*) | Changed to `SVECTOR` |
| `GM_PlayerControl` | `int` | Changed to `void *` with default buffer |
| `GM_PlayerBody` | `int` | Changed to `void *` |
| `DG_HikituriFlagOld` | Function stub | Changed to `int` variable |
| `dword_8009F440/44/48` | Function stubs | Changed to `int` variables |
| `DG_OBJ.extend` | Model index stored as pointer | Cast via `(int)(intptr_t)` before comparison |

### Binary Data Loaders (32→64 bit conversion)

All binary game data has 32-bit pointer fields. Custom loaders parse raw bytes and build 64-bit structs:

| Loader | File | What it converts |
|--------|------|-----------------|
| KMD | `port/libdg/kmd_loader.c` | DG_DEF + DG_MDL[] (model vertices, normals, materials) |
| OAR | `port/libdg/libdg_stub.c` | DG_OAR (animation tables, archive pointers) |
| IMG | `port/libdg/libdg_stub.c` | DG_IMG (sphere image: textures, attribs, tilemap) |
| HZD | `port/libhzd/hzd_loader.c` | HZD_MAP, HZD_GRP (zones, groups, walls, floors) |
| HZD routes | `source/libhzd/hzdd.c` | HZD_PAT.points (patrol route points) |
| GCL | `port/libgcl_fix/parse.c` | Pointer table for 64-bit GCL data pointers |
| RPK | `source/menu/weapon.c` | Resource pack offset→pointer table |
| Font | `source/font/font.c` | Big-endian header offsets |

**Critical**: KMD and OAR data MUST use `malloc()` for persistent allocation, NOT `GV_AllocMemory(GV_NORMAL_MEMORY)` which is per-frame scratch.

### Scratchpad (0x1F800000)

PSX scratchpad (256 bytes fast SRAM at 0x1F800000) is replaced with `port_scratchpad[1024]` in BSS. The macro `SCRPAD_ADDR` resolves to `((unsigned long long)(port_scratchpad))`.

**Critical bug found**: `SCRPAD_ADDR` was originally `(unsigned long)` which is `uint32_t` due to `#define u_long uint32_t`. This truncated the 64-bit pointer to 32 bits. Fixed to `(unsigned long long)`.

All hardcoded `0x1F800xxx` addresses were replaced with `(SCRPAD_ADDR + 0xXXX)` across: `event.c`, `level.c`, `bound.c`, `wt_view.c`, `demo.c`, `divide.c`, `sort.c`, `trans.c`, `expr.c`, `radar.c`, `ending2.c`, `spark2.c`, `libdg2.c`, `collide.c`.

The scratchpad is zeroed every frame in `game_tick()` because collision stubs don't initialize fields that other code reads.

### MIPS Assembly Replacements

| File | Original ASM | C Replacement |
|------|-------------|---------------|
| `envmap3.c` | `mfc2` (COP2 $9/$10/$11) | `gte_state.IR1/IR2/IR3` |
| `sub_efct.c` | `mfc2` (COP2 $9) | `gte_state.IR1` |
| `searchli.c` | GTE interpolation ($12/$13/$14) | `intpol_xz()` C function |
| `radar.c` | `gte_stbh`, `gte_ldv0h` | `gte_state.IR1/IR2`, `gte_state.V0` |
| `collide.c` | MIPS register bindings, GTE ops | Removed register asm, use C GTE wrappers |
| `sd_sub1.c` | `inline` without `static` | Added `static inline` |

### PSX Compiler Extensions Fixed

| Extension | Files | Fix |
|-----------|-------|-----|
| Lvalue cast: `(int)ptr \|= flag` | thing.c, radioanim.c | `*(int *)&ptr \|= flag` |
| Operator as macro arg: `macro(&v, x, y, z, +=)` | blood.c, splash.c | Expand macro inline |
| Const array mutation | action.c | Remove `const` qualifier |
| `setXYWH` on structs without w/h | life.c, radiofacedraw.c | Manual corner coordinate assignment |
| `inline` without `static` (C99) | sd_sub1.c | Add `static` |
| `random()` conflict with stdlib | sd_ext.h | `#define random sd_random` |

### Resident Cache System

The init stage loads resident data (Snake model, animations, common resources) that must persist across stage transitions.

- `FS_ResidentCacheDirty` flag: set to 1 when 'r' archive loads, cleared in `gamed.c` AFTER `GV_SaveResidentFileCache()` uses it
- `DG_SaveResidentTextureCache()`: saves ALL textures with non-zero ID as resident (called when flag is set)
- `DG_ResetTextureCache()`: clears texture table, then reloads resident entries
- **Bug fixed**: Flag was cleared in `FS_LoadStageComplete` BEFORE gamed.c read it — resident data was lost

### OT (Ordering Table) Handle System

PSX OT uses 24-bit linked-list pointers. The port uses a handle table (`port_ot_table[256K]`) mapping 24-bit indices to 64-bit pointers:
- `_ptr_to_handle(p)` → store pointer, return 24-bit index
- `_handle_to_ptr(h)` → look up 64-bit pointer from index
- `ClearOTagR` creates REVERSE chain: `ot[n-1]→ot[n-2]→...→ot[0]→terminator`
- Cycle guard: `port_DrawOTag` aborts after 100K nodes

### DG Render Pipeline

The PSX pipeline (7 stages) runs from compiled source. The port adds a direct software renderer:

```
PSX Pipeline:   DG_ScreenChanl → DG_BoundChanl → DG_TransChanl → DG_ShadeChanl → DG_PrimChanl → DG_DivideChanl → DG_SortChanl
Port Renderer:  port_RenderObjects() — iterates channel 1 queue, transforms vertices, rasterizes to VRAM
```

`port_RenderObjects` uses `objs->world` for map objects and `obj->world` for animated bones (per-bone matrices set by `DG_ScreenChanl`).

### Stage Overlay System

All 92 stages have `_StageCharacterEntries` tables compiled statically. `GM_LoadInitBin` maps stage hash codes to the correct symbol via a switch statement in `gamed.c`.

61 stages have proper function pointers. 31 stages still use raw PSX addresses (0x800xxxxx) — these crash when the chara constructor is called.

### Sound System

The real sound driver (10 files from `source/sound/`) is compiled. A software SPU emulator (`port/sound/spu_emu.c`) provides:
- 512 KB virtual SPU RAM
- 24 voice channels with ADPCM decode
- ADSR envelope (simplified linear)
- SDL2 audio callback at 44100Hz stereo
- All `Spu*` functions implemented

Sound data loading (`PcmOpen/PcmRead/PcmClose`) returns -1 — no audio plays yet.

### Input Mapping

| PSX Button | Keyboard | Gamepad | Hex |
|-----------|----------|---------|-----|
| D-Pad Up | Arrow Up | D-pad Up / Left Stick | 0x1000 |
| D-Pad Down | Arrow Down | D-pad Down / Left Stick | 0x4000 |
| D-Pad Left | Arrow Left | D-pad Left / Left Stick | 0x8000 |
| D-Pad Right | Arrow Right | D-pad Right / Left Stick | 0x2000 |
| Cross (X) | X | A | 0x0040 |
| Circle (O) | Z | B | 0x0020 |
| Triangle | S | Y | 0x0010 |
| Square | A | X | 0x0080 |
| L1 | Q | Left Shoulder | 0x0004 |
| L2 | 1 | Left Trigger | 0x0001 |
| R1 | E | Right Shoulder | 0x0008 |
| R2 | 3 | Right Trigger | 0x0002 |
| Start | Enter | Start | 0x0800 |
| Select | Backspace | Back | 0x0100 |

### Debug Controls

| Key | Action |
|-----|--------|
| P | Toggle ImGui actor inspector |
| Tab | Toggle VRAM debug view |
| Escape | Quit |

### Environment Variables

| Variable | Effect |
|----------|--------|
| `MGS_AUTO_INPUT=1` | Scripted button sequence for headless testing |

## Known Issues

1. **Freed-memory detection in DG_BoundObjs**: Objects from previous stages leave dangling pointers in the render queue. A runtime check detects macOS's 0xfc free-scribble pattern and skips corrupted entries.

2. **GCL proc ID corruption**: Some bind callbacks produce proc IDs like `0x7F0000xx` — a 64-bit pointer truncated to 32 bits in the GCL variable system.

3. **HZD zone lookup fails**: `HZD_GetAddress` returns 0 for Snake's position. The `HZD_ZON` zone data likely needs 64-bit pointer conversion (same pattern as `HZD_PAT` routes).

4. **GM_GameStatus blocking**: The opening stage sets `STATE_PADRELEASE | STATE_ALL_OFF` which blocks pad input. Fixed with PORT_BUILD guard that clears these when entering WORKING state.

5. **shakemdl model 0x6D**: GCL parameter parsing returns wrong hash (0x6D = single char 'm' instead of full model name hash).
