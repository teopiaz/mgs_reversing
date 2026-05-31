# `source/mts/` — multi-task scheduler

MTS ("Multi-Task System") is the cooperative task scheduler that the
PSX build runs on. The PSX has no operating system to provide threads
or interrupts at a comfortable level of abstraction, so MGS ships its
own kernel — small enough to fit alongside the game on a 2 MB R3000
but featureful enough to give the CD reader, sound DMA, controller
poll, and game logic each their own stack and message queue.

Internal documentation in the source headers credits the system to
the **beatmania APPEND 5thMIX** library (1998), which MGS adopted
wholesale; you'll find Konami's "beatmania" filenames embedded in the
`taskid.h` comment block.

## What MTS provides

A traditional embedded RTOS interface, scaled down to what an action
game actually needs:

- **Tasks** — preempt-free coroutines with their own stack. The
  scheduler runs the highest-ready-priority task until it yields
  (`mts_slp_tsk`), waits on a message (`mts_recv_msg`), waits on a
  V-blank (`mts_wait_vbl`), or returns from its top frame
  (`mts_ext_tsk`).
- **Messages** — `mts_send_msg(dst, d0, d1)` enqueues a two-word
  message to task `dst`. `mts_recv_msg` blocks until something
  arrives. Used as the universal "wake the consumer" primitive
  between CD ↔ stream ↔ sound, and game ↔ pad.
- **Semaphores** — `mts_lock_sem` / `mts_unlock_sem` guard shared
  state (mainly the SPU register file and the VRAM upload queue).
- **V-blank synchronisation** — `mts_wait_vbl(n)` is the canonical
  game-thread "give up the rest of this frame" call. The vsync ISR
  unblocks tasks at the head of the v-blank wait queue.
- **Per-task malloc** — `mts_free_all(owner)` reclaims everything an
  exited task allocated, so the CD-bios task can leak buffers safely
  during its lifetime and have them swept on exit.
- **Stack guarding** — `mts_set_stack_check` plants a sentinel; the
  scheduler complains on overflow. Pads / kicks the stack into a
  known size so debug builds can spot a runaway recursion.

The scheduler is **non-preemptive**. A task runs until it explicitly
yields. Only the V-blank ISR and the SPU IRQ can pull a task off the
CPU — and they don't switch contexts, they just unblock waiting
tasks. This means hot loops in MGS that don't call any `mts_*` entry
will pin the CPU; in practice every actor's `Act` returns after one
iteration, and `GV_ExecActorSystem` is what loops over the queue.

## Task slot map (`taskid.h`)

| ID | Slot              | What runs here |
| -- | ----------------- | -------------- |
| 0  | `MTSID_SYSTEM`    | Idle / system housekeeping |
| 1  | `MTSID_SOUND_INT` | SPU IRQ handler — pumps the next ADPCM buffer per voice |
| 2  | `MTSID_SOUND_SET` | Sound-config processor (load wave bank, install sequence) |
| 3  | `MTSID_GAME`      | The main game loop — `gamed.c::Act`, `GV_ExecActorSystem` |
| 4  | `MTSID_SOUND_DUMMY` | Unused placeholder (kept to preserve the ID-space layout) |
| 5  | `MTSID_SOUND_MAIN`  | `sd_main.c::SdMain` — sequence advance, mixing oversight |
| 6  | `MTSID_CD_SYSTEM`   | CD streamer — reads sectors into the stream ring buffer |
| 7  | `MTSID_MEMCARD` *(old layout: `MTSID_MEMORY_CARD`)* | Memory-card I/O |
| 10 | `MTSID_CDBIOS`    | Wraps the PSX CD BIOS' async callbacks into MTS messages |

`MTSID_IDLE` is `MTS_NR_TASK - 1`; the scheduler always has a
do-nothing task pinned at the bottom so it has something to dispatch
when every other task is asleep.

## File map

| File | Role |
| ---- | ---- |
| [`mts_new.c`](../../../../source/mts/mts_new.c) | The released kernel — task switching, message queues, semaphores, the V-blank/SPU ISR plumbing. "New" because it replaced an earlier version mid-development. |
| [`mts_sub.c`](../../../../source/mts/mts_sub.c) | Helpers used by `mts_new.c` (queue manipulation, free-list walks). |
| [`mts_pad.c`](../../../../source/mts/mts_pad.c) | The pad task body — calls the PSX BIOS `PadStartCom` / `PadStopCom` async API, parses the response, fills `MTS_PAD` per channel, services rumble. |
| [`mask.c`](../../../../source/mts/mask.c) | Interrupt-mask save/restore — `mts_disable_int` / `mts_enable_int`. Used inside critical sections (queue updates) that the V-blank ISR could otherwise interrupt. |
| [`mts.h`](../../../../source/mts/mts.h) | Public API. Task creation, message, semaphore, sleep, V-blank, malloc, debug, controller, terminal. |
| [`mts_new.h`](../../../../source/mts/mts_new.h) | Internal data structures — `MTS_TCB` (task control block), `MTS_MSGQ`. |
| [`mts_pad.h`](../../../../source/mts/mts_pad.h) | `MTS_PAD` struct + button bit defines. |
| [`taskid.h`](../../../../source/mts/taskid.h) | The `MTSID_*` enum. |
| [`terminal.h`](../../../../source/mts/terminal.h) | Debug terminal API — `fprintf(stream, ...)`, `cprintf(...)`. Bound to the PSY-Q serial port in the dev build; release stubs them to no-ops. |

