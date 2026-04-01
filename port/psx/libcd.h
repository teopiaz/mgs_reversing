#ifndef __PSX_LIBCD_H__
#define __PSX_LIBCD_H__

#include <sys/types.h>
#include <libgte.h>  /* for RECT */

/* CD-ROM location */
typedef struct {
    u_char minute;
    u_char second;
    u_char sector;
    u_char track;
} CdlLOC;

typedef struct {
    CdlLOC pos;
    u_char mode;
    u_char file;
    u_char chan;
    u_char pad;
} CdlFILTER;

typedef struct {
    u_char status;
    u_char pad[3];
} CdlATTR;

typedef void (*CdlCB)(u_char, u_char *);

/* CD commands */
#define CdlNop          0x01
#define CdlSetloc       0x02
#define CdlPlay         0x03
#define CdlForward      0x04
#define CdlBackward     0x05
#define CdlReadN        0x06
#define CdlStandby      0x07
#define CdlStop         0x08
#define CdlPause        0x09
#define CdlMute         0x0B
#define CdlDemute       0x0C
#define CdlSetfilter    0x0D
#define CdlSetmode      0x0E
#define CdlGetparam     0x0F
#define CdlGetlocL      0x10
#define CdlGetlocP      0x11
#define CdlReadT        0x12
#define CdlGetTN        0x13
#define CdlGetTD        0x14
#define CdlSeekL        0x15
#define CdlSeekP        0x16
#define CdlReadS        0x1B

/* CD mode flags */
#define CdlModeSpeed    0x80
#define CdlModeSize1    0x20
#define CdlModeSize0    0x10
#define CdlModeSF       0x08
#define CdlModeRT       0x04
#define CdlModeStream2  0x04
#define CdlModeStream   0x02
#define CdlModeAP       0x02

/* CD interrupt types */
#define CdlDATAREADY    0x01
#define CdlComplete     0x02
#define CdlAcknowledge  0x03
#define CdlDataEnd      0x04
#define CdlDiskError    0x05

/* Functions */
int    CdInit(void);
int    CdControl(u_char com, u_char *param, u_char *result);
int    CdControlB(u_char com, u_char *param, u_char *result);
int    CdControlF(u_char com, u_char *param);
int    CdRead(int sectors, u_long *buf, int mode);
int    CdRead2(long mode);
int    CdReady(int mode, u_char *result);
int    CdSync(int mode, u_char *result);
CdlCB CdReadyCallback(CdlCB func);
CdlCB CdSyncCallback(CdlCB func);
int    CdFlush(void);
int    CdDiskReady(int mode);
void  *CdGetSector(void *madr, int size);
int    CdGetToc(CdlLOC *loc);
CdlLOC *CdIntToPos(int i, CdlLOC *p);
int    CdPosToInt(CdlLOC *p);
CdlLOC *CdLastPos(void);
u_char *CdLastCom(void);

/* BCD conversion */
#define btoi(b) ((b) / 16 * 10 + (b) % 16)
#define itob(i) ((i) / 10 * 16 + (i) % 10)

/* CD status flags */
#define CdlStatStandby   0x00
#define CdlStatShellOpen 0x10
#define CdlStatSeekError 0x04
#define CdlStatIdError   0x08
#define CdlStatReading   0x20
#define CdlStatSeeking   0x40
#define CdlStatPlay      0x80
#define CdlStatError     0x01
#define CdlStatSeek      0x40
#define CdlStatRead      CdlStatReading
#define CdlDataReady     CdlDATAREADY

#endif /* __PSX_LIBCD_H__ */
