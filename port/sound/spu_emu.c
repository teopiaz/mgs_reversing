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
    int env_counter;           /* cycle counter for rate-based envelope */

    /* Previous decoded samples for Gaussian interpolation */
    short prev_decoded[3];     /* last 3 samples from previous blocks */
} SPU_Voice;

static SPU_Voice voices[NUM_VOICES];
static short master_vol_l = 0x3FFF;
static short master_vol_r = 0x3FFF;

/* --- Debug accessor used by the imgui overlay (Audio tab). ----------------
 * Copies a compact snapshot of the voice table into `out`. Returns the
 * number of voices written (min(NUM_VOICES, max)). The struct layout is a
 * plain-data copy to avoid exposing SPU_Voice internals outside this TU. */
typedef struct {
    int            active;
    int            key_off;
    short          vol_l, vol_r;
    unsigned short pitch;
    unsigned long  addr;
    int            env_phase;   /* 0..4 (ENV_OFF .. ENV_RELEASE) */
    int            env_level;   /* 0..0x7FFF */
} PortSpuVoiceInfo;

int port_spu_get_voices(PortSpuVoiceInfo *out, int max)
{
    int n = NUM_VOICES;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        SPU_Voice *v = &voices[i];
        out[i].active    = v->active;
        out[i].key_off   = v->key_off;
        out[i].vol_l     = v->vol_l;
        out[i].vol_r     = v->vol_r;
        out[i].pitch     = v->pitch;
        out[i].addr      = v->addr;
        out[i].env_phase = (int)v->env_phase;
        out[i].env_level = v->env_level;
    }
    return n;
}

/* SPU master volume accessor (signed 14-bit). */
void port_spu_get_master(short *l, short *r) { *l = master_vol_l; *r = master_vol_r; }

/* Global mute. When on, the SDL audio device stays open but we don't feed
 * samples -- it outputs silence. Non-destructive; unmute resumes mid-stream. */
static int port_spu_muted_flag = 0;
int  port_spu_is_muted(void) { return port_spu_muted_flag; }
void port_spu_set_muted(int on)
{
    port_spu_muted_flag = on ? 1 : 0;
    /* Actual muting is applied in the mixer (see master gain multiply below). */
}

/* Per-voice mute (bitmask, one bit per channel). Non-destructive: the game
 * keeps ticking the voice, we just skip its contribution in the mixer. */
static unsigned int port_spu_voice_mute_mask = 0;
int  port_spu_voice_is_muted(int voice)
{
    if (voice < 0 || voice >= NUM_VOICES) return 0;
    return (port_spu_voice_mute_mask >> voice) & 1u;
}
void port_spu_voice_set_mute(int voice, int on)
{
    if (voice < 0 || voice >= NUM_VOICES) return;
    if (on) port_spu_voice_mute_mask |=  (1u << voice);
    else    port_spu_voice_mute_mask &= ~(1u << voice);
}
void port_spu_voice_mute_all(int on)
{
    port_spu_voice_mute_mask = on ? 0x00FFFFFFu : 0u;
}
unsigned int port_spu_voice_mute_get_mask(void) { return port_spu_voice_mute_mask; }
/* port_spu_stream_info() is defined below, after the stream_* statics. */
int port_spu_stream_info(int *pcm_rd_out, int *pcm_wr_r_out, int *pcm_wr_l_out,
                         int *active_out, unsigned long *base_r_out, unsigned long *base_l_out);

/* IRQ */
static SpuIRQCallbackProc spu_irq_callback = NULL;
static unsigned long spu_irq_addr = 0;
static int spu_irq_enabled = 0;

/* Noise generator. PSX has a 16-bit LFSR clocked at a SPUCNT-controlled rate;
 * voices with noise mode set replace their per-sample ADPCM output with the
 * LFSR sign-bit sample. Used for codec radio static, gunfire crackle and
 * explosion shoulder-noise. Game doesn't write SPU_COMMON_NOISECLK, so we use
 * a fixed mid-rate (~172 Hz LFSR step ≈ broadband hiss). */
static unsigned int   spu_noise_voice_mask = 0;
static unsigned short spu_noise_lfsr       = 0x7FFFu;
static int            spu_noise_counter    = 0;
static int            spu_noise_period     = 256;  /* samples per LFSR step */
static short          spu_noise_sample     = 0;

/* SDL audio */
static SDL_AudioDeviceID audio_dev = 0;

