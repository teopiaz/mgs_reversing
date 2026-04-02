# Sound System Architecture

This document describes the PSX SPU hardware, the original MGS sound driver
architecture, and the software SPU emulator used in the macOS port.

---

## 1. PSX SPU Hardware

The PlayStation Sound Processing Unit provides:

- **24 voice channels**: Each can play one sample simultaneously.
- **512 KB Sound RAM**: Dedicated memory for sample data, separate from main RAM.
- **ADPCM compression**: 4-bit adaptive differential pulse-code modulation.
  16 bytes of ADPCM data decode to 28 PCM samples (~1.75:1 compression).
- **Hardware ADSR envelopes**: Each voice has Attack, Decay, Sustain level, and
  Release parameters, processed entirely in hardware.
- **Hardware reverb**: 10 preset reverb modes (room, hall, space, etc.).
  Configurable depth and work area in sound RAM.
- **Pitch**: 16-bit pitch value per voice. 0x1000 = 44100 Hz base rate.
  Higher values = higher pitch. Range covers ~1 Hz to ~176 kHz.
- **DMA transfers**: Main RAM -> Sound RAM via DMA channel 4.
- **IRQ**: Can trigger an interrupt when playback reaches a specific address
  in sound RAM (used for streaming synchronization).

### ADPCM Block Format

Each ADPCM block is 16 bytes and decodes to 28 PCM samples:

```
Byte 0: [shift:4][filter:4]
  shift  = right-shift amount for nibble data (0-12)
  filter = prediction filter index (0-4)
Byte 1: [flags]
  bit 0: loop end (stop or jump to loop start)
  bit 1: loop repeat (if set with bit 0, jump to loop addr; else stop)
  bit 2: loop start (mark this block as the loop point)
Bytes 2-15: 28 4-bit nibbles (2 per byte, low nibble first)
```

Decoding formula for each sample:
```
nibble = sign_extend_4bit(raw_nibble)
sample = nibble << (12 - shift)
sample += (prev1 * filter_pos[filter] + prev2 * filter_neg[filter] + 32) >> 6
prev2 = prev1
prev1 = clamp16(sample)
```

### Filter Coefficients

| Filter | Positive | Negative |
|--------|----------|----------|
| 0      | 0        | 0        |
| 1      | 60       | 0        |
| 2      | 115      | -52      |
| 3      | 98       | -55      |
| 4      | 122      | -60      |

---

## 2. MGS Sound Driver Architecture

The sound system is spread across several source files in `source/sound/`:

### sd_main.c

Entry points for the sound system:

