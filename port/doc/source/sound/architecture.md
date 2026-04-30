---
file: source/sound/{sd_main,sd_drv,sd_cli,sd_str,sd_file,sd_ioset,sd_sub1,sd_sub2,sd_wk,se_tbl}.c + g_sound.h
---

# `source/sound/` — SPU streaming + sequence + wave

Audio stack on PSX is unusual: a small dedicated SPU (Sound
Processing Unit) plays voice channels from VRAM, fed by DMA. MGS's
sound code is a custom mini-OS on top of that.

## Files

| File | Role |
| ---- | ---- |
| `sd_main.c` | Top-level — main pump task, orchestrates everything |
| `sd_drv.c` | Low-level driver — SPU register write helpers |
| `sd_cli.c` | Client API — what game code calls (`SD_PlayVox`, etc.) |
| `sd_str.c` | Streaming layer — fills SPU sample buffers from CD/RAM |
| `sd_file.c` | File I/O — VOX/SEQ/SAMPLE format parser |
| `sd_ioset.c` | I/O setup — initialise SPU registers, DMA channels |
| `sd_sub1.c` / `sd_sub2.c` | Internal helpers (envelope, pitch) |
| `sd_wk.c` | Per-voice work struct allocator |
| `se_tbl.c` | Sound-effect table — SE id → (sample, pitch, etc.) |
| `se_data/` | Compiled sound effects (small samples) |

## The audio types

| Type | What | Source |
| ---- | ---- | ------ |
| **VOX** | Voice (codec dialogue, character grunts) | `.vox` files, streamed |
| **SEQ** | Music sequence (notes + tempo) | `.seq` files |
| **WAVE** | Sample bank (instruments) | `.wav` chunk inside .seq |
| **SE** | Sound effect (gunshot, footstep) | `se_data/*.dat`, embedded |
| **AMB** | Ambient loop (wind, machinery) | streamed like VOX |

The four are routed through different sub-systems:

- VOX / AMB: `sd_str.c` (streaming).
- SEQ / WAVE: `sd_main.c::SDD_Play_Seq` plays via SPU note triggers.
- SE: pre-loaded into VRAM at boot; `SD_PlaySE(id)` triggers a SPU voice.

## Streaming model (PSX)

The SPU has 24 voice channels, each consuming samples from VRAM.
Long sounds (codec voice) don't fit in VRAM, so they stream:

```
CD stream → main RAM ring buffer
              ↓ (DMA)
            SPU VRAM ring (per voice)
              ↓
            SPU plays audio
```

`sd_str.c` runs as an MTS task (highest priority) that:

1. Polls voice playback positions.
2. When a voice has consumed half its VRAM ring, schedules a CD
   read for the next chunk.
3. DMAs main-RAM data into SPU VRAM at the next available offset.
4. Updates voice loop point so playback continues seamlessly.

## API (game-side)

```c
int  SD_PlayVox(int vox_id);            // start voice playback
void SD_StopVox(int vox_id);
int  SD_GetVoxPos(int vox_id);          // current sample offset (for lip-flap)
int  SD_PlaySE(int se_id);              // one-shot SE
void SD_StopSE(int handle);
int  SD_PlayBGM(int song_id);           // sequenced music
void SD_StopBGM(void);
void SD_PauseAudio(int level);
```

## Voice-allocation: 24 SPU channels

The 24 channels are partitioned roughly:

- 1–2: BGM melody
- 3–10: BGM instruments (drums, bass, etc.)
- 11–14: Voice / VOX (codec)
- 15–24: SE pool (round-robin)

`sd_wk.c::SD_GetWork` allocates from the appropriate sub-pool based
on sound type.

## Pitfalls

- **VRAM is tight (512 KB).** Pre-loaded SE bank + per-voice
  ring buffers compete. Adding samples means rebuilding the bank.
- **CD streaming has latency.** A VOX has ~200ms of buffer; longer
  CD seek times can starve playback. The port replaces CD with
  RAM, so this disappears.
- **Music bank changes are expensive.** Changing songs requires
  reloading the wave bank — several seconds of audio interruption.
- **Voice-amplitude lip-flap is sample-precise.** `SD_GetVoxPos`
  returns the current SPU offset; codec uses it to pick the
  right mouth frame.

---

## Port notes

The port replaces `sd_drv.c` / `sd_str.c` with a software SPU
emulator + SDL audio output (`port/sound.c`). Streaming becomes
trivial because everything lives in RAM.

[`port/sound.c`](../../../../port/sound.c) implements:
- A pull-driven SPU emulator (16-bit ADPCM decode, ADSR envelopes,
  reverb).
- SDL_OpenAudioDevice callback that mixes 24 voices.
- A 1-tick lag (~22 ms) acceptable for game audio.

The port has been the focus of several debugging passes — see
[codec_radio_issue.md](#) and [reference_testvox.md](#).

## See also

- [`source/menu/codec.md`](../menu/codec.md) — biggest VOX consumer.
- [`source/mts/`](../mts/index.md) — MTS scheduler hosts the
  streaming task.
- [`source/libfs/`](../libfs/index.md) — disc streaming source.
- [`port/doc/codec/`](../../codec/) — port-side codec deep-dive.
