# MGS PSX-to-macOS Port: Overview

## What Is This?

This is a native macOS port of **Metal Gear Solid Integral** (PlayStation, 1999), built on
top of a fully decompiled C codebase. The original PSX executables (SLPM_862.47/48/49) were
reverse-engineered to produce C source that compiles to binary-identical assembly under the
original PSYQ SDK toolchain. This port takes that decompiled C and replaces PSX hardware
dependencies with modern equivalents so the game runs natively on macOS ARM64 / x86_64.

The port compiles the original game source files (`source/`) alongside a `port/` layer that
provides PSX hardware stubs, a software renderer, SDL2 integration, SPU audio emulation, and
a GTE math emulator. Stage overlays, which were dynamically loaded on PSX, are compiled
statically into the single binary.

## Architecture at a Glance

```
┌──────────────────────────────────────────────────────────────────┐
│  Original PSX Source (source/)                                   │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐        │
│  │libdg │ │libgv │ │libhzd│ │libgcl│ │libfs │ │sound │        │
│  │render│ │core  │ │colli-│ │scrip-│ │file- │ │driver│        │
│  │pipe- │ │engine│ │sion  │ │ting  │ │system│ │      │        │
│  │line  │ │memory│ │      │ │      │ │      │ │      │        │
│  └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘        │
│     │        │        │        │        │        │              │
│  ┌──┴────────┴────────┴────────┴────────┴────────┴──┐           │
│  │            Port Abstraction Layer (port/)          │           │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────┐ │           │
│  │  │Software │ │GTE Math │ │SPU Emu  │ │Filesys │ │           │
│  │  │Renderer │ │Emulator │ │ADPCM+   │ │stdio   │ │           │
│  │  │libdg_   │ │gte_math │ │ADSR+    │ │replace │ │           │
│  │  │stub.c + │ │.c +     │ │Gauss    │ │ment    │ │           │
│  │  │vram.c   │ │inline_n │ │interp.  │ │        │ │           │
│  │  └────┬────┘ └─────────┘ └────┬────┘ └────────┘ │           │
│  │       │                       │                   │           │
│  │  ┌────┴───────────────────────┴──┐                │           │
│  │  │          SDL2 Backend          │                │           │
│  │  │  Window ─ Renderer ─ Audio    │                │           │
│  │  └───────────────────────────────┘                │           │
│  └───────────────────────────────────────────────────┘           │
└──────────────────────────────────────────────────────────────────┘
```

## Current State (April 2026)

### Working
- **Stage loading**: 95 stages in STAGE.DIR parsed, data loads (GCL, KMD, textures, HZD)
- **3D rendering**: Software rasterizer — affine textured quads, Z-buffer, backface culling, per-vertex Gouraud shading
- **2D rendering**: Full OT primitive support — TILE, SPRT, POLY_F3/F4, POLY_G3/G4 (Gouraud), POLY_FT3/FT4 (textured), POLY_GT3/GT4 (textured Gouraud), LINE_F2/F4/G2/G4
- **Semi-transparency**: 4 PSX blend modes (B+F, (B+F)/2, B-F, B+F/4) for both textured and non-textured primitives (radar vision cones, glass, fades)
- **Multi-channel rendering**: channels 0 (background), 1 (main), 2 (overlay) all iterated
- **Radar**: walls rendered via handle-based OT linking; vision cones semi-transparent
- **Collision system**: HZD wall/floor/zone checks, level height testing, dynamic floors
- **Actor system**: All 9 priority levels, 88 stage overlays compiled statically
- **GCL scripting**: Bytecode interpreter, command dispatch, variable/expression system
- **Input**: Keyboard + SDL GameController, analog sticks, input record/replay
- **VRAM**: 1024×512 16-bit array, LoadImage/StoreImage/MoveImage/ClearImage
- **GTE**: All COP2 ops in C (RTPS/RTPT/NCLIP/NCS/NCT/NCDS/AVSZ3/4/etc.); sf=1 shift properly applied in stlvnl
- **Sound**: SPU emulator (ADPCM, 24 voices, ADSR, Gaussian interpolation, SDL2 audio)
- **Codec/Radio**: SELECT opens codec, UI renders via OT, VOX streaming starts; face portraits render correctly
- **Cutscenes**: Camera/animation, 29 character types, subtitles via jimctrl
- **Palette effects**: Goggle tint (thermal/NV) via LoadImage2/StoreImage2 callbacks
- **FPV**: First-person view hides Snake model via DG_FLAG_INVISIBLE
- **3D Gouraud lighting**: NCS colour pipeline wired up end-to-end; actors and
  SHADE-flag level geometry now render with per-vertex Gouraud lighting matching
  PSX (see `13-shading-and-lighting.md` §5.6–§5.8)
- **Weapon spawn positions**: Nikita missile (and other GTE-computed spawn points) correct after gte_stlvnl fix
- **ImGui overlay**: Press F1 for debug panel (renderer, camera, actors, stage, game, other tabs)
- **Widescreen**: Optional 16:9 Hor+ toggle in ImGui Renderer → Quality/Output

