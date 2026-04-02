/**
 * Software SPU emulator — replaces PSX SPU hardware with SDL2 audio output.
 * Decodes PSX ADPCM, mixes 24 voices, outputs stereo PCM via SDL2 callback.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "libspu.h"

/*---------------------------------------------------------------------------*/
/* PSX ADPCM filter coefficients                                             */
/*---------------------------------------------------------------------------*/
static const int pos_adpcm_table[5] = { 0, 60, 115, 98, 122 };
static const int neg_adpcm_table[5] = { 0, 0, -52, -55, -60 };

/*---------------------------------------------------------------------------*/
/* SPU RAM (512 KB)                                                          */
/*---------------------------------------------------------------------------*/
#define SPU_RAM_SIZE (512 * 1024)
static unsigned char spu_ram[SPU_RAM_SIZE];
static unsigned long spu_transfer_addr = 0;
static unsigned long spu_malloc_ptr = 0x1010; /* after header area */
static int spu_malloc_count = 0;

/*---------------------------------------------------------------------------*/
/* Voice state                                                               */
/*---------------------------------------------------------------------------*/
#define NUM_VOICES 24

typedef enum {
    ENV_OFF,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvPhase;

typedef struct {
    /* Parameters set by SpuSetVoiceAttr */
    unsigned long addr;        /* sample start address in SPU RAM */
    unsigned long loop_addr;   /* loop address */
    unsigned short pitch;      /* playback pitch (0x1000 = 44100Hz) */
    short vol_l, vol_r;        /* voice volume */
    unsigned short ar, dr, sr, rr, sl; /* ADSR */
    long a_mode, s_mode, r_mode;

    /* Playback state */
    int active;                /* key-on active */
    int key_off;               /* release triggered */
    unsigned long cur_addr;    /* current decode address */
    int sample_idx;            /* index within decoded block (0-27) */
    unsigned long frac_pos;    /* fractional position for pitch (16.16) */

    /* ADPCM decoder state */
    int prev1, prev2;

    /* Decoded sample buffer (28 samples per block) */
    short decoded[28];

    /* ADSR envelope */
    EnvPhase env_phase;
    int env_level;             /* 0..0x7FFF */
} SPU_Voice;

static SPU_Voice voices[NUM_VOICES];
static short master_vol_l = 0x3FFF;
static short master_vol_r = 0x3FFF;

/* IRQ */
static SpuIRQCallbackProc spu_irq_callback = NULL;
static unsigned long spu_irq_addr = 0;
static int spu_irq_enabled = 0;

/* SDL audio */
static SDL_AudioDeviceID audio_dev = 0;

/*---------------------------------------------------------------------------*/
/* ADPCM block decoder                                                       */
/*---------------------------------------------------------------------------*/
static void decode_adpcm_block(SPU_Voice *v)
{
    unsigned char *block = &spu_ram[v->cur_addr % SPU_RAM_SIZE];
    int shift = block[0] & 0x0F;
    int filter = (block[0] >> 4) & 0x07;
    unsigned char flags = block[1];

    if (filter > 4) filter = 4;

    int f0 = pos_adpcm_table[filter];
    int f1 = neg_adpcm_table[filter];

    for (int i = 0; i < 28; i++) {
        int nibble;
        int byte_idx = 2 + i / 2;
        if (i & 1)
            nibble = (block[byte_idx] >> 4) & 0x0F;
        else
            nibble = block[byte_idx] & 0x0F;

        /* Sign-extend nibble */
        if (nibble >= 8) nibble -= 16;

        int sample = nibble << (12 - shift);
        sample += (v->prev1 * f0 + v->prev2 * f1 + 32) >> 6;

        /* Clamp to 16-bit */
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;

        v->decoded[i] = (short)sample;
        v->prev2 = v->prev1;
        v->prev1 = sample;
    }

    /* Handle loop flags */
    if (flags & 4) { /* loop start */
        v->loop_addr = v->cur_addr;
    }
    if (flags & 1) { /* loop end */
        if (flags & 2) { /* loop repeat */
            v->cur_addr = v->loop_addr;
        } else {
            v->active = 0; /* stop */
        }
        return;
    }

    v->cur_addr += 16; /* advance to next block */
    if (v->cur_addr >= SPU_RAM_SIZE)
        v->cur_addr = 0;
}

/*---------------------------------------------------------------------------*/
/* ADSR envelope tick (simplified linear)                                     */
/*---------------------------------------------------------------------------*/
static void env_tick(SPU_Voice *v)
{
    switch (v->env_phase) {
    case ENV_ATTACK: {
        int rate = (v->ar == 0) ? 0x7FFF : (0x7FFF / (v->ar + 1));
        v->env_level += rate;
        if (v->env_level >= 0x7FFF) {
            v->env_level = 0x7FFF;
            v->env_phase = ENV_DECAY;
        }
        break;
    }
    case ENV_DECAY: {
        int sustain_level = (v->sl + 1) * 0x800;
        if (sustain_level > 0x7FFF) sustain_level = 0x7FFF;
        int rate = (v->dr == 0) ? 0x7FFF : (0x7FFF / (v->dr + 1));
        v->env_level -= rate;
        if (v->env_level <= sustain_level) {
            v->env_level = sustain_level;
            v->env_phase = ENV_SUSTAIN;
        }
        break;
    }
    case ENV_SUSTAIN:
        /* Sustain holds level; slow decay based on sr */
        if (v->sr > 0) {
            v->env_level -= v->sr;
            if (v->env_level < 0) v->env_level = 0;
        }
        break;
    case ENV_RELEASE: {
        int rate = (v->rr == 0) ? 0x7FFF : (0x7FFF / (v->rr + 1));
        v->env_level -= rate;
        if (v->env_level <= 0) {
            v->env_level = 0;
            v->env_phase = ENV_OFF;
            v->active = 0;
        }
        break;
    }
    default:
        break;
    }

    if (v->env_level < 0) v->env_level = 0;
    if (v->env_level > 0x7FFF) v->env_level = 0x7FFF;
}

/*---------------------------------------------------------------------------*/
/* SDL2 audio callback — mix all voices                                      */
/*---------------------------------------------------------------------------*/
static void spu_audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    short *out = (short *)stream;
    int num_samples = len / 4; /* stereo 16-bit = 4 bytes per sample */

    for (int s = 0; s < num_samples; s++) {
        int mix_l = 0, mix_r = 0;

        for (int ch = 0; ch < NUM_VOICES; ch++) {
            SPU_Voice *v = &voices[ch];
            if (!v->active || v->env_phase == ENV_OFF)
                continue;

            /* Get current sample via pitch interpolation */
            int idx = (v->frac_pos >> 16) % 28;
            short sample = v->decoded[idx];

            /* Apply envelope */
            int env = v->env_level;
            int s16 = (sample * env) >> 15;

            /* Apply voice volume and accumulate */
            mix_l += (s16 * v->vol_l) >> 14;
            mix_r += (s16 * v->vol_r) >> 14;

            /* Advance fractional position by pitch */
            v->frac_pos += v->pitch;

            /* When we cross a sample boundary, check if we need a new block */
            unsigned int new_idx = (v->frac_pos >> 16);
            if (new_idx >= 28) {
                v->frac_pos -= (28 << 16);
                decode_adpcm_block(v);
            }

            /* Tick envelope (at reduced rate — roughly per-sample is too fast) */
            /* Tick every 64 samples (~689 Hz at 44100) */
            if ((s & 63) == 0) {
                env_tick(v);
            }
        }

        /* Apply master volume */
        mix_l = (mix_l * master_vol_l) >> 14;
        mix_r = (mix_r * master_vol_r) >> 14;

        /* Clamp */
        if (mix_l > 32767) mix_l = 32767;
        if (mix_l < -32768) mix_l = -32768;
        if (mix_r > 32767) mix_r = 32767;
        if (mix_r < -32768) mix_r = -32768;

        out[s * 2 + 0] = (short)mix_l;
        out[s * 2 + 1] = (short)mix_r;
    }
}

