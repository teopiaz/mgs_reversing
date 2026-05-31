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

## Current State (June 2026)

### Working
- **Pre-game menu**: Splash → main menu → Options / Controls pages (ImGui).
  Settings persisted to `./port_config.ini`: resolution, fullscreen, vsync,
  OpenGL backend toggle, FBO scale (1–8), widescreen, master volume,
  language (Japanese/English), per-button keyboard + gamepad bindings. GL
  init has a safe-defaults fallback so a saved config that won't initialize
  can't lock the user out of the menu.
- **Stage loading**: 95 stages in STAGE.DIR parsed, data loads (GCL, KMD, textures, HZD)
- **3D rendering**: Two backends — original software rasterizer + hi-res
  OpenGL renderer (`PORT_GL=1`, also the "OpenGL backend" checkbox in
  Options). GL path has perspective-correct texturing, hot-reloadable GLSL
  shaders (F5), and the FBO upscale (1×–8×). Software path stays the
  fallback for systems without GL.
- **2D rendering**: Full OT primitive support — TILE, SPRT, POLY_F3/F4, POLY_G3/G4 (Gouraud), POLY_FT3/FT4 (textured), POLY_GT3/GT4 (textured Gouraud), LINE_F2/F4/G2/G4
- **Semi-transparency**: 4 PSX blend modes (B+F, (B+F)/2, B-F, B+F/4) for both textured and non-textured primitives (radar vision cones, glass, fades)
- **Framebuffer-readback effects**: NewBlur / NewBlurPure (2D) and kogaku2
  Optical Camo / Stealth (3D) implemented via tpage-into-display-region
  detection + mid-frame FBO capture (3D) / end-of-frame capture (2D). ImGui
  Effects panel has the strength slider + on/off
- **Widescreen (16:9 Hor+)**: Pre-game Options toggle (also ImGui Renderer →
  Quality/Output). Internal render width grows from 320 to 400; the 3D
  scene expands FOV horizontally, the 2D HUD pillarboxes in the central
  80%. The motion-blur quad, gas-mask sight, and cinema letterbox bars all
  cover the widescreen extras instead of leaking the 3D scene through
- **Multi-channel rendering**: channels 0 (background), 1 (main), 2 (overlay) all iterated
- **Radar**: walls rendered via handle-based OT linking; vision cones semi-transparent
- **Collision system**: HZD wall/floor/zone checks, level height testing, dynamic floors
- **Actor system**: All 9 priority levels, 88 stage overlays compiled statically
- **GCL scripting**: Bytecode interpreter, command dispatch, variable/expression system
- **Input**: Keyboard + SDL GameController with full per-button remapping
  via the Controls page (saved to INI as raw SDL scancode /
  `SDL_GameControllerButton` ints). Switch Pro Controller handled — d-pad
  presses bypass the analog-stick read so SDL's HAT-to-axes leak doesn't
  turn LEFT into DOWN. Input record/replay still available for headless
  testing.
- **VRAM**: 1024×512 16-bit array, LoadImage/StoreImage/MoveImage/ClearImage
- **GTE**: All COP2 ops in C (RTPS/RTPT/NCLIP/NCS/NCT/NCDS/AVSZ3/4/etc.); sf=1 shift properly applied in stlvnl
- **Sound**: SPU emulator (ADPCM, 24 voices, ADSR, Gaussian interpolation, SDL2 audio)
- **Codec/Radio**: SELECT opens codec, UI renders via OT, VOX streaming
  starts; face portraits render correctly; `str_tick_count` slaved to the
  audio cursor so lipsync + subtitle timing track the voiced line
- **Cutscenes**: Camera/animation, 29 character types, subtitles via jimctrl
- **Demo scrubber**: ImGui Demo tab — per-frame scrubber, dump-snake-render-
  state button, direct-from-disk demo feeder for investigating individual
  cutscene frames (`PORT_DEMO_PAUSE_AT`, `PORT_DEBUG_SNAKE`, etc.)
- **Palette effects**: Goggle tint (thermal/NV) via LoadImage2/StoreImage2 callbacks
- **FPV**: First-person view hides Snake model via DG_FLAG_INVISIBLE
- **3D Gouraud lighting**: NCS colour pipeline wired up end-to-end; actors and
  SHADE-flag level geometry now render with per-vertex Gouraud lighting matching
  PSX (see `13-shading-and-lighting.md` §5.6–§5.8)
