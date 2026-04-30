# `source/libsio/` — serial I/O

PSX serial-port library. Two files:

| File | Role |
| ---- | ---- |
| [`dummy.c`](../../../../source/libsio/dummy.c) | Stubs — no-op implementations. The release build doesn't actually use the serial port. |
| [`isio.h`](../../../../source/libsio/isio.h) | Public API declarations. |

## Why it exists

PSX dev kits (PSY-Q dev boards) had a serial debug port. The
team used it during development for trace logging and remote
debugging. In the release disc binary, every API entry is a stub
that returns 0 / does nothing — they just kept the structure to
preserve the matching build.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (terminal protocol,
  mts_sio_* interface).
- The disc binary's debug paths in `menu/debug.c` reference the
  serial port API but are unreachable in normal play.

---

## Port notes

The port doesn't link this folder at all (or links and ignores).
Modern host has stdio + sockets, which the port uses where
debug output is needed.