/*---------------------------------------------------------------------------*/
/* SPU API implementations                                                   */
/*---------------------------------------------------------------------------*/

void spu_emu_init(void)
{
    memset(spu_ram, 0, SPU_RAM_SIZE);
    memset(voices, 0, sizeof(voices));

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = spu_audio_callback;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev > 0) {
        SDL_PauseAudioDevice(audio_dev, 0); /* start playback */
        printf("[spu] Audio device opened: %dHz %dch\n", have.freq, have.channels);
    } else {
        printf("[spu] Failed to open audio: %s\n", SDL_GetError());
    }
}

void spu_emu_shutdown(void)
{
    if (audio_dev > 0) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }
}

void SpuInit(void)
{
    spu_emu_init();
}

void SpuQuit(void)
{
    spu_emu_shutdown();
}

void SpuReset(void)
{
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);
    memset(voices, 0, sizeof(voices));
    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}

u_long SpuSetTransferStartAddr(u_long addr)
{
    spu_transfer_addr = addr;
    return addr;
}

u_long SpuSetTransferMode(long mode)
{
    (void)mode;
    return 0;
}

u_long SpuWrite(u_char *addr, u_long size)
{
    if (spu_transfer_addr + size <= SPU_RAM_SIZE) {
        memcpy(&spu_ram[spu_transfer_addr], addr, size);
    }
    spu_transfer_addr += size;
    return size;
}

