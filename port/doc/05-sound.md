# Sound System: PSX SPU vs Port SPU Emulator

This document describes the PSX SPU hardware, the original MGS sound driver
architecture, and the software SPU emulator used in the macOS port.

## 1. PSX SPU Hardware

### 1.1 Overview

The PSX Sound Processing Unit (SPU) is a dedicated audio chip with:
- 512KB dedicated Sound RAM (separate from main RAM)
- 24 hardware voices
- ADPCM decompression (16 bytes -> 28 samples, 4:1 compression)
- Per-voice ADSR envelope generator
- Hardware reverb processor
- 44100 Hz output (stereo)

### 1.2 SPU RAM Layout (MGS)

```
0x0000-0x100F: System reserved (CD audio buffer, capture buffers)
0x1010:        Blank data (silence)
0x1210:        Wave data start (sound effects, instruments)
  |
0x75010:       BGM right channel start
0x77010:       BGM left channel start
  |
0x7FFFF:       End of SPU RAM (512KB)
```

### 1.3 ADPCM Block Format

Each 16-byte ADPCM block decodes to 28 audio samples:

```
Byte 0: [shift:4][filter:3][0:1]
Byte 1: [flags]
   bit 0: loop end
   bit 1: loop (jump to loop_addr)
   bit 2: loop start (set loop_addr here)
Bytes 2-15: 28 nibbles (14 bytes, 2 nibbles each)
```

**Decoding algorithm**:
```
for each nibble in block:
    sample = (nibble << 12) >> shift   // Sign-extend and shift
    sample += (prev1 * f0 + prev2 * f1 + 32) >> 6  // IIR filter
    prev2 = prev1
    prev1 = clamp(sample, -32768, 32767)
```

**Filter coefficients** (5 filter modes):

| Filter | f0 (positive) | f1 (negative) |
|--------|---------------|---------------|
| 0      | 0             | 0             |
| 1      | 60            | 0             |
| 2      | 115           | -52           |
| 3      | 98            | -55           |
| 4      | 122           | -60           |

### 1.4 ADSR Envelope

Each voice has a 4-phase envelope: Attack, Decay, Sustain, Release.

```
Level
  ^
  |     /\
  |    /  \_______ Sustain Level
  |   /    Decay  \
  |  / Attack      \  Release
  | /                \
  +-------------------+----> Time
```

The envelope level (0-0x7FFF) modulates the sample amplitude.

**PSX ADSR register encoding** (32-bit):
```
bits  0-4:  Sustain Level (SL)
bits  5-8:  Decay Rate (DR) -- always exponential decrease
bits  9-14: Attack Rate (AR)
bit  15:    Attack Mode (0=linear, 1=exponential)
bits 16-20: Release Rate (RR)
bit  21:    Release Mode (0=linear, 1=exponential)
bits 22-26: Sustain Rate (SR)
bits 27-28: unused
bit  29:    Sustain Direction (0=increase, 1=decrease)
bit  30:    Sustain Mode (0=linear, 1=exponential)
```

**Rate tables**: The SPU uses a counter-based rate system. Each rate value (0-127)
maps to a denominator and numerator pair. The counter increments each sample tick
(44100 Hz). When the counter reaches the denominator, the envelope level changes by
the numerator amount.

### 1.5 Pitch and Sample Rate

Voice pitch is a 16-bit value where 0x1000 = 44100 Hz (1:1 playback).

```
actual_rate = 44100 * pitch / 0x1000
```

Common values: 0x800 = 22050 Hz, 0x1000 = 44100 Hz, 0x2000 = 88200 Hz.

The SPU uses Gaussian interpolation between samples for smooth pitch shifting.

### 1.6 Gaussian Interpolation

The PSX SPU interpolates between 4 neighboring samples using a 512-entry coefficient
table. The fractional position (bits 4-11 of the sample counter) selects the table
index:

```
idx = (frac_pos >> 4) & 0xFF   // 0-255
out = gauss[0x0FF-idx] * s0    // oldest sample
    + gauss[0x1FF-idx] * s1
    + gauss[0x100+idx] * s2
    + gauss[0x000+idx] * s3    // newest sample
out >>= 15
```

This matches the hardware SPU exactly (the coefficient table is from pcsx-redux).

## 2. MGS Sound Driver Architecture

### 2.1 Source Files

```
source/sound/
  sd_main.c   -- Init, memory allocation, main tick (IntSdMain)
  sd_cli.c    -- Command interface (SdCommand)
  sd_drv.c    -- Low-level SPU register writes
  sd_sub1.c   -- Sequence engine (music/SFX playback)
  sd_sub2.c   -- Additional sequence processing
  sd_file.c   -- File loading (wave banks, SE, songs)
  sd_str.c    -- Stream audio (VOX, cutscene audio)
  sd_ioset.c  -- I/O parameter batching
  sd_wk.c     -- Work area management
  se_tbl.c    -- Sound effect table (mapping IDs to SPU params)
```

