# MGS PSX-to-macOS Port: Overview

## What Is This?

This is a native macOS port of **Metal Gear Solid Integral** (PlayStation, 1999), built on top of a fully decompiled C codebase. The original PSX executables (SLPM_862.47/48/49) were reverse-engineered to produce C source that compiles to binary-identical assembly under the original PSYQ SDK toolchain. This port takes that decompiled C and replaces PSX hardware dependencies with modern equivalents so the game runs natively on macOS.

The port compiles the original game source files (`source/`) alongside a `port/` layer that provides PSX hardware stubs, a software renderer, SDL2 integration, and a GTE math emulator. Stage overlays, which were dynamically loaded on PSX, are compiled statically into the single binary.

## Current State

### What Works

- **Stage loading**: STAGE.DIR is parsed, stage data (GCL scripts, KMD models, textures, collision) loads correctly
- **Software 3D rendering**: ~2000 faces rendered per frame via software rasterizer with Z-buffer, textured triangles, PSX-accurate CLUT/tpage sampling
- **Collision system**: HZD collision data loads from source, collision checks run (libhzd compiled from original source)
- **Actor system**: All 9 priority levels execute, actors spawn/tick/die correctly (enemies, cameras, map objects)
- **GCL scripting**: Bytecode interpreter runs stage scripts (chara/map/camera/pad/start commands)
- **Pad input**: Keyboard and SDL GameController mapped to PSX button layout, analog stick support
- **VRAM simulation**: Full 1024x512 16-bit VRAM array, LoadImage/StoreImage/ClearImage, texture page sampling
- **GTE emulation**: All COP2 operations implemented in C (RTPS, RTPT, NCLIP, NCS, NCT, NCDS, AVSZ3/4, etc.)
- **ImGui debug overlay**: Press P to see all actors by priority level with name/status/tick count
- **SPU emulation**: ADPCM decoder, 24-voice mixer, ADSR envelopes, SDL2 audio callback (partially working)

### What Does Not Work

- **Snake movement**: Character spawns but movement/animation is incomplete
- **Sound playback**: SPU emulator decodes audio but many sound driver calls are stubbed
- **FMV playback**: Movie/STR streaming is fully stubbed (FS_StreamIsForceStop returns 1)
- **Memory cards**: Stubbed to no-op (no save/load)
- **Some overlays**: R-variant overlays (d18ar, s08br, s19br) excluded due to duplicate symbols

## Build Instructions

### Prerequisites

```bash
# Install SDL2
brew install sdl2

# Clone Dear ImGui into port/imgui/
cd port
git clone https://github.com/ocornut/imgui.git imgui
cd ..
```

### Required Data Files

You need the game data files from a Metal Gear Solid Integral disc (Disc 1). Extract the `MGS/` directory from the disc and place it at `port/data/disc1/MGS/`. The required files are:

| File          | Description                              |
|---------------|------------------------------------------|
| `STAGE.DIR`   | Stage archive (models, textures, scripts, collision) |
| `RADIO.DAT`   | Radio codec dialogue data                |
| `FACE.DAT`    | Codec face animation data                |
| `ZMOVIE.STR`  | FMV movie streams (not yet used)         |
| `VOX.DAT`     | Voice audio data                         |
| `DEMO.DAT`    | Demo replay data                         |
| `BRF.DAT`     | Briefing data                            |

The filesystem code (`port/libfs/libfs.c`) looks for these at `data/disc1/MGS/` relative to the port directory. Missing files are logged but do not prevent startup; only `STAGE.DIR` is required for stage loading.

### Building

```bash
cd port
make
```

This compiles everything with clang (`-Wno-everything -O0 -g`), links against SDL2 and libc++, and produces the `mgs` binary. The build force-includes `psx/port_overrides.h` before every source file to redefine PSX types.

### Running

```bash
cd port
./mgs
```

### Controls

| Action        | Keyboard    | Gamepad              |
|---------------|-------------|----------------------|
| D-Pad Up      | Arrow Up    | D-Pad Up / L-Stick   |
| D-Pad Down    | Arrow Down  | D-Pad Down / L-Stick |
| D-Pad Left    | Arrow Left  | D-Pad Left / L-Stick |
| D-Pad Right   | Arrow Right | D-Pad Right / L-Stick|
| Cross (X)     | X           | A (south)            |
| Circle (O)    | Z           | B (east)             |
| Triangle      | S           | Y (north)            |
| Square        | A           | X (west)             |
| L1            | Q           | Left Shoulder        |
| L2            | 1           | Left Trigger         |
| R1            | E           | Right Shoulder       |
| R2            | 3           | Right Trigger        |
| Start         | Enter       | Start                |
| Select        | Backspace   | Back                 |

Debug keys:
- **Tab**: Toggle VRAM debug view (shows entire 1024x512 VRAM)
- **P**: Toggle ImGui actor inspector
- **Escape**: Quit
- **W/A/S/D/Q/E + Arrow keys**: Free-fly camera (development)

### Environment Variables

- `MGS_VRAM_DEBUG=1` -- Start with VRAM debug view enabled
- `MGS_AUTO_INPUT=1` -- Replay a scripted button sequence for headless testing (navigates menus to load s00a)

## Directory Layout

```
port/
  main.c              -- SDL2 window, event loop, crash handler
  main_game.c         -- game_init() / game_tick(), free-fly camera
  port_memory.c       -- malloc'd memory pools replacing PSX RAM addresses
  Makefile             -- Build system
  psx/                 -- PSX SDK replacement headers + implementations
    port_overrides.h   -- Force-included, redefines u_long/u_short/u_char to fixed-width
    gte_math.c         -- Software GTE (all COP2 operations)
    gpu_stubs.c        -- GPU function stubs (DrawSync, PutDrawEnv, etc.)
    libgte.h           -- SVECTOR/VECTOR/MATRIX/CVECTOR/RECT types
    libgpu.h           -- Primitive structs (POLY_GT4, SPRT, TILE), OT macros
    inline_n.h         -- GTE macro expansions (gte_ldv0, gte_rtps, etc.)
  libdg/               -- Rendering subsystem
    vram.c             -- 1024x512 VRAM array, software rasterizer, OT walker
    kmd_loader.c       -- 64-bit safe KMD model loader
    pcx_loader.c       -- PCX texture loader (uploads to VRAM)
    libdg_stub.c       -- DG_StartDaemon, DG_ResetPipeline, port_RenderObjects
  libfs/               -- Filesystem (stdio replacement for CD-ROM)
    libfs.c            -- Reads STAGE.DIR, parses DATACNF, loads DAR archives
  libgv/               -- GV library overrides
    libgv.h            -- Memory address remapping
  libgcl_fix/          -- 64-bit safe GCL interpreter
  libhzd/              -- HZD collision data loader
  mts/                 -- MTS kernel replacement (single-threaded)
    mts.c              -- Task stubs, pad input, controller support
  sound/               -- SPU emulator
    spu_emu.c          -- ADPCM decoder, 24-voice mixer, SDL2 audio
  memcard/             -- Memory card stubs
  imgui/               -- Dear ImGui (submodule/clone)
  imgui_debug.cpp      -- Actor inspector overlay
  data/disc1/MGS/      -- Game data files (not in repo)
```
