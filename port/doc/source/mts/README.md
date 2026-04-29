# `source/mts/` — multi-task scheduler

The PSX-side cooperative task scheduler. PSX has no OS-level
threads; MGS runs *coroutines* with explicit yield points to give
the CD reader, sound DMA, and pad polling time-slices alongside
the main game loop.

## Files

| File | Role |
| ---- | ---- |
| [`mts_new.c`](../../../../source/mts/mts_new.c) | The "new" task system (post-rewrite). Used by the released game. |
| [`mts_sub.c`](../../../../source/mts/mts_sub.c) | Sub-task helpers. |
| [`mts_pad.c`](../../../../source/mts/mts_pad.c) | Pad-input task — polls controller via BIOS, fills `GV_PadData`. |
| [`mask.c`](../../../../source/mts/mask.c) | Interrupt mask helpers. |
| [`mts.h`](../../../../source/mts/mts.h) | Public task API. |
| [`taskid.h`](../../../../source/mts/taskid.h) | Task ID enum (TASK_VBLANK, TASK_CD, TASK_SOUND, …). |
| [`terminal.h`](../../../../source/mts/terminal.h) | Debug terminal API (serial-port bound — stubs in release). |

## Task types

The MTS scheduler runs ~6 concurrent "tasks":

| Task | Triggered by | Job |
| ---- | ------------ | --- |
| Main game loop | every V-blank | Run `gamed.c::Act`, `GV_ExecActorSystem` |
| Sound | SPU IRQ | Process VOX/song streaming (`sd_main.c`) |
| CD streamer | CD interrupt | Pump sectors from CD into stream ring buffer |
| Pad | every V-blank | Poll controllers, write to `GV_PadData` |
| Sound init | infrequent | Dispatch song / sample loads |
| Debug | (release: never) | Serial-port terminal |

`mts_sta_tsk(task_id, fn)` schedules a task. `mts_wait_vbl(n)`
yields for n V-blanks.

## On the port

The port replaces this entire system with single-threaded
callbacks on the main render loop. There's no IRQ; SPU is a
pull-driven emulator, CD is replaced by stdio, pad is SDL events.

`port/mts.c` provides stub implementations that do nothing or
forward to host equivalents.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (mts_new vs older,
  task priority levels, V-blank timing).
- [`source/sound/`](../sound/README.md) — the sound system uses
  MTS for the SPU pump task.
- [`source/libfs/`](../libfs/README.md) — CD streaming uses MTS
  for sector reads.
- [`port/mts.c`](../../../../port/mts.c) — port replacement.
