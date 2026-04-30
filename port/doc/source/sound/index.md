# `source/sound/` — SPU streaming + sequence player

The sound library — songs, sound effects, voice. PSX SPU
(Sound Processing Unit) is fed by:

- A **wave** stream — short samples (gunshots, voice clips).
- A **sequence** stream — MIDI-like per-channel note events for
  music.
- A **VOX** stream — long voice clips for codec calls.

All three play concurrently. This folder is the orchestrator.

## Files

### Top-level driver
| File | Role |
| ---- | ---- |
| [`sd_main.c`](../../../../source/sound/sd_main.c) | Main driver — `SdMain` task, the per-IRQ sound pump. |
| [`sd_drv.c`](../../../../source/sound/sd_drv.c) | Lower-level driver — wraps SPU register pokes. |
| [`sd_cli.c`](../../../../source/sound/sd_cli.c) | "Client" API — what the rest of the engine calls (`SD_PlaySong`, `SD_StopAll`). |
| [`sd_ioset.c`](../../../../source/sound/sd_ioset.c) | I/O setup — initialise SPU registers. |
| [`sd_file.c`](../../../../source/sound/sd_file.c) | File loading — `SD_LoadWvx`, `SD_LoadMdx`. |
| [`sd_str.c`](../../../../source/sound/sd_str.c) | Stream playback — `SD_StrPlay`, `SD_StrStop`. |
| [`sd_sub1.c`](../../../../source/sound/sd_sub1.c) | Sequence player sub-routines (note-event dispatch). |
| [`sd_sub2.c`](../../../../source/sound/sd_sub2.c) | More sub-routines. |
| [`sd_wk.c`](../../../../source/sound/sd_wk.c) | Worker — runs in MTS task slot, advances songs. |
| [`se_tbl.c`](../../../../source/sound/se_tbl.c) | Sound-effect table — names → wave-bank slots. |

### Headers
| File | Role |
| ---- | ---- |
| [`g_sound.h`](../../../../source/sound/g_sound.h) | Public API. |
| [`sd_cli.h`](../../../../source/sound/sd_cli.h) | Client struct definitions. |
| [`sd_ext.h`](../../../../source/sound/sd_ext.h) | Extended API. |
| [`sd_incl.h`](../../../../source/sound/sd_incl.h) | Includes meta-header. |

## Audio data formats

The disc ships:

- **`*.wvx`** — Wave-bank. Per stage. ADPCM samples (gunshots,
  footsteps, voices). Loaded into SPU RAM at stage entry.
- **`*.mdx`** — MIDI-like sequence data. The "song" file.
- **`VOX.DAT`** — Streamed voice clips for codec / ambient lines.
  Sectors keyed (similar to DEMO.DAT).

## Per-tick flow

```
SPU IRQ (every ~24ms)
    ↓
SdInt (interrupt handler)
    ↓
IntSdMain (drives sequence advance)
    ↓
   ├─ Advance song positions
   ├─ Pump VOX stream (if active)
   └─ Service wave channel updates

Game loop (60 Hz)
    ↓
GM_PlaySE(name) → SD_PlaySE(slot) → write SPU keys
GM_VoxStream(id) → start VOX playback
```

## Public API (game-facing)

```c
void SD_PlaySong(int song_id);
void SD_PlaySE(int slot, int volume);     /* one-shot wave */
void SD_StrPlay(int sectors, int loop);   /* start streaming */
void SD_StrStop(void);
void SD_FadeOut(int frames);
```

## Used by

- `game/sound.c` — game-tier wrappers.
- `game/strctrl.c` — story-cinematic stream control.
- Every `chara/`, `enemy/`, `weapon/` actor that emits sound.

---

## Port notes

The port replaces the SPU emulation with
[`port/spu_emu.c`](../../../../port/spu_emu.c) (a software SPU
emulator) + SDL audio output. The disc's `sd_main.c` etc. still
runs but its register pokes go to `spu_emu.c` rather than
hardware.

Some pump routines are missing from the port — see
[doc/demo/08-known-issues.md](../../demo/08-known-issues.md) under
"No audio" for the editor's playback gap.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [architecture.md](architecture.md) | Full SPU + streaming + sequence + wave stack |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`source/mts/`](../mts/index.md) — the task scheduler that
  feeds SPU pumps.
- [`source/menu/radio.c`](../../../../source/menu/radio.c) — the
  codec system uses VOX streaming.
- [`source/kojo/demothrd.c`](../../../../source/kojo/demothrd.c) —
  cinematic streamer reads sound + DMO from same DEMO.DAT.
