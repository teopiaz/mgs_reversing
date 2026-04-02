# Metal Gear Solid — macOS Native Port

A native macOS (ARM64/x86_64) port of the decompiled PSX Metal Gear Solid Integral source code, using SDL2.

## Quick Start

```bash
brew install sdl2
cd port
git clone --depth 1 https://github.com/ocornut/imgui.git imgui
make
./mgs
```

Requires game data in `port/data/disc1/MGS/` (STAGE.DIR, RADIO.DAT, etc. from original PSX disc).

## Documentation

Detailed documentation is in [`doc/`](doc/):

| Document | Contents |
|----------|----------|
| [00-overview.md](doc/00-overview.md) | Project overview, build instructions, current state |
| [01-psx-architecture.md](doc/01-psx-architecture.md) | PSX hardware: CPU, GPU, GTE, SPU, CD, scratchpad, MTS |
| [02-port-architecture.md](doc/02-port-architecture.md) | Port design: SDL2, memory pools, VRAM sim, GTE wrappers |
| [03-64bit-porting.md](doc/03-64bit-porting.md) | All 64-bit issues: types, pointers, loaders, SCRPAD_ADDR |
| [04-rendering.md](doc/04-rendering.md) | OT system, channel pipeline, software 3D renderer |
| [05-sound.md](doc/05-sound.md) | SPU hardware, sound driver, ADPCM decoder, SDL2 audio |
| [06-filesystem.md](doc/06-filesystem.md) | STAGE.DIR format, cache system, resident data lifecycle |
| [07-actors-gcl.md](doc/07-actors-gcl.md) | Actor system, GCL scripting, stage overlays |
| [08-collision.md](doc/08-collision.md) | HZD collision: zones, walls, floors, collide.c porting |
| [09-input.md](doc/09-input.md) | Pad system, keyboard/gamepad mapping, analog handling |
| [10-changelog.md](doc/10-changelog.md) | Chronological log of all fixes |
| [11-known-issues.md](doc/11-known-issues.md) | Current bugs with root cause analysis |
| [12-todo.md](doc/12-todo.md) | Prioritized TODO list |

## Controls

| Action | Keyboard | Gamepad |
|--------|----------|---------|
| Move | Arrow keys | Left stick / D-pad |
| Cross (confirm) | X | A |
| Circle | Z | B |
| Triangle | S | Y |
| Square | A | X |
| L1/R1 | Q / E | Shoulders |
| L2/R2 | 1 / 3 | Triggers |
| Start | Enter | Start |
| Select | Backspace | Back |
| Debug overlay | P | — |
| VRAM view | Tab | — |
| Quit | Escape | — |