/*---------------------------------------------------------------------------*/
/* Stream PCM bypass — decode ADPCM at SpuWrite time instead of in callback. */
/* Eliminates race condition between game thread (SpuWrite) and audio thread. */
/*---------------------------------------------------------------------------*/
#define STREAM_PCM_MAX (44100 * 60)  /* 60 seconds of mono samples */
static short stream_pcm_r[STREAM_PCM_MAX];
static short stream_pcm_l[STREAM_PCM_MAX];
static volatile int stream_pcm_wr_r = 0;  /* write cursor (game thread) */
static volatile int stream_pcm_wr_l = 0;
volatile int stream_pcm_rd = 0;           /* read cursor (audio thread) — extern'd by main_game.c */
static int stream_active = 0;
static unsigned long stream_base_r = 0;   /* SPU RAM addr of right buffer */
static unsigned long stream_base_l = 0;   /* SPU RAM addr of left buffer */
/* Per-channel ADPCM decoder state (maintained across SpuWrite calls) */
static int stream_prev1_r = 0, stream_prev2_r = 0;
static int stream_prev1_l = 0, stream_prev2_l = 0;

/* Imgui debug accessor (forward-declared earlier). */
int port_spu_stream_info(int *pcm_rd_out, int *pcm_wr_r_out, int *pcm_wr_l_out,
                         int *active_out, unsigned long *base_r_out, unsigned long *base_l_out)
{
    if (pcm_rd_out)   *pcm_rd_out   = stream_pcm_rd;
    if (pcm_wr_r_out) *pcm_wr_r_out = stream_pcm_wr_r;
    if (pcm_wr_l_out) *pcm_wr_l_out = stream_pcm_wr_l;
    if (active_out)   *active_out   = stream_active;
    if (base_r_out)   *base_r_out   = stream_base_r;
    if (base_l_out)   *base_l_out   = stream_base_l;
    return STREAM_PCM_MAX;
}

void spu_stream_set_buffers(unsigned long base_r, unsigned long base_l)
{
    stream_base_r = base_r;
    stream_base_l = base_l;
}

void stream_reset(void)
{
    stream_pcm_wr_r = 0;
    stream_pcm_wr_l = 0;
    stream_pcm_rd = 0;
    stream_prev1_r = stream_prev2_r = 0;
    stream_prev1_l = stream_prev2_l = 0;
    stream_active = 0;
}

/* Decode ADPCM data into the stream PCM ring buffer for one channel.
   Returns number of PCM samples written. */
static int stream_decode_adpcm(const unsigned char *data, int size,
                                short *pcm_buf, int *wr_ptr,
                                int *prev1, int *prev2)
{
    int written = 0;
    int wr = *wr_ptr;
    for (int off = 0; off + 16 <= size; off += 16) {
        const unsigned char *block = data + off;
        int shift = block[0] & 0x0F;
        int filter = (block[0] >> 4) & 0x07;
        if (filter > 4) filter = 4;

        int f0 = pos_adpcm_table[filter];
        int f1 = neg_adpcm_table[filter];

        for (int i = 0; i < 28; i++) {
            int nibble;
            int byte_idx = 2 + i / 2;
            if (i & 1) nibble = (block[byte_idx] >> 4) & 0x0F;
            else       nibble = block[byte_idx] & 0x0F;
            if (nibble >= 8) nibble -= 16;

            int sample = nibble << (12 - shift);
            sample += (*prev1 * f0 + *prev2 * f1 + 32) >> 6;
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;

            *prev2 = *prev1;
            *prev1 = sample;

            if (wr < STREAM_PCM_MAX) {
                pcm_buf[wr++] = (short)sample;
                written++;
            }
        }
    }
    *wr_ptr = wr;
    return written;
}

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

    /* Handle loop flags — must match real PSX SPU behavior.
       Flag 0x04: save current address as loop point.
       Flag 0x01 (end): ALWAYS jump to loop_addr.
         - With 0x02: normal loop, keep playing.
         - Without 0x02: enter envelope release, but still loop.
       The stream audio ping-pong buffer relies on end-without-repeat
       looping back to loop_addr (not stopping), because StrSpuTrans
       writes fresh data to the other half while the voice loops. */
    if (flags & 4) { /* loop start */
        v->loop_addr = v->cur_addr;
    }
    if (flags & 1) { /* loop end */
        v->cur_addr = v->loop_addr; /* always loop back */
        if (!(flags & 2)) {
            /* End without repeat: enter release phase (voice fades out
               but keeps producing samples from loop_addr). */
            if (!v->key_off) {
                v->key_off = 1;
                v->env_phase = ENV_RELEASE;
            }
        }
        return;
    }

    v->cur_addr += 16; /* advance to next block */
    if (v->cur_addr >= SPU_RAM_SIZE)
        v->cur_addr = 0;
}

