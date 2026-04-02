#ifndef __PSX_LIBSPU_H__
#define __PSX_LIBSPU_H__

#include <sys/types.h>

/* SPU voice attribute masks */
#define SPU_VOICE_VOLL      (0x0001 <<  0)
#define SPU_VOICE_VOLR      (0x0001 <<  1)
#define SPU_VOICE_PITCH     (0x0001 <<  2)
#define SPU_VOICE_NOTE      (0x0001 <<  3)
#define SPU_VOICE_WDSA      (0x0001 <<  4)
#define SPU_VOICE_ADSR_AMODE (0x0001 <<  5)
#define SPU_VOICE_ADSR_SMODE (0x0001 <<  6)
#define SPU_VOICE_ADSR_RMODE (0x0001 <<  7)
#define SPU_VOICE_ADSR_AR   (0x0001 <<  8)
#define SPU_VOICE_ADSR_DR   (0x0001 <<  9)
#define SPU_VOICE_ADSR_SR   (0x0001 << 10)
#define SPU_VOICE_ADSR_RR   (0x0001 << 11)
#define SPU_VOICE_ADSR_SL   (0x0001 << 12)
#define SPU_VOICE_DIRECT    (0x0001 << 13)
#define SPU_VOICE_ADSR1     (0x0001 << 14)
#define SPU_VOICE_ADSR2     (0x0001 << 15)
#define SPU_VOICE_LSAX      (0x0001 << 16)

/* SPU on/off */
#define SPU_ON              1
#define SPU_OFF             0
#define SPU_RESET           2

/* SPU transfer modes */
#define SPU_TRANSFER_BY_DMA 0
#define SPU_TRANSFER_BY_IO  1

/* SPU transfer completion check modes */
#define SPU_TRANSFER_PEEK   0
#define SPU_TRANSFER_WAIT   1

/* SPU voice channel bits */
#define SPU_0CH     (1 <<  0)
#define SPU_1CH     (1 <<  1)
#define SPU_2CH     (1 <<  2)
#define SPU_3CH     (1 <<  3)
#define SPU_4CH     (1 <<  4)
#define SPU_5CH     (1 <<  5)
#define SPU_6CH     (1 <<  6)
#define SPU_7CH     (1 <<  7)
#define SPU_8CH     (1 <<  8)
#define SPU_9CH     (1 <<  9)
#define SPU_10CH    (1 << 10)
#define SPU_11CH    (1 << 11)
#define SPU_12CH    (1 << 12)
#define SPU_13CH    (1 << 13)
#define SPU_14CH    (1 << 14)
#define SPU_15CH    (1 << 15)
#define SPU_16CH    (1 << 16)
#define SPU_17CH    (1 << 17)
#define SPU_18CH    (1 << 18)
#define SPU_19CH    (1 << 19)
#define SPU_20CH    (1 << 20)
#define SPU_21CH    (1 << 21)
#define SPU_22CH    (1 << 22)
#define SPU_23CH    (1 << 23)
#define SPU_ALLCH   0x00FFFFFF

/* SPU reverb modes */
#define SPU_REV_MODE_OFF    0
#define SPU_REV_MODE_ROOM   1
#define SPU_REV_MODE_STUDIO_A 2
#define SPU_REV_MODE_STUDIO_B 3
#define SPU_REV_MODE_STUDIO_C 4
#define SPU_REV_MODE_HALL   5
#define SPU_REV_MODE_SPACE  6
#define SPU_REV_MODE_ECHO   7
#define SPU_REV_MODE_DELAY  8
#define SPU_REV_MODE_PIPE   9
#define SPU_REV_MODE_MAX    10

/* SPU ADSR modes */
#define SPU_VOICE_LINEARIncN  0
#define SPU_VOICE_LINEARIncR  1
#define SPU_VOICE_LINEARDecN  2
#define SPU_VOICE_EXPIncN     3
#define SPU_VOICE_EXPIncR     4
#define SPU_VOICE_EXPDec      5

