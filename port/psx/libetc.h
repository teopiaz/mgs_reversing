#ifndef __PSX_LIBETC_H__
#define __PSX_LIBETC_H__

/* PAD button constants */
#define PADLup      0x1000
#define PADLdown    0x4000
#define PADLleft    0x8000
#define PADLright   0x2000
#define PADRup      0x0010
#define PADRdown    0x0040
#define PADRleft    0x0080
#define PADRright   0x0020
#define PADL1       0x0004
#define PADL2       0x0001
#define PADR1       0x0008
#define PADR2       0x0002
#define PADstart    0x0800
#define PADselect   0x0100
#define PADi        PADRup
#define PADj        PADRright
#define PADk        PADRdown
#define PADl        PADRleft
#define PADm        PADL2
#define PADn        PADR2
#define PADo        PADL1
#define PADp        PADR1

int  VSync(int mode);
void ResetCallback(void);
void StopCallback(void);
void RestartCallback(void);
int  CheckCallback(void);
void VSyncCallback(void (*func)(void));
long PadInit(int mode);

#endif /* __PSX_LIBETC_H__ */