/*---------------------------------------------------------------------------*/
/* ADSR envelope — based on pcsx-redux implementation                         */
/* Rate table from PSX SPU hardware: maps rate (0-127) to denominator,        */
/* increase numerator, and decrease numerator.                                */
/*---------------------------------------------------------------------------*/

/* Precomputed rate tables (from pcsx-redux adsr.cc) */
static int adsr_denom(int rate) {
    return (rate < 48) ? 1 : (1 << ((rate >> 2) - 11));
}
static int adsr_num_inc(int rate) {
    return (rate < 48) ? (7 - (rate & 3)) << (11 - (rate >> 2))
                       : (7 - (rate & 3));
}
static int adsr_num_dec(int rate) {
    return (rate < 48) ? (-8 + (rate & 3)) << (11 - (rate >> 2))
                       : (-8 + (rate & 3));
}

/* Called once per audio sample (44100Hz) — matches PSX SPU hardware */
static void env_tick(SPU_Voice *v)
{
    int rate, denom, step;

    switch (v->env_phase) {
    case ENV_ATTACK: {
        rate = v->ar & 0x7F;
        /* Exponential attack: slow down near peak */
        if ((v->a_mode == 5 || v->a_mode == 1) && v->env_level >= 0x6000)
            rate = (rate + 8 > 127) ? 127 : rate + 8;
        denom = adsr_denom(rate);
        v->env_counter++;
        if (v->env_counter >= denom) {
            v->env_counter = 0;
            v->env_level += adsr_num_inc(rate);
        }
        if (v->env_level >= 0x7FFF) {
            v->env_level = 0x7FFF;
            v->env_phase = ENV_DECAY;
            v->env_counter = 0;
        }
        break;
    }
    case ENV_DECAY: {
        rate = (v->dr & 0xF) * 4;
        denom = adsr_denom(rate);
        v->env_counter++;
        if (v->env_counter >= denom) {
            v->env_counter = 0;
            /* Always exponential decrease */
            step = (adsr_num_dec(rate) * v->env_level) >> 15;
            v->env_level += step;
        }
        int sustain_level = ((v->sl & 0xF) + 1) * 0x800;
        if (sustain_level > 0x7FFF) sustain_level = 0x7FFF;
        if (v->env_level <= sustain_level) {
            v->env_level = sustain_level;
            v->env_phase = ENV_SUSTAIN;
            v->env_counter = 0;
        }
        break;
    }
    case ENV_SUSTAIN: {
        rate = v->sr & 0x7F;
        denom = adsr_denom(rate);
        v->env_counter++;
        if (v->env_counter >= denom) {
            v->env_counter = 0;
            if (v->s_mode >= 4) {
                /* Decrease (modes 5=exp dec ↑, 7=exp dec ↓, etc.) */
                if (v->s_mode == 7 || v->s_mode == 5) {
                    /* Exponential decrease */
                    step = (adsr_num_dec(rate) * v->env_level) >> 15;
                } else {
                    /* Linear decrease */
                    step = adsr_num_dec(rate);
                }
            } else {
                /* Increase */
                if (v->a_mode == 5 && v->env_level >= 0x6000)
                    rate = (rate + 8 > 127) ? 127 : rate + 8;
                step = adsr_num_inc(rate);
            }
            v->env_level += step;
        }
        break;
    }
    case ENV_RELEASE: {
        rate = (v->rr & 0x1F) * 4;
        denom = adsr_denom(rate);
        v->env_counter++;
        if (v->env_counter >= denom) {
            v->env_counter = 0;
            if (v->r_mode == 7) {
                /* Exponential release */
                step = (adsr_num_dec(rate) * v->env_level) >> 15;
            } else {
                /* Linear release */
                step = adsr_num_dec(rate);
            }
            v->env_level += step;
        }
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
/* PSX Gaussian interpolation table (4-point, from SPU documentation)         */
/*---------------------------------------------------------------------------*/
static const short gauss_table[512] = {
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  3,  3,
    3,  4,  4,  5,  5,  6,  7,  7,  8,  9,  9, 10, 11, 12, 13, 14,
   15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 28, 29, 31, 33, 34, 36,
   38, 40, 42, 44, 46, 48, 51, 53, 55, 58, 60, 63, 65, 68, 71, 74,
   77, 80, 83, 86, 89, 93, 96, 99,103,106,110,114,117,121,125,129,
  133,137,141,146,150,154,159,163,168,172,177,182,186,191,196,201,
  206,211,217,222,227,233,238,244,249,255,260,266,272,278,284,290,
  296,302,309,315,321,328,334,341,347,354,361,367,374,381,388,395,
  402,410,417,424,431,439,446,454,461,469,477,484,492,500,508,516,
  524,532,540,549,557,565,574,582,591,599,608,617,625,634,643,652,
  661,670,679,688,697,706,716,725,734,744,753,763,772,782,791,801,
  811,820,830,840,850,860,869,879,889,899,909,919,929,939,949,959,
  969,979,989,999,1009,1020,1030,1040,1050,1060,1070,1080,1090,1101,1111,1121,
 1131,1141,1151,1162,1172,1182,1192,1202,1212,1222,1232,1242,1252,1262,1272,1282,
 1292,1302,1312,1322,1331,1341,1351,1361,1370,1380,1389,1399,1409,1418,1428,1437,
 1446,1456,1465,1474,1483,1492,1501,1510,1519,1528,1537,1546,1554,1563,1571,1580,
 1588,1596,1605,1613,1621,1629,1636,1644,1652,1660,1667,1675,1682,1689,1697,1704,
 1711,1718,1724,1731,1738,1744,1751,1757,1763,1769,1775,1781,1787,1793,1798,1804,
 1809,1814,1819,1824,1829,1834,1839,1843,1848,1852,1856,1860,1864,1868,1872,1875,
 1879,1882,1885,1888,1891,1894,1897,1899,1902,1904,1906,1908,1910,1912,1913,1915,
 1916,1918,1919,1920,1921,1922,1922,1923,1923,1924,1924,1924,1924,1924,1924,1923,
 1923,1922,1922,1921,1920,1919,1918,1916,1915,1913,1912,1910,1908,1906,1904,1902,
 1899,1897,1894,1891,1888,1885,1882,1879,1875,1872,1868,1864,1860,1856,1852,1848,
 1843,1839,1834,1829,1824,1819,1814,1809,1804,1798,1793,1787,1781,1775,1769,1763,
 1757,1751,1744,1738,1731,1724,1718,1711,1704,1697,1689,1682,1675,1667,1660,1652,
 1644,1636,1629,1621,1613,1605,1596,1588,1580,1571,1563,1554,1546,1537,1528,1519,
 1510,1501,1492,1483,1474,1465,1456,1446,1437,1428,1418,1409,1399,1389,1380,1370,
 1361,1351,1341,1331,1322,1312,1302,1292,1282,1272,1262,1252,1242,1232,1222,1212,
 1202,1192,1182,1172,1162,1151,1141,1131,1121,1111,1101,1090,1080,1070,1060,1050,
 1040,1030,1020,1009, 999, 989, 979, 969, 959, 949, 939, 929, 919, 909, 899, 889,
  879, 869, 860, 850, 840, 830, 820, 811, 801, 791, 782, 772, 763, 753, 744, 734,
};

/* 4-point Gaussian interpolation (matches PSX SPU) */
static short gauss_interpolate(short s0, short s1, short s2, short s3, int frac)
{
    /* frac is 0..0xFFFF, use top 8 bits as table index */
    int i = (frac >> 8) & 0xFF;
    int out = (gauss_table[0x0FF - i] * s0) >> 11;
    out   += (gauss_table[0x1FF - i] * s1) >> 11;
    out   += (gauss_table[0x100 + i] * s2) >> 11;
    out   += (gauss_table[0x000 + i] * s3) >> 11;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return (short)out;
}

/* PSX SPU noise LFSR. Stepped once per output sample at a 1/period rate
 * derived from SPUCNT noise frequency bits (here a fixed mid-rate). Output
 * is the LFSR's high bit mapped to {-32768, +32767} so a noise-mode voice
 * sounds like full-amplitude broadband hiss. Reference: pcsx-redux
 * SPU.cc::do_noise() (16-bit Galois LFSR, taps at bits 0/1/2/3/5). */
static void noise_step(void)
{
    if (++spu_noise_counter < spu_noise_period) return;
    spu_noise_counter = 0;
    unsigned int lfsr = spu_noise_lfsr;
    unsigned int bit = ((lfsr >> 0) ^ (lfsr >> 1) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
    spu_noise_lfsr = (unsigned short)((lfsr >> 1) | (bit << 14));
    spu_noise_sample = (spu_noise_lfsr & 1u) ? 32767 : -32768;
}

/*---------------------------------------------------------------------------*/
/* Reverb -- Freeverb-style replacement                                      */
/*                                                                           */
/* PSX SPU implements a 22-register reverb running over a SPU-RAM delay      */
/* buffer; we substitute a simpler Freeverb DSP (4 lowpass-feedback combs +  */
/* 2 all-pass per stereo channel). The preset (STUDIO_C, HALL, ...) sets    */
/* room-size and damping. `port_reverb_send` is a per-voice bit mask written */
/* by SpuSetReverbVoice; only those voices' sample contribute to rev_in.    */
/* PORT_REVERB=0 forces the master gate off for bisect.                     */
/*---------------------------------------------------------------------------*/

#define FV_NUM_COMBS     4
#define FV_NUM_APS       2
#define FV_STEREO_SPREAD 23
static const int fv_comb_len[FV_NUM_COMBS] = { 1116, 1188, 1277, 1356 };
static const int fv_ap_len  [FV_NUM_APS  ] = {  225,  556 };
#define FV_COMB_BUF_MAX (1356 + FV_STEREO_SPREAD)
#define FV_AP_BUF_MAX   ( 556 + FV_STEREO_SPREAD)

static short fv_comb_buf_l[FV_NUM_COMBS][FV_COMB_BUF_MAX];
static short fv_comb_buf_r[FV_NUM_COMBS][FV_COMB_BUF_MAX];
static int   fv_comb_idx_l[FV_NUM_COMBS];
static int   fv_comb_idx_r[FV_NUM_COMBS];
static int   fv_comb_filt_l[FV_NUM_COMBS]; /* damping LP state */
static int   fv_comb_filt_r[FV_NUM_COMBS];

static short fv_ap_buf_l[FV_NUM_APS][FV_AP_BUF_MAX];
static short fv_ap_buf_r[FV_NUM_APS][FV_AP_BUF_MAX];
static int   fv_ap_idx_l[FV_NUM_APS];
static int   fv_ap_idx_r[FV_NUM_APS];

/* Q15 parameters (preset-driven; defaults match STUDIO_C used by sd_init). */
static int  fv_room_size_q15 = 28000;  /* ~0.85 */
static int  fv_damping_q15   =  6000;  /* ~0.18 */
static int  fv_wet_q15       = 12000;  /* ~0.37 */
static int  fv_input_gain    =  6000;  /* small input scaling to avoid clipping */

static unsigned int port_reverb_send = 0;
static int          port_reverb_on   = 1;     /* SpuSetReverb master */
static int          port_reverb_env_off = 0;  /* PORT_REVERB=0 bisect override */

static inline int clamp_s16(int x) {
    if (x > 32767)  return 32767;
    if (x < -32768) return -32768;
    return x;
}

/* Apply Freeverb to one stereo input sample and return wet output. */
static void freeverb_step(int in_l, int in_r, int *out_l, int *out_r)
{
    int comb_l = 0, comb_r = 0;
    int sin_l = (in_l * fv_input_gain) >> 15;
    int sin_r = (in_r * fv_input_gain) >> 15;

    for (int c = 0; c < FV_NUM_COMBS; c++) {
        /* Left */
        int len_l = fv_comb_len[c];
        short d_l = fv_comb_buf_l[c][fv_comb_idx_l[c]];
        comb_l += d_l;
        fv_comb_filt_l[c] = (d_l * (0x8000 - fv_damping_q15) + fv_comb_filt_l[c] * fv_damping_q15) >> 15;
        int new_l = sin_l + ((fv_comb_filt_l[c] * fv_room_size_q15) >> 15);
        fv_comb_buf_l[c][fv_comb_idx_l[c]] = (short)clamp_s16(new_l);
        fv_comb_idx_l[c]++;
        if (fv_comb_idx_l[c] >= len_l) fv_comb_idx_l[c] = 0;

        /* Right (stereo-spread delay) */
        int len_r = fv_comb_len[c] + FV_STEREO_SPREAD;
        short d_r = fv_comb_buf_r[c][fv_comb_idx_r[c]];
        comb_r += d_r;
        fv_comb_filt_r[c] = (d_r * (0x8000 - fv_damping_q15) + fv_comb_filt_r[c] * fv_damping_q15) >> 15;
        int new_r = sin_r + ((fv_comb_filt_r[c] * fv_room_size_q15) >> 15);
        fv_comb_buf_r[c][fv_comb_idx_r[c]] = (short)clamp_s16(new_r);
        fv_comb_idx_r[c]++;
        if (fv_comb_idx_r[c] >= len_r) fv_comb_idx_r[c] = 0;
    }

    /* All-pass series (feedback 0.5) */
    int ap_l = comb_l, ap_r = comb_r;
    for (int a = 0; a < FV_NUM_APS; a++) {
        int len_l = fv_ap_len[a];
        short d_l = fv_ap_buf_l[a][fv_ap_idx_l[a]];
        int o_l = -ap_l + d_l;
        fv_ap_buf_l[a][fv_ap_idx_l[a]] = (short)clamp_s16(ap_l + (d_l >> 1));
        fv_ap_idx_l[a]++;
        if (fv_ap_idx_l[a] >= len_l) fv_ap_idx_l[a] = 0;
        ap_l = o_l;

        int len_r = fv_ap_len[a] + FV_STEREO_SPREAD;
        short d_r = fv_ap_buf_r[a][fv_ap_idx_r[a]];
        int o_r = -ap_r + d_r;
        fv_ap_buf_r[a][fv_ap_idx_r[a]] = (short)clamp_s16(ap_r + (d_r >> 1));
        fv_ap_idx_r[a]++;
        if (fv_ap_idx_r[a] >= len_r) fv_ap_idx_r[a] = 0;
        ap_r = o_r;
    }
    *out_l = ap_l;
    *out_r = ap_r;
}

/*---------------------------------------------------------------------------*/
/* SDL2 audio callback — mix all voices                                      */
/*---------------------------------------------------------------------------*/
static void spu_audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    short *out = (short *)stream;
    int num_samples = len / 4; /* stereo 16-bit = 4 bytes per sample */

    int reverb_active = port_reverb_on && !port_reverb_env_off && port_reverb_send != 0;
    for (int s = 0; s < num_samples; s++) {
        int mix_l = 0, mix_r = 0;
        int rev_in_l = 0, rev_in_r = 0;

        /* Global noise LFSR tick. Voices in noise mode all read the same
         * `spu_noise_sample` this sample, matching PSX hardware. */
        noise_step();

        /* Stream audio bypass: read pre-decoded PCM for voices 21-22.
           Don't gate on v->active — the SPU voice may hit ADPCM end flags
           in the ping-pong buffer and enter release/inactive state, but the
           stream should keep playing. Volume comes from voice attributes
           set by StrFadeInt. */
        if (stream_active) {
            int rd = stream_pcm_rd;
            if (rd < stream_pcm_wr_r || rd < stream_pcm_wr_l) {
                SPU_Voice *vr = &voices[21]; /* SPU_21CH = right */
                SPU_Voice *vl = &voices[22]; /* SPU_22CH = left */
                short sam_r = (rd < stream_pcm_wr_r) ? stream_pcm_r[rd] : 0;
                short sam_l = (rd < stream_pcm_wr_l) ? stream_pcm_l[rd] : 0;

                /* Apply voice volumes (set by StrFadeInt). Honor the
                   per-voice mute bits so the ImGui mute toggles silence
                   the stream channels too. */
                int mute21 = (port_spu_voice_mute_mask >> 21) & 1u;
                int mute22 = (port_spu_voice_mute_mask >> 22) & 1u;
                if (!mute21) {
                    mix_l += ((int)sam_r * vr->vol_l) >> 15;
                    mix_r += ((int)sam_r * vr->vol_r) >> 15;
                }
                if (!mute22) {
                    mix_l += ((int)sam_l * vl->vol_l) >> 15;
                    mix_r += ((int)sam_l * vl->vol_r) >> 15;
                }

                /* Advance read cursor — PSX SPU pitch is 4.12 fixed-point.
                   Pitch 0x1000 = 1.0 = 44100Hz (one decoded sample per output sample). */
                vr->frac_pos += vr->pitch;
                if ((vr->frac_pos >> 12) >= 1) {
                    int adv = vr->frac_pos >> 12;
                    vr->frac_pos -= adv << 12;
                    stream_pcm_rd = rd + adv;
                }
            }
        }

        for (int ch = 0; ch < NUM_VOICES; ch++) {
            SPU_Voice *v = &voices[ch];
            if (!v->active || v->env_phase == ENV_OFF)
                continue;

            /* Skip stream voices — handled above via pre-decoded PCM */
            if (stream_active && (ch == 21 || ch == 22))
                continue;

            /* Debug per-voice mute: skip this channel's mix contribution.
               The voice keeps ticking (envelope, addr advance) so the
               game's state is unaffected -- we only silence it. */
            if ((port_spu_voice_mute_mask >> ch) & 1u)
                continue;

            /* PSX SPU pitch is 4.12 fixed-point: 0x1000 = 44100Hz.
               frac_pos bits 12+ = sample index (0-27), bits 0-11 = fraction.
               Gaussian interpolation uses bits 4-11 as table index. */
            int idx = (v->frac_pos >> 12) % 28;
            int gauss_idx = (v->frac_pos >> 4) & 0xFF;
            short sample;
            if (spu_noise_voice_mask & (1u << ch)) {
                /* Voice in noise mode -- substitute LFSR sample for ADPCM. */
                sample = spu_noise_sample;
            } else {
                short s0 = (idx >= 3) ? v->decoded[idx - 3] : v->prev_decoded[idx];
                short s1 = (idx >= 2) ? v->decoded[idx - 2] : v->prev_decoded[idx + 1 > 2 ? 2 : idx + 1];
                short s2 = (idx >= 1) ? v->decoded[idx - 1] : v->prev_decoded[2];
                short s3 = v->decoded[idx];
                sample = gauss_interpolate(s0, s1, s2, s3, gauss_idx << 8);
            }

            /* Apply envelope (0..0x7FFF) */
            int s16 = (sample * v->env_level) >> 15;

            /* Apply voice volume (signed 16-bit, represents N/0x8000) */
            int contrib_l = (s16 * v->vol_l) >> 15;
            int contrib_r = (s16 * v->vol_r) >> 15;
            mix_l += contrib_l;
            mix_r += contrib_r;

            /* Reverb send: voices flagged via SpuSetReverbVoice feed the
             * Freeverb input. The wet output is added to mix below. */
            if (reverb_active && (port_reverb_send & (1u << ch))) {
                rev_in_l += contrib_l;
                rev_in_r += contrib_r;
            }

            /* Advance pitch counter */
            v->frac_pos += v->pitch;

            /* Decode next ADPCM block when crossing 28-sample boundary */
            if ((v->frac_pos >> 12) >= 28) {
                v->frac_pos -= (28 << 12);
                v->prev_decoded[0] = v->decoded[25];
                v->prev_decoded[1] = v->decoded[26];
                v->prev_decoded[2] = v->decoded[27];
                decode_adpcm_block(v);
            }

            /* Tick envelope once per sample (44100Hz, matches PSX SPU) */
            env_tick(v);
        }

        /* Freeverb wet pass. The send is tapped post-voice-volume so room
         * tone follows the dry mix proportionally. */
        if (reverb_active) {
            int wet_l = 0, wet_r = 0;
            freeverb_step(rev_in_l, rev_in_r, &wet_l, &wet_r);
            mix_l += (wet_l * fv_wet_q15) >> 15;
            mix_r += (wet_r * fv_wet_q15) >> 15;
        }

        /* Apply master volume (or silence if the debug overlay muted us). */
        if (port_spu_muted_flag) {
            mix_l = 0;
            mix_r = 0;
        } else {
            mix_l = (mix_l * master_vol_l) >> 14;
            mix_r = (mix_r * master_vol_r) >> 14;
        }

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
    /* Prevent double-init */
    if (audio_dev > 0) return;

    memset(spu_ram, 0, SPU_RAM_SIZE);
    memset(voices, 0, sizeof(voices));

    /* PORT_REVERB=0 forces reverb off (bisect aid). Any other value leaves
     * it gated by SpuSetReverb (default-on after sd_init). */
    const char *rev_env = getenv("PORT_REVERB");
    if (rev_env && *rev_env == '0') {
        port_reverb_env_off = 1;
        printf("[spu] reverb disabled via PORT_REVERB=0\n");
    }

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

    /* Decode ADPCM→PCM for stream buffer writes (voices 21-22).
       This pre-decodes audio at write time so the audio callback can
       read clean PCM without racing with the game thread. */
    if (stream_base_r && stream_base_l) {
        unsigned long dst = spu_transfer_addr;
        if (dst >= stream_base_r && dst < stream_base_r + 0x2000) {
            stream_decode_adpcm(addr, (int)size,
                stream_pcm_r, &stream_pcm_wr_r,
                &stream_prev1_r, &stream_prev2_r);
        } else if (dst >= stream_base_l && dst < stream_base_l + 0x2000) {
            stream_decode_adpcm(addr, (int)size,
                stream_pcm_l, &stream_pcm_wr_l,
                &stream_prev1_l, &stream_prev2_l);
        }
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

    /* Detect stream voice key-on/off (SPU_21CH | SPU_22CH) */
    int stream_keyoff = 0;
    if ((voice_bit & ((1 << 21) | (1 << 22))) && stream_base_r) {
        if (on_off == SPU_ON) {
            stream_pcm_rd = 0;
            stream_active = 1;
            printf("[spu] Stream key-on: wr_r=%d wr_l=%d\n",
                   stream_pcm_wr_r, stream_pcm_wr_l);
        } else {
            stream_active = 0;
            stream_keyoff = 1;
        }
    }

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
            v->env_counter = 0;
            v->prev_decoded[0] = 0;
            v->prev_decoded[1] = 0;
            v->prev_decoded[2] = 0;
            decode_adpcm_block(v);
        } else if (stream_keyoff && (ch == 21 || ch == 22)) {
            /* Stream voices don't have a real ADSR ramp -- their volume is
             * controlled by StrFadeInt. Letting them go through ENV_RELEASE
             * makes the per-voice ADPCM-gauss path play out stale v->decoded
             * bytes (= dirty-buffer glitch at codec line end). Kill cleanly. */
            v->active = 0;
            v->key_off = 0;
            v->env_phase = ENV_OFF;
            v->env_level = 0;
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

/* Master gate (called once from sd_init with SPU_ON; PORT_REVERB=0 forces off). */
void SpuSetReverb(long on_off)
{
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);
    port_reverb_on = (on_off == SPU_ON) ? 1 : 0;
    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}

/* Preset mapping. The game's sd_init installs STUDIO_C; per-stage env_snd
 * actors may switch modes via this same call. We map each PSX preset to a
 * {room_size, damping, wet} triple that approximates the original tail. */
long SpuSetReverbModeParam(SpuReverbAttr *attr)
{
    if (!attr) return 0;
    /* SPU_REV_MODE_* values from libspu.h. Tuned by ear: shorter rooms get
     * smaller room_size + larger damping; halls reverse. */
    int rs = 28000, dp = 6000, wet = 12000;
    switch ((int)attr->mode & 0xFF) {
    case 0:  rs = 0;     dp = 0;     wet = 0;     break; /* OFF */
    case 1:  rs = 22000; dp = 10000; wet =  8000; break; /* ROOM */
    case 2:  rs = 25000; dp =  8000; wet = 10000; break; /* STUDIO_A */
    case 3:  rs = 27000; dp =  6000; wet = 11000; break; /* STUDIO_B */
    case 4:  rs = 28000; dp =  6000; wet = 12000; break; /* STUDIO_C (default) */
    case 5:  rs = 30000; dp =  4000; wet = 14000; break; /* HALL */
    case 6:  rs = 29000; dp =  4500; wet = 13000; break; /* SPACE */
    case 7:  rs = 31000; dp =  3000; wet = 15000; break; /* ECHO */
    case 8:  rs = 26000; dp =  9000; wet = 10000; break; /* DELAY */
    case 9:  rs = 27500; dp =  6000; wet = 11000; break; /* PIPE */
    default: break;
    }
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);
    fv_room_size_q15 = rs;
    fv_damping_q15   = dp;
    fv_wet_q15       = wet;
    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
    return 0;
}

/* Enable reverb send for the voices in voice_bit. */
void SpuSetReverbVoice(long on_off, u_long voice_bit)
{
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);
    if (on_off == SPU_ON) port_reverb_send |=  (unsigned int)voice_bit;
    else                  port_reverb_send &= ~(unsigned int)voice_bit;
    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}

long SpuReserveReverbWorkArea(long on_off) { (void)on_off; return 0; }
long SpuClearReverbWorkArea(long mode) { (void)mode; return 0; }

/* Per-preset wet/dry depth. The game uses the depth.left/right field as a
 * 0..0x7FFF gain. We average L+R into our single wet scalar. */
void SpuSetReverbDepth(SpuReverbAttr *attr)
{
    if (!attr) return;
    int d = ((int)attr->depth.left + (int)attr->depth.right) / 2;
    if (d < 0)     d = 0;
    if (d > 32767) d = 32767;
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);
    fv_wet_q15 = d / 2;  /* gentle ceiling so saturated depth doesn't drown the dry mix */
    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}
void SpuSetPitchLFOVoice(long on_off, u_long voice_bit) { (void)on_off; (void)voice_bit; }

/* Enable noise mode for the voices selected by voice_bit (one bit per
 * channel). PSX behavior: noise-mode voices replace ADPCM output with the
 * shared SPU noise LFSR. on_off must be SPU_ON or SPU_OFF (1/0). */
void SpuSetNoiseVoice(long on_off, u_long voice_bit)
{
    if (audio_dev > 0) SDL_LockAudioDevice(audio_dev);
    if (on_off == SPU_ON) spu_noise_voice_mask |=  (unsigned int)voice_bit;
    else                  spu_noise_voice_mask &= ~(unsigned int)voice_bit;
    if (audio_dev > 0) SDL_UnlockAudioDevice(audio_dev);
}

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

/* Return current decode address for a voice (0-23). Used by stream code to
   derive the actual playback position in the ping-pong buffer. */
unsigned long spu_get_voice_cur_addr(int ch)
{
    if (ch < 0 || ch >= NUM_VOICES) return 0;
    return voices[ch].cur_addr;
}