u_long SpuRead(u_char *addr, u_long size)
{
    if (spu_transfer_addr + size <= SPU_RAM_SIZE) {
        memcpy(addr, &spu_ram[spu_transfer_addr], size);
    }
    return size;
}

long SpuIsTransferCompleted(long flag)
{
    (void)flag;
    return 1; /* always complete (instant transfer) */
}

void SpuSetVoiceAttr(SpuVoiceAttr *attr)
{
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);

    for (int ch = 0; ch < NUM_VOICES; ch++) {
        if (!(attr->voice & (1 << ch)))
            continue;

        SPU_Voice *v = &voices[ch];

        if (attr->mask & SPU_VOICE_VOLL)  v->vol_l = attr->volume.left;
        if (attr->mask & SPU_VOICE_VOLR)  v->vol_r = attr->volume.right;
        if (attr->mask & SPU_VOICE_PITCH) v->pitch = (unsigned short)attr->pitch;
        if (attr->mask & SPU_VOICE_WDSA)  v->addr = attr->addr;
        if (attr->mask & SPU_VOICE_LSAX)  v->loop_addr = attr->loop_addr;

        if (attr->mask & SPU_VOICE_ADSR_AMODE) v->a_mode = attr->a_mode;
        if (attr->mask & SPU_VOICE_ADSR_SMODE) v->s_mode = attr->s_mode;
        if (attr->mask & SPU_VOICE_ADSR_RMODE) v->r_mode = attr->r_mode;
        if (attr->mask & SPU_VOICE_ADSR_AR) v->ar = attr->ar;
        if (attr->mask & SPU_VOICE_ADSR_DR) v->dr = attr->dr;
        if (attr->mask & SPU_VOICE_ADSR_SR) v->sr = attr->sr;
        if (attr->mask & SPU_VOICE_ADSR_RR) v->rr = attr->rr;
        if (attr->mask & SPU_VOICE_ADSR_SL) v->sl = attr->sl;
    }

    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}

void SpuGetVoiceAttr(SpuVoiceAttr *attr)
{
    for (int ch = 0; ch < NUM_VOICES; ch++) {
        if (!(attr->voice & (1 << ch)))
            continue;

        SPU_Voice *v = &voices[ch];
        attr->volume.left = v->vol_l;
        attr->volume.right = v->vol_r;
        attr->pitch = v->pitch;
        attr->addr = v->addr;
        attr->loop_addr = v->loop_addr;
        attr->envx = (short)(v->env_level >> 8);
        break; /* only first matching voice */
    }
}