### 2.2 Sound Driver Data Flow

```
Game Code
  |
  +-- GM_SeSet(position, se_id)         -- Play sound effect
  +-- sd_command(SNG_PLAY, song_id)     -- Play music
  +-- StartStream(header)               -- Start VOX/cutscene audio
  |
  v
SdCommand Queue
  |
  v
IntSdMain() [called every game tick]
  |
  +-- Process command queue
  +-- sound_sub() -- Sequence engine tick
  |    +-- For each track:
  |    |    +-- Accumulate tempo
  |    |    +-- Process sequence commands when gate expires
  |    |    +-- Apply modulation (vibrato, portamento, sweep)
  |    +-- Batch SPU writes via sd_ioset
  |
  +-- WaveSpuTrans() -- Transfer wave data to SPU RAM
  +-- StrSpuTrans()  -- Transfer stream data to SPU voices
  +-- StrFadeInt()   -- Stream volume fade
```

### 2.3 Sequence Command Format

The sequence engine processes a bytecode stream. Commands 0x80-0xFF are control
commands; values 0x00-0x7F are note data.

**Critical bug found**: The original decompilation had `(char)mdata1 >= 128` which is
ALWAYS FALSE for signed char (wraps to negative). This was fixed with
`(unsigned char)mdata1 >= 128`. This single bug made ALL sequence control commands
(volume, tone, ADSR, pan, vibrato, etc.) unreachable -- explaining why sound effects
played but had wrong volume/pitch.

Key sequence commands:
| Opcode | Name      | Description                        |
|--------|-----------|------------------------------------|
| 0x80   | vol_chg   | Set volume (pvod = mdata2 << 8)    |
| 0x81   | ton_chg   | Set tone/pitch                     |
| 0x82   | pan_chg   | Set stereo pan position            |
| 0x83   | adsr_chg  | Set ADSR envelope parameters       |
| 0x90   | vib_dpt   | Vibrato depth                      |
| 0x91   | vib_spd   | Vibrato speed                      |
| 0x92   | dtun_chg  | Detune                             |
| 0xA0   | loop_s    | Loop start marker                  |
| 0xA1   | loop_e    | Loop end (decrement counter)       |
| 0xB0   | blk_end   | End of sequence block               |
| 0xFE   | note_rest | Rest (silence for N ticks)         |

### 2.4 Sound File Loading

**Wave banks (.wvx)**:
- Header: array of 16-byte `WAVE_W` entries (PSX format)
- Body: raw ADPCM data uploaded to SPU RAM via `SpuWrite()`
- Port converts 16-byte entries to port struct with 64-bit pointers

**Sound effects (.e)**:
- `SETBL` struct: 16 bytes on PSX (contains SPU address, pitch, ADSR)
- Port converts to larger struct (32+ bytes) with proper field sizes

**Song data (.mdx)**:
- Sequence bytecode for the music engine
- Loaded to `sng_data` buffer

### 2.5 Stream Audio (VOX/Demo)

Cutscene voice and demo audio use the streaming system:

```
StartStream(header)
  |
  +-- Parse stream header (unsigned char* to avoid sign extension!)
  +-- Set str_status = 2 (loading)
  |
  v
StrSpuTransWithNoLoop() [called each tick]
  |
  +-- Transfer ADPCM blocks to SPU voices 21/22 (L/R)
  +-- Advance through stream buffer
  +-- Set str_status progression: 2 -> 3 -> 4 -> 5 -> 6
```

**Important**: The stream header must be cast to `unsigned char*` because values 0x80+
sign-extend incorrectly when read through `char*` (signed on ARM64).

## 3. Port SPU Emulator

### 3.1 Architecture

```
port/sound/spu_emu.c
  |
  +-- spu_emu_init()       -- Open SDL2 audio device (44100Hz, stereo, S16)
  +-- SpuSetVoiceAttr()    -- Configure voice parameters
  +-- SpuSetKey()          -- Key on/off voices
  +-- SpuWrite()           -- Copy ADPCM data to emulated SPU RAM
  +-- SpuRead()            -- Read back from SPU RAM
  +-- spu_audio_callback() -- SDL2 callback: mix all 24 voices
```

### 3.2 Voice Structure

```c
typedef struct {
    // Configuration (set by SpuSetVoiceAttr)
    uint32_t addr;           // Start address in SPU RAM
    uint32_t loop_addr;      // Loop point address
    uint16_t pitch;          // Playback rate (0x1000 = 44100Hz)
    int16_t  vol_l, vol_r;   // Volume (-0x3FFF to 0x3FFF)
    // ADSR parameters
    uint16_t ar, dr, sr, rr; // Attack/Decay/Sustain/Release rates
    uint16_t sl;             // Sustain level
    uint8_t  ar_mode, sr_mode, sr_dir, rr_mode;

    // Playback state
    int      active;         // Voice is playing
    int      key_off;        // Release phase triggered
    uint32_t cur_addr;       // Current address in SPU RAM
    int      sample_idx;     // Index within current ADPCM block (0-27)
    uint32_t frac_pos;       // 16.16 fractional sample position
    int16_t  prev1, prev2;   // ADPCM filter history
    int16_t  decoded[28];    // Current decoded ADPCM block
    int16_t  prev_decoded[3]; // Last 3 samples for Gaussian interpolation

    // Envelope state
    int      env_phase;      // ATTACK/DECAY/SUSTAIN/RELEASE
    int      env_level;      // Current level (0-0x7FFF)
    int      env_counter;    // Rate counter
} SPU_Voice;
```