/* SPU common attribute masks */
#define SPU_COMMON_MVOLL    (0x0001 <<  0)
#define SPU_COMMON_MVOLR    (0x0001 <<  1)
#define SPU_COMMON_CDVOLL   (0x0001 <<  2)
#define SPU_COMMON_CDVOLR   (0x0001 <<  3)
#define SPU_COMMON_CDREV    (0x0001 <<  4)
#define SPU_COMMON_CDMIX    (0x0001 <<  5)

/* SPU reverb attribute masks */
#define SPU_REV_MODE        (0x0001 << 0)
#define SPU_REV_DEPTHL      (0x0001 << 1)
#define SPU_REV_DEPTHR      (0x0001 << 2)

/* Key on/off flags */
#define SPU_KEYON           0
#define SPU_KEYOFF          1

/* Key status (matches PSX SDK — SPU_ON/SPU_OFF are 1/0 for key control,
   but key STATUS uses different values) */
#define SPU_OFF_ENV_OFF     0  /* same as SPU_OFF */
#define SPU_ON_ENV_OFF      SPU_ON  /* same as SPU_ON — intentional duplicate */
#define SPU_OFF_ENV_ON      2
#define SPU_ON_ENV_ON       3

/* Voice volume */
typedef struct {
    short left;
    short right;
} SpuVolume;

/* Voice attributes */
typedef struct {
    u_long      voice;
    u_long      mask;
    SpuVolume   volume;
    long        pitch;
    long        note;
    short       sample_note;
    short       envx;
    u_long      addr;
    u_long      loop_addr;
    long        a_mode;
    long        s_mode;
    long        r_mode;
    u_short     ar;
    u_short     dr;
    u_short     sr;
    u_short     rr;
    u_short     sl;
    u_short     adsr1;
    u_short     adsr2;
} SpuVoiceAttr;

/* Extended volume with mix control */
typedef struct {
    SpuVolume  volume;
    long       reverb;
    long       mix;
} SpuExtVolume;

/* Common attributes */
typedef struct {
    u_long       mask;
    SpuVolume    mvol;
    SpuExtVolume cd;
    SpuExtVolume ext;
} SpuCommonAttr;

/* Reverb attributes */
typedef struct {
    u_long     mask;
    long       mode;
    SpuVolume  depth;
    long       delay;
    long       feedback;
} SpuReverbAttr;

/* IRQ callback type */
typedef void (*SpuIRQCallbackProc)(void);

/* Functions */
void SpuInit(void);
void SpuQuit(void);
void SpuReset(void);
void SpuSetVoiceAttr(SpuVoiceAttr *attr);
void SpuGetVoiceAttr(SpuVoiceAttr *attr);
u_long SpuWrite(u_char *addr, u_long size);
u_long SpuRead(u_char *addr, u_long size);
long SpuIsTransferCompleted(long flag);
u_long SpuSetTransferMode(long mode);
u_long SpuSetTransferStartAddr(u_long addr);
void SpuSetKey(long on_off, u_long voice_bit);
long SpuGetKeyStatus(u_long voice_bit);
void SpuSetCommonAttr(SpuCommonAttr *attr);
void SpuSetReverb(long on_off);
long SpuSetReverbModeParam(SpuReverbAttr *attr);
void SpuSetReverbVoice(long on_off, u_long voice_bit);
long SpuReserveReverbWorkArea(long on_off);
long SpuClearReverbWorkArea(long mode);
void SpuSetReverbDepth(SpuReverbAttr *attr);
void SpuSetPitchLFOVoice(long on_off, u_long voice_bit);
void SpuSetNoiseVoice(long on_off, u_long voice_bit);
void SpuSetIRQ(long on_off);
u_long SpuSetIRQAddr(u_long addr);
void SpuSetIRQCallback(SpuIRQCallbackProc func);
void SpuGetAllKeysStatus(char *status);
long SpuInitMalloc(long num, char *top);
long SpuMalloc(long size);
void SpuFree(long addr);

#endif /* __PSX_LIBSPU_H__ */