void SpuSetKey(long on_off, u_long voice_bit)
{
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);

    for (int ch = 0; ch < NUM_VOICES; ch++) {
        if (!(voice_bit & (1 << ch)))
            continue;

        SPU_Voice *v = &voices[ch];
        if (on_off == SPU_ON) {
            /* Key on — start playing from addr */
            v->active = 1;
            v->key_off = 0;
            v->cur_addr = v->addr;
            v->sample_idx = 0;
            v->frac_pos = 0;
            v->prev1 = 0;
            v->prev2 = 0;
            v->env_phase = ENV_ATTACK;
            v->env_level = 0;
            decode_adpcm_block(v);
        } else {
            /* Key off — enter release */
            v->key_off = 1;
            v->env_phase = ENV_RELEASE;
        }
    }

    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}

long SpuGetKeyStatus(u_long voice_bit)
{
    for (int ch = 0; ch < NUM_VOICES; ch++) {
        if (!(voice_bit & (1 << ch)))
            continue;
        SPU_Voice *v = &voices[ch];
        if (!v->active) return SPU_OFF_ENV_OFF;
        if (v->env_phase == ENV_RELEASE) return SPU_OFF_ENV_ON;
        return SPU_ON_ENV_ON;
    }
    return SPU_OFF_ENV_OFF;
}

void SpuSetCommonAttr(SpuCommonAttr *attr)
{
    if (attr->mask & SPU_COMMON_MVOLL) master_vol_l = attr->mvol.left;
    if (attr->mask & SPU_COMMON_MVOLR) master_vol_r = attr->mvol.right;
    /* CD volume/mix handled by cd.volume.left/right and cd.mix — no-op for port */
}

void SpuSetReverb(long on_off) { (void)on_off; }
long SpuSetReverbModeParam(SpuReverbAttr *attr) { (void)attr; return 0; }
void SpuSetReverbVoice(long on_off, u_long voice_bit) { (void)on_off; (void)voice_bit; }
long SpuReserveReverbWorkArea(long on_off) { (void)on_off; return 0; }
long SpuClearReverbWorkArea(long mode) { (void)mode; return 0; }
void SpuSetReverbDepth(SpuReverbAttr *attr) { (void)attr; }
void SpuSetPitchLFOVoice(long on_off, u_long voice_bit) { (void)on_off; (void)voice_bit; }
void SpuSetNoiseVoice(long on_off, u_long voice_bit) { (void)on_off; (void)voice_bit; }

void SpuSetIRQ(long on_off) { spu_irq_enabled = on_off; }

u_long SpuSetIRQAddr(u_long addr)
{
    spu_irq_addr = addr;
    return addr;
}

void SpuSetIRQCallback(SpuIRQCallbackProc func)
{
    spu_irq_callback = func;
}

void SpuGetAllKeysStatus(char *status)
{
    for (int i = 0; i < 24; i++) {
        if (voices[i].active)
            status[i] = (voices[i].env_phase == ENV_RELEASE) ? SPU_OFF_ENV_ON : SPU_ON_ENV_ON;
        else
            status[i] = SPU_OFF_ENV_OFF;
    }
}

long SpuInitMalloc(long num, char *top)
{
    (void)num;
    (void)top;
    spu_malloc_ptr = 0x1010;
    return 0;
}

long SpuMalloc(long size)
{
    long addr = spu_malloc_ptr;
    /* Align to 8 bytes */
    size = (size + 7) & ~7;
    spu_malloc_ptr += size;
    if (spu_malloc_ptr >= SPU_RAM_SIZE) {
        printf("[spu] SpuMalloc: out of SPU RAM (requested %ld at 0x%lx)\n", size, addr);
        return -1;
    }
    return addr;
}

void SpuFree(long addr)
{
    (void)addr; /* no-op for bump allocator */
}