## Public API (engine-facing)

The headline calls — see `mts.h` for the full surface.

```c
/* Task lifecycle */
int  mts_sta_tsk(int task_id, void (*entry)(void), void *stack);
void mts_ext_tsk(void);                /* terminate self */

/* Message-passing */
void mts_send_msg(int dst, int d0, int d1);
int  mts_recv_msg(int src, int *d0, int *d1);   /* blocks */

/* Sleep / wake */
void mts_slp_tsk(void);
void mts_wup_tsk(int dst);

/* V-blank */
int  mts_wait_vbl(long count);         /* yield for N v-blanks */
int  mts_get_tick_count(void);         /* monotonic vsync counter */

/* Semaphore */
void mts_lock_sem(int no);
void mts_unlock_sem(int no);

/* Per-task malloc */
void *mts_malloc(size_t n);            /* owner = current task */
void  mts_free_all(long owner);        /* reclaim everything */

/* Controller */
int  mts_get_pad(int channel, MTS_PAD *pad);
int  mts_read_pad(int channel);        /* button word only */
void mts_set_pad_vibration(int channel, int time);
```

## Why the engine cares

Every "this happens while that happens" relationship in the engine is
expressed through MTS. A few load-bearing examples:

- **CD streaming** for codec voice — the game task calls
  `FS_StreamRead`, which posts a message to `MTSID_CD_SYSTEM`, which
  in turn calls into `MTSID_CDBIOS` for the actual sector read. The
  BIOS callback wakes the CD task, which copies bytes into the ring
  buffer and `mts_send_msg`-es the stream consumer. The game task
  has been sleeping in `mts_wait_vbl` the entire time.
- **SPU sequencer** — `sd_main.c::SdMain` runs in `MTSID_SOUND_MAIN`.
  Every SPU IRQ wakes `MTSID_SOUND_INT`, which updates voice state
  and posts a message back to `SOUND_MAIN`, which advances the
  sequence. The mixer never blocks the game loop.
- **Pad input** — `MTSID_GAME` reads `GV_PadData`, which is filled by
  `MTSID_SYSTEM`'s V-blank-driven pad task. Game logic never touches
  hardware directly.

## Pitfalls

- The "two-word message" interface (`d0, d1` ints) is the only IPC
  primitive — there's no shared buffer queue. Anything bigger goes
  through `mts_lock_sem` and a global pointer (e.g. the stream ring
  buffer descriptor).
- `mts_wait_vbl(0)` is not "yield once"; it's "wait until the next
  v-blank ISR fires", which depending on where you are in the frame
  is between 0 and 16 ms. `mts_slp_tsk` is the explicit "yield" — but
  you need someone else to call `mts_wup_tsk` on you, otherwise you
  sleep forever.
- The PSX BIOS is *not* thread-safe inside MTS. Pad polling, memcard
  access, and CD reads all wrap the BIOS in their dedicated task so
  that no two BIOS calls overlap.

## See also

- [`source/sound/architecture.md`](../sound/architecture.md) — the
  SPU pipeline, which is the largest consumer of MTS message-passing.
- [`source/libfs/streaming.md`](../libfs/streaming.md) — the CD
  streamer's MTS interactions.
- [`source/menu/codec.md`](../menu/codec.md) — codec dialog runs the
  full pad ↔ stream ↔ sound triangle.
- [_unreversed.md](_unreversed.md) — opaque areas (priority
  inversion handling, the exact `MTS_TCB` layout, the older
  `mts_old.c` that was rewritten into `mts_new.c`).

---

## Port notes

The port collapses the entire scheduler to a single thread. The PSX
fiction (one task per subsystem, sleeping on messages) is replaced
by:

| MTS task | Port equivalent |
| -------- | --------------- |
| `MTSID_SYSTEM` | No-op |
| `MTSID_SOUND_INT` / `_MAIN` | SDL audio callback runs the SPU emu pump; the sequence advance is folded into `IntSdMain` and called once per `port_render` from the main thread |
| `MTSID_GAME` | The main `while (g_running)` loop in `port/main.c` |
| `MTSID_CD_SYSTEM` / `_CDBIOS` | `port/libfs/libfs.c` — synchronous `fread` on the host disc image |
| `MTSID_MEMCARD` | Stubbed (no save/load yet) |

`port/mts/mts.c` provides the API surface — `mts_sta_tsk` records the
function pointer but doesn't actually start a task, `mts_wait_vbl`
returns immediately, `mts_send_msg` / `mts_recv_msg` use a tiny in-
process FIFO so the few places that genuinely need round-trip
messaging (notably codec voice triggers) still work. The trade-off:
strictly cooperative semantics are gone (the SDL audio callback IS
preemptive), which is why `port/spu_emu.c` wraps every shared write
in `SDL_LockAudioDevice`.

The `fprintf(stream, …)` debug-terminal entry from `terminal.h` is
where the port's `#define fprintf(stream, ...) printf(...)` macro
comes from — the original code passed a *task-local* stream id where
modern stdio expects a `FILE*`, and the macro papers over that
mismatch. Port-native files that need real-`FILE*` writes (config
save, photo export) `#undef fprintf` at the top to escape it.