- **Weapon spawn positions**: Nikita missile (and other GTE-computed spawn points) correct after gte_stlvnl fix
- **Language**: Japanese (PSX default) or English, toggled in Options and
  applied via the `OPTION_ENGLISH` (0x0100) bit of `GM_OptionFlag`
  (`linkvarbuf[2]`) right before `game_init()`. The in-game option screen
  and save-file load both still override at runtime, as on PSX.
- **ImGui overlay**: Press F1 for debug panel (Renderer, Camera, Actors, Stage, Game, Demo, etc.)

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
| Variable             | Description                                          |
|----------------------|------------------------------------------------------|
| `PORT_GL`            | `1` = OpenGL backend, `0` = software (also a checkbox in Options) |
| `PORT_GL_SCALE`      | FBO upscale 1..8 (default 4); also Options slider    |
| `PORT_SKIP_MENU`     | `1` to bypass the pre-game menu (CI / automation)    |
| `MGS_INPUT_RECORD`   | Record input to file (e.g., `=test.log`)             |
| `MGS_INPUT_REPLAY`   | Replay input from file (headless testing)            |
| `MGS_AUTO_INPUT`     | Scripted button sequence (auto-skips menu)           |
| `MGS_VRAM_DEBUG`     | Show full 1024×512 VRAM instead of game display      |
| `PORT_PAD_LOG`       | `1` prints every keyboard D-pad edge with full state |
| `PORT_AUTOLOAD_STAGE`| Skip select menu and load this stage (e.g., `s00a`)  |
| `PORT_DATA_PATH`     | Override data file location (default: `data/disc1/MGS/`) |
| `PORT_DEMO_PAUSE_AT` | Freeze streaming cinema at target frame              |
| `PORT_DEBUG_SNAKE`   | Dump snake pos + DMO_ADJ every frame                 |
| `PORT_DEBUG_CAM`     | Dump DG_Chanls[1] eye_inv ~once per second           |

### Controls (defaults)
Re-mapped in the pre-game Controls page — values below are the factory
defaults installed by `port_config_set_defaults` and written to
`./port_config.ini` on first run.

| Keyboard     | PSX Button  | Action            |
|-------------|-------------|-------------------|
| X           | Cross (×)   | Action / Crouch   |
| A           | Square (□)  | Weapon            |
| Z           | Circle (○)  | Punch / Confirm   |
| S           | Triangle (△)| First-person view |
| Q           | L1          |                   |
| E           | R1          | Weapon ready      |
| 1 / 3       | L2 / R2     |                   |
| Enter       | Start       | Pause / Skip      |
| Backspace   | Select      | Codec             |
| Arrow keys  | D-Pad       | Movement          |
| Escape      | —           | Quit              |

Gamepad defaults follow the standard SDL_GameController layout (A=Cross,
B=Circle, X=Square, Y=Triangle, L/R shoulders = L1/R1, triggers = L2/R2,
DPAD = movement). Both can be rebound from the Controls page.

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
| [04 — Rendering](04-rendering.md) | PSX GPU pipeline, port software + OpenGL renderers, widescreen |
| [05 — Sound](05-sound.md) | PSX SPU hardware vs port SPU emulator |
| [06 — Filesystem](06-filesystem.md) | CD-ROM layout, STAGE.DIR, DATACNF format |
| [07 — Actors & GCL](07-actors-gcl.md) | Actor system, GCL scripting, stage overlays |
| [08 — Collision](08-collision.md) | HZD system, wall/floor checks, zones |
| [09 — Input](09-input.md) | Controller mapping, config-driven remap, record/replay |
| [10 — Changelog](10-changelog.md) | Chronological fix log |
| [11 — Known Issues](11-known-issues.md) | Open bugs and limitations |
| [12 — TODO](12-todo.md) | Outstanding work items |
| [13 — Shading & Lighting](13-shading-and-lighting.md) | NCS Gouraud pipeline, per-vertex lighting |
| [demo/](demo/README.md) | Cutscene streaming, DMO format, frame scrubber |
| [editor/](editor/) | Stage editor (collision / GCL authoring) |
| [source/](source/) | Notes on the original decompiled source tree |
| [gcl/](gcl/) | GCL opcode reference |
