# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a reverse engineering project to decompile **Metal Gear Solid Integral** (PlayStation) back to C source code that produces binary-identical assembly when compiled with the original PSYQ SDK toolchain. The main executables (SLPM_862.47/48/49) are 100% decompiled; overlay decompilation is ongoing.

## Current Goal: macOS Native Port

The active goal is to port this decompiled PSX C code to compile and run natively on macOS. This means:

- Replacing PSX hardware dependencies (GPU, SPU, CD, controller, memory card) with modern equivalents
- Replacing PSYQ SDK types and functions with portable alternatives
- Replacing MIPS inline assembly with C or platform-appropriate code
- Adding a rendering backend (e.g., Metal, OpenGL, SDL2) to replace PSX GPU primitives
- Adding audio output to replace PSX SPU
- Adding input handling to replace PSX controller polling
- Building with a modern C compiler (clang) targeting macOS/ARM64 or x86_64

Key PSX-specific subsystems that need replacement:
- `libdg/` — PSX GPU rendering (ordering tables, draw primitives)
- `libfs/` — PSX CD-ROM filesystem
- `libgv/` — Core engine (memory management uses PSX-specific layouts)
- `libhzd/` — Collision (mostly portable math, but may use fixed-point PSX conventions)
- `libsio/` — PSX serial I/O (can likely be stubbed/removed)
- `source/sound/`, `source/mts/` — PSX SPU audio
- `source/memcard/` — PSX memory card (replace with file I/O)
- MIPS assembly in `asm/` — Must be rewritten in C or dropped

## Build Commands

All build commands run from `build/`:

```bash
# use a virtual environment
python3 -m venv venv
source venv/bin/activate  # Linux/macOS

cd build

# Install dependencies (first time)
pip3 install -r requirements.txt

# Build main executable (SLPM_862.47/48)
python3 build.py

# Build VR disc executable (SLPM_862.49)
python3 build.py --variant=vr_exe

# Build non-matching dev variant (for testing in emulator)
python3 build.py --variant=dev_exe

# Run in PCSX-Redux emulator (dev variant)
python3 run.py --iso <ISO_PATH> --pcsx-redux <EMULATOR_PATH>
```

The build generates `build.ninja` then runs Ninja. Requires the PSYQ SDK cloned as a sibling directory (`../../psyq_sdk`) or specify `--psyq_path`. On macOS, Wine is required (`brew install --cask --no-quarantine wine-stable`).

## Verification

There are no unit tests. Correctness is verified by SHA256 hash comparison of compiled binaries against the original game binaries. The build automatically runs `compare.py` which checks hashes. A successful build produces a hash match message. CI runs on AppVeyor (Windows).

## Architecture

### Compilation Toolchain

- **PSYQ 4.3/4.4**: PlayStation cross-compiler (cc1psx.exe), assembler (aspsx.exe), linker (psylink.exe)
- Runs via Wine/Wibo on non-Windows platforms
- Compiler flags: `-O2 -G 8 -g0 -Wall`
- Defines: `INTEGRAL` (always), `VR_EXE` (vr variant), `DEV_EXE` (dev variant)

### Source Layout

- `source/` — Decompiled C code
  - `source/include/` — Header files
  - `source/lib*/` — Engine libraries (libdg=graphics, libfs=filesystem, libgcl=scripting/GCL, libgv=core engine, libhzd=collision, libsio=serial I/O)
  - `source/game/` — Core game systems
  - `source/chara/` — Character actors (snake, snake_vr)
  - `source/enemy/` — Enemy AI actors
  - `source/stage/` — Per-stage overlay source (s00a, s01a, etc.)
  - `source/stagevr/` — VR stage variants
  - `source/overlays/` — Overlay loading system
  - `source/sound/`, `source/mts/` — Audio systems
  - `source/weapon/`, `source/bullet/`, `source/equip/` — Weapons/items
  - `source/anime/` — Animation system
  - `source/thing/` — Game object/entity system
- `asm/` — Assembly files for functions not yet decompiled (MIPS PSX)
- `asm/overlays/` — Overlay functions awaiting decompilation
- `build/` — Build scripts and utilities
  - `build/functions.txt` — Catalog of all functions
  - `build/linker_command_file.txt` — Linker script
  - `build/decompme_asm.py` — Exports ASM to clipboard for decomp.me scratches
  - `build/compare.py` — Binary hash verification

### Overlay System

The game dynamically loads per-stage overlay code at runtime. Stage codes (s00a, s01a, d00a, etc.) map to game locations. "s" prefix = playable stages, "d" prefix = cutscene stages, "r" suffix = RED variants. Each overlay directory may contain its own actors and logic specific to that stage.

### Decompilation Workflow

1. Find an undecompiled function in `asm/overlays/` (small .s files are good starting points)
2. Check if the function was already decompiled in another overlay (many overlays share code)
3. Run `python decompme_asm.py [path_to.s]` to copy ASM to clipboard for [decomp.me](https://decomp.me/) (use the "Metal Gear Solid (overlays)" preset)
4. Iteratively match the C implementation to produce identical assembly
5. Replace the .s file with a .c implementation

## Code Style

- `.clang-format`: Microsoft style, `AlignConsecutiveDeclarations: true`, `SortIncludes: false`
- C files: 4-space indentation (spaces). ASM files: 8-space tab indentation.
- Format all C code: `./clang-format-all`
- The CI enforces a warning whitelist — new warnings not in the whitelist will fail the build.
