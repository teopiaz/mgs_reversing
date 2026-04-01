#ifndef __PSX_LIBAPI_H__
#define __PSX_LIBAPI_H__

#include <sys/types.h>

/* Event classes */
#define SwCARD          0xf0000011
#define HwCARD          0xf0000011
#define EvSpIOE         0x0004
#define EvSpERROR       0x8000
#define EvSpTIMOUT      0x0100
#define EvSpNEW         0x2000
#define EvMdINTR        0x1000
#define EvMdNOINTR      0x2000

/* RCnt */
#define RCntCNT0        0xf2000000
#define RCntCNT1        0xf2000001
#define RCntCNT2        0xf2000002
#define RCntCNT3        0xf2000003
#define RCntMdINTR      0x1000
#define RCntMdNOINTR    0x0000
#define RCntMdSC        0x0001
#define RCntMdSP        0x0000
#define RCntMdFR        0x0000

long OpenEvent(u_long class_id, long spec, long mode, long (*func)());
long CloseEvent(long event);
long EnableEvent(long event);
long DisableEvent(long event);
long TestEvent(long event);
int  DeliverEvent(u_long class_id, long spec);
int  UnDeliverEvent(u_long class_id, long spec);

long OpenTh(void (*func)(), u_long sp, u_long gp);
int  CloseTh(long thread);
int  ChangeTh(long thread);

int  ChangeClearRCnt(int mode);
long SetRCnt(u_long spec, u_short target, long mode);
long GetRCnt(u_long spec);
long StartRCnt(u_long spec);
long StopRCnt(u_long spec);
long ResetRCnt(u_long spec);

void ReturnFromException(void);
void FlushCache(void);

long EnterCriticalSection(void);
long ExitCriticalSection(void);

long SwEnterCriticalSection(void);
long SwExitCriticalSection(void);

#endif /* __PSX_LIBAPI_H__ */