- **`SdMain` task**: Handles file loading, stream management, and high-level
  sound commands. Runs as a cooperative task via MTS (the game's task system).
- **`SdInt` task**: Real-time voice processing. Runs at a higher priority and
  handles per-tick updates to voices (pitch slides, volume fades, sequencer
  advancement).

### sd_cli.c

Command interface for the rest of the game:

```c
int sd_set_cli(int sound_code, int sync_mode);
```

The `sound_code` parameter encodes the command type in its high byte:

| High byte | Command type    | Description                           |
|-----------|----------------|---------------------------------------|
| 0x00      | SE (sound FX)  | Play a sound effect                   |
| 0x01      | BGM (music)    | Play/stop/change background music     |
| 0x02      | Load SE        | Load sound effect data from disc      |
| 0xE0      | Stream         | Start/stop PCM audio streaming        |
| 0xFE      | Load WAV       | Load WAV sample data to SPU RAM       |

Other functions:
- `sd_task_active()`: Returns whether the sound task is running.
- `sd_str_play()`, `sd_sng_play()`, `sd_se_play()`: Playback status queries.
- `SePlay(code)`: Convenience wrapper for playing a sound effect.
- `get_sng_code()`: Returns current song ID.
- `get_sd_buf(size)`: Allocate a sound data buffer.

### sd_drv.c

Song/music sequencer:

- Processes up to **13 tracks** per song.
- Per-tick updates: note on/off, pitch bend, volume change, pan change.
- Songs are stored in a custom binary format loaded from 'm' (music) data
  entries in the stage archive.

### sd_str.c

PCM audio streaming:

- Double-buffered SPU transfer: While one buffer plays, the next is loaded
  from disc and DMA'd to SPU RAM.
- IRQ-driven: The SPU IRQ fires when playback reaches the boundary between
  buffers, triggering the next DMA transfer.
- Used for voice acting, FMV audio, and long ambient sounds.

### sd_ioset.c

SPU voice parameter batching:

- `SPU_TRACK_REG[23]`: Array of per-voice parameter change records.
- Accumulates volume, pitch, and ADSR changes throughout a tick.
- Flushes all changes to the SPU hardware at the end of the tick via
  `SpuSetVoiceAttr`, minimizing SPU bus contention.

### sd_file.c

Sound data file I/O:

- `PcmOpen(filename)`: Open a streaming audio file.
- `PcmRead(buffer, size)`: Read PCM data.
- `PcmClose()`: Close the stream.
- `SD_SngDataLoadInit(id)`: Load song data by ID.
- `SD_SeDataLoadInit(id)`: Load SE data by ID.
- `SD_WavDataLoadInit(id)`: Load WAV samples by ID.

### se_tbl.c

Sound effect definition table:

- **128 SE entries**, each defining:
  - Priority level
  - Number of tracks (voices used)
  - Character association (which game character triggers this sound)
  - SPU voice parameters (pitch, volume, ADSR)

---

## 3. SPU Emulator

**File**: `port/sound/spu_emu.c`

Replaces the PSX SPU hardware with a software implementation using SDL2 audio
output.

### Core Data Structures

```c
static unsigned char spu_ram[512 * 1024];  /* Virtual SPU RAM */

typedef struct {
    /* Parameters (set by SpuSetVoiceAttr) */
    unsigned long addr;           /* sample start in SPU RAM */
    unsigned long loop_addr;      /* loop address */
    unsigned short pitch;         /* 0x1000 = 44100 Hz */
    short vol_l, vol_r;           /* -0x3FFF..0x3FFF */
    unsigned short ar, dr, sr, rr, sl;  /* ADSR params */

    /* Playback state */
    int active;                   /* key-on active */
    unsigned long cur_addr;       /* current decode position */
    unsigned long frac_pos;       /* fractional sample position (16.16) */

    /* ADPCM decoder */
    int prev1, prev2;             /* filter history */
    short decoded[28];            /* decoded sample ring buffer */

    /* ADSR envelope */
    EnvPhase env_phase;           /* OFF/ATTACK/DECAY/SUSTAIN/RELEASE */
    int env_level;                /* 0..0x7FFF */
} SPU_Voice;

static SPU_Voice voices[24];
```

### Audio Callback

SDL2 audio callback runs at 44100 Hz stereo, 1024-sample buffer:

```c
static void spu_audio_callback(void *userdata, Uint8 *stream, int len)
{
    short *out = (short *)stream;
    int num_samples = len / 4;   /* stereo 16-bit = 4 bytes/sample */

    for (int s = 0; s < num_samples; s++) {
        int mix_l = 0, mix_r = 0;

        for (int ch = 0; ch < 24; ch++) {
            SPU_Voice *v = &voices[ch];
            if (!v->active) continue;

            /* Get current sample from decoded buffer */
            int idx = (v->frac_pos >> 16) % 28;
            short sample = v->decoded[idx];

            /* Apply envelope and voice volume */
            int s16 = (sample * v->env_level) >> 15;
            mix_l += (s16 * v->vol_l) >> 14;
            mix_r += (s16 * v->vol_r) >> 14;

            /* Advance by pitch */
            v->frac_pos += v->pitch;
            if ((v->frac_pos >> 16) >= 28) {
                v->frac_pos -= (28 << 16);
                decode_adpcm_block(v);
            }

            /* Envelope tick (every 64 samples ~= 689 Hz) */
            if ((s & 63) == 0) env_tick(v);
        }

        /* Apply master volume and clamp */
        out[s*2+0] = clamp16((mix_l * master_vol_l) >> 14);
        out[s*2+1] = clamp16((mix_r * master_vol_r) >> 14);
    }
}
```

### ADPCM Decoder

`decode_adpcm_block(v)` reads 16 bytes from `spu_ram[v->cur_addr]`:

1. Extract shift (bits 0-3) and filter (bits 4-7) from byte 0.
2. Extract flags from byte 1 (loop start/end/repeat).
3. For each of 28 nibbles (bytes 2-15):
   - Sign-extend the 4-bit nibble.
   - Apply shift: `sample = nibble << (12 - shift)`.
   - Apply prediction filter: `sample += (prev1*f0 + prev2*f1 + 32) >> 6`.
   - Clamp to [-32768, 32767].
   - Update prev2/prev1 history.
4. Handle loop flags:
   - Flag 4 (loop start): save `cur_addr` as `loop_addr`.
   - Flag 1 (loop end) + Flag 2 (repeat): jump to `loop_addr`.
   - Flag 1 without Flag 2: deactivate voice.
5. Advance `cur_addr` by 16 bytes.

### ADSR Envelope

Simplified linear envelope (the real PSX SPU uses exponential curves):

```
ATTACK:   env_level += rate_attack    (ramp from 0 to 0x7FFF)
DECAY:    env_level -= rate_decay     (ramp down to sustain level)
SUSTAIN:  env_level holds             (slow decay based on sr parameter)
RELEASE:  env_level -= rate_release   (ramp down to 0, then deactivate)
```

Rates are computed as `0x7FFF / (param + 1)`, providing a simple linear
approximation. This differs from the real SPU which supports linear and
exponential modes for each phase.

### Implemented SPU API Functions

| Function                     | Implementation                              |
|-----------------------------|---------------------------------------------|
| `SpuInit()`                 | `spu_emu_init()` — open SDL audio device    |
| `SpuQuit()`                 | Close SDL audio device                      |
| `SpuReset()`                | Zero all voice state (with audio lock)      |
| `SpuSetTransferStartAddr()` | Set `spu_transfer_addr`                     |
| `SpuWrite(addr, size)`      | `memcpy` to `spu_ram[transfer_addr]`        |
| `SpuRead(addr, size)`       | `memcpy` from `spu_ram[transfer_addr]`      |
| `SpuIsTransferCompleted()`  | Always returns 1 (instant transfer)         |
| `SpuSetVoiceAttr(attr)`     | Update voice params (with SDL audio lock)   |
| `SpuGetVoiceAttr(attr)`     | Read back voice state                       |
| `SpuSetKey(on_off, bits)`   | Key on: reset ADPCM, start attack. Key off: enter release. |
| `SpuGetKeyStatus(bits)`     | Return ON/OFF/ENV status                    |
| `SpuSetCommonAttr(attr)`    | Set master volume L/R                       |
| `SpuInitMalloc()`           | Reset bump allocator to 0x1010              |
| `SpuMalloc(size)`           | Bump-allocate from SPU RAM                  |
| `SpuFree(addr)`             | No-op (bump allocator)                      |
| `SpuSetReverb()`            | No-op stub                                  |
| `SpuSetReverbModeParam()`   | No-op stub                                  |
| `SpuSetReverbVoice()`       | No-op stub                                  |
| `SpuSetIRQ()`               | Set `spu_irq_enabled`                       |
| `SpuSetIRQAddr()`           | Set `spu_irq_addr`                          |
| `SpuSetIRQCallback()`       | Store callback pointer                      |
| `SpuGetAllKeysStatus()`     | Fill 24-byte status array                   |

### Sound Driver Stubs

**File**: `port/sound/sd_stubs.c`

All sound driver functions are currently stubbed:
- `sd_set_cli()` returns 0 (command accepted, no action).
- `sd_task_active()` returns 1 (so `Main()` doesn't block waiting for sound).
- `SdMain()` prints "sound:" and returns.
- `SD_SngDataLoadInit()`, `SD_SeDataLoadInit()`, `SD_WavDataLoadInit()` return 0/NULL.
- `PcmOpen`, `PcmRead`, `PcmClose` return -1 (file not found).

---

## 4. Current Status

| Component         | Status                                               |
|------------------|------------------------------------------------------|
| SPU emulator     | Functional: ADPCM decode, 24-voice mixing, ADSR      |
| SDL audio output | Working: 44100Hz stereo S16                           |
| Sound driver     | Compiles but stubbed: all commands are no-ops         |
| File loading     | `PcmOpen` returns -1, so no sound data loads from disc|
| Music sequencer  | Stubbed: sd_drv.c not connected                      |
| Streaming        | Stubbed: sd_str.c not connected                      |
| Reverb           | Not implemented (stubs only)                         |

The SPU emulator is ready to produce audio as soon as sample data is loaded
into `spu_ram` and voices are keyed on. The blocking issue is that
`sd_file.c`'s `PcmOpen` (disc-based streaming) and `SD_WavDataLoadInit` (sample
loading) are not yet connected to the port's file system.

### Next Steps to Enable Sound

1. Implement `SD_WavDataLoadInit` to load WAV samples from the extracted stage
   data (the 'w' extension entries in DATACNF 's' mode).
2. Connect `SD_SeDataLoadInit` to load SE definition data ('e' entries).
3. Connect `SD_SngDataLoadInit` to load song/music data ('m' entries).
4. Wire the sound driver stubs to call actual `sd_cli.c`/`sd_drv.c` functions.
5. Implement IRQ callback for streaming audio synchronization.