### 3.3 Audio Callback

The SDL2 audio callback runs on a separate thread at 44100 Hz:

```c
void spu_audio_callback(void *userdata, Uint8 *stream, int len) {
    int16_t *out = (int16_t *)stream;
    int n_samples = len / 4;  // stereo 16-bit = 4 bytes per sample

    for (int s = 0; s < n_samples; s++) {
        int mix_l = 0, mix_r = 0;

        for (int v = 0; v < 24; v++) {
            SPU_Voice *voice = &spu_voices[v];
            if (!voice->active) continue;

            // Tick envelope (per-sample, 44100Hz)
            env_tick(voice);

            // Gaussian interpolation between 4 samples
            int sample = gauss_interpolate(voice);

            // Apply envelope
            sample = (sample * voice->env_level) >> 15;

            // Apply per-voice volume
            mix_l += (sample * voice->vol_l) >> 15;
            mix_r += (sample * voice->vol_r) >> 15;

            // Advance playback position
            voice->frac_pos += voice->pitch;
            // Load next ADPCM block when position crosses block boundary
            while ((voice->frac_pos >> 16) >= 28) {
                voice->frac_pos -= (28 << 16);
                advance_to_next_block(voice);
            }
        }

        // Apply master volume and clamp
        out[s*2+0] = clamp(mix_l * master_vol_l / 0x3FFF, -32768, 32767);
        out[s*2+1] = clamp(mix_r * master_vol_r / 0x3FFF, -32768, 32767);
    }
}
```

### 3.4 ADSR Envelope Implementation

The port uses pcsx-redux rate tables for accurate envelope timing:

```c
static void env_tick(SPU_Voice *v) {
    int rate, step;
    switch (v->env_phase) {
    case ATTACK:
        rate = v->ar;
        if (v->ar_mode && v->env_level >= 0x6000)
            rate += 8;  // Exponential slowdown near peak
        step = adsr_num_inc(rate);
        v->env_counter += step;
        if (v->env_counter >= adsr_denom(rate)) {
            v->env_counter = 0;
            v->env_level += step;
            if (v->env_level >= 0x7FFF) {
                v->env_level = 0x7FFF;
                v->env_phase = DECAY;
            }
        }
        break;
    case DECAY:
        // Exponential decrease toward sustain level
        ...
    case SUSTAIN:
        // Linear or exponential, increase or decrease
        ...
    case RELEASE:
        // Linear or exponential decrease to 0
        ...
    }
}
```

### 3.5 Differences from PSX SPU

| Feature           | PSX SPU              | Port Emulator           |
|-------------------|----------------------|-------------------------|
| Processing        | Hardware, parallel   | Software, sequential    |
| Sample rate       | 44100 Hz fixed       | 44100 Hz (SDL2)         |
| ADPCM decoding    | Hardware per-voice   | Software decode_block() |
| ADSR envelope     | Hardware counter     | Software rate tables    |
| Interpolation     | Gaussian 4-point     | Gaussian 4-point (same) |
| Reverb            | Hardware processor   | Not implemented         |
| Noise generator   | Hardware LFSR        | Not implemented         |
| SPU RAM transfer  | DMA (async)          | Instant memcpy          |
| IRQ callback      | Hardware interrupt   | Not implemented         |
| Voice priority    | N/A (hardware)       | N/A (all 24 available)  |

### 3.6 Sound Driver Integration

The original sound driver (`sd_main.c` etc.) is compiled from source. The port provides:

- `sd_mem_alloc()`: Replaces PSX hardcoded address `0x801E0000` with
  `static unsigned char sd_mem_buf[0x40000]`
- `PcmOpen/PcmRead/PcmClose`: Redirected to `port_PcmOpen` etc. for disc file access
- `WaveSpuTrans()`: PORT_BUILD path transfers wave data from cdload_buf to SPU RAM
- Sound file loading (.wvx, .e, .mdx): Port converts PSX structs to 64-bit equivalents

### 3.7 Stream System

Stream audio (VOX/DEMO.DAT) uses a state machine:

```
State 1: Idle
State 2: Loading header
State 3: Parsing entries
State 4: Ready (str_tick_count set to 0)
State 5: Playing (transfers to SPU voices, subtitle timing)
State 6: Ending
```

The port caps stream buffers to 4MB (PSX loads entire rest of file which can be 248MB).
`FS_StreamGetSize` reads from the header tag (stream-4), not the data itself.