### Known Limitations
- **VOX audio**: Stream transfers to SPU voices but playback needs work
- **Subtitle timing**: MENU_JimakuWrite receives frames=0 (duration incorrect)
- **s02c floor**: One hangar room missing floor + walls (scratchpad mmap failure → `DG_ScreenChanl` produces wrong matrices for ONEPIECE maps; see known-issues #11)
- **Frustum culling**: GTE-based BOUND/GBOUND tests produce wrong coords on 64-bit and are bypassed (every visible group passes with `bound_mode=2`); causes no visual bugs but wastes CPU on off-screen geometry
- **s01a-style `map -c` stage walls**: render brighter than the PSX reference for unknown reasons (see known-issues #12) — actors and preshaded props are correct
- **FMV**: Fully stubbed (no MDEC decoder)
- **Memory cards**: Stubbed (no save/load)
- **3 R-variant overlays**: Excluded (d18ar, s08br, s19br) — duplicate symbols

## Build Instructions

### Prerequisites
```bash
brew install sdl2
cd port && git clone https://github.com/ocornut/imgui.git imgui
```

### Required Data Files
Extract `MGS/` from an MGS Integral Disc 1 to `port/data/disc1/MGS/`:

| File          | Description                              |
|---------------|------------------------------------------|
| `STAGE.DIR`   | All stage data (models, scripts, sounds) |
| `RADIO.DAT`   | Codec face textures                      |
| `FACE.DAT`    | Character face models                    |
| `ZMOVIE.STR`  | FMV cutscenes (not yet used)             |
| `VOX.DAT`     | Voice audio for cutscenes                |
| `DEMO.DAT`    | Demo/cutscene data streams               |
| `BRF.DAT`     | Briefing data                            |

### Build & Run
```bash
cd port
make -j
./mgs
```

### Environment Variables
| Variable            | Description                                          |
|---------------------|------------------------------------------------------|
| `MGS_INPUT_RECORD`  | Record input to file (e.g., `=test.log`)             |
| `MGS_INPUT_REPLAY`  | Replay input from file (headless testing)            |
| `MGS_VRAM_DEBUG`    | Show full 1024×512 VRAM instead of game display      |
| `PORT_DATA_PATH`    | Override data file location (default: `data/disc1/MGS/`) |

### Controls
| Keyboard     | PSX Button  | Action            |
|-------------|-------------|-------------------|
| Z           | Cross (×)   | Action / Crouch   |
| X           | Square (□)  | Weapon            |
| C           | Circle (○)  | Punch / Confirm   |
| V           | Triangle (△)| First-person view |
| Space       | R1          | Weapon ready      |
| Left Shift  | L1          |                   |
| Enter       | Start       | Pause / Skip      |
| Tab         | Select      | Codec             |
| Arrow keys  | D-Pad       | Movement          |
| Escape      | —           | Quit              |

## Directory Layout

```
port/
├── main.c              # SDL2 init, window creation
├── main_game.c         # Game tick loop, init sequence
├── Makefile            # Build system
├── psx/                # PSX hardware replacements
│   ├── port_overrides.h  # Force-included type fixes
│   ├── gte_math.c        # GTE emulation (all COP2 ops)
│   ├── inline_n.h        # GTE register load/store macros
│   ├── gpu_stubs.c       # GPU/OT stubs
│   ├── libgpu.h          # GPU types + function stubs
│   ├── libgte.h          # GTE types
│   └── psxdefs.h         # Scratchpad replacement
├── libdg/              # Rendering
│   ├── libdg_stub.c      # Software 3D rasterizer
│   ├── vram.c            # VRAM sim + OT walker + triangle rasterizer
│   ├── libdg.h           # Patched libdg header (enum fix, inline helpers)
│   ├── kmd_loader.c      # KMD model binary loader (32→64 bit)
│   └── pcx_loader.c      # PCX texture uploader
├── libfs/              # Filesystem
│   └── libfs.c           # stdio replacement for CD-ROM
├── libhzd/             # Collision loader
│   └── hzd_loader.c     # HZD binary loader (32→64 bit pointer fixup)
├── sound/              # Audio
│   └── spu_emu.c        # SPU emulator (ADPCM, ADSR, mixing)
├── mts/                # Multitasking
│   └── mts.c            # Single-threaded stubs for PSX MTS kernel
├── include/            # Patched headers
│   ├── common.h          # SCRPAD_ADDR macro, getScratchAddr
│   └── psxdefs.h         # Scratchpad buffer extern
├── extern_stubs.c      # Character entry table (29 types)
├── game_stubs.c        # Stubbed game functions
├── asm_stubs.c         # MIPS assembly replacements
├── link_stubs.c        # Linker symbol stubs
├── port_memory.c       # Memory pool allocation
├── imgui_debug.cpp     # ImGui actor inspector
└── doc/                # This documentation
```

## Document Index

| Document | Contents |
|----------|----------|
| [01 — PSX Architecture](01-psx-architecture.md) | PSX hardware reference (CPU, GPU, GTE, SPU, memory) |
| [02 — Port Architecture](02-port-architecture.md) | Design decisions, SDL2 integration, frame loop |
| [03 — 64-bit Porting](03-64bit-porting.md) | Every pointer/type issue and its fix |
| [04 — Rendering](04-rendering.md) | PSX GPU pipeline vs port software renderer |
| [05 — Sound](05-sound.md) | PSX SPU hardware vs port SPU emulator |
| [06 — Filesystem](06-filesystem.md) | CD-ROM layout, STAGE.DIR, DATACNF format |
| [07 — Actors & GCL](07-actors-gcl.md) | Actor system, GCL scripting, stage overlays |
| [08 — Collision](08-collision.md) | HZD system, wall/floor checks, zones |
| [09 — Input](09-input.md) | Controller mapping, record/replay |
