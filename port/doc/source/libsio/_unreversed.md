---
file: source/libsio/ — opaque areas
---

# `libsio/` — opaque areas

PSX serial I/O. Mostly stubbed in release / port.

## Original purpose

Used during development for connecting a debug terminal over the
PSX serial port. Functions like `sio_putchar` / `sio_getchar` /
debug-terminal control codes.

In the released disc, all of this is compiled out (DEV_EXE only)
or stubbed to NOPs. The port preserves the stubs.

## What's still partly opaque

- The exact terminal protocol (some control sequences look like
  VT100 emulation but aren't fully verified).
- The `mts_sio_*` interface in `mts/`.
- The `terminal.h` API names — present but unused.

## See also

- [README.md](README.md) — file map.
- [`port/libsio/`](../../../../port/libsio/) — stub
  replacement.
