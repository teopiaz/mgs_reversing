#ifndef __PSX_LIBPAD_H__
#define __PSX_LIBPAD_H__

#include <sys/types.h>

#define PadStateStable    6
#define PadStateFindCTP1  2
#define PadStateFindPad   1

#define InfoModeCurID     1
#define InfoModeCurExID   2
#define InfoModeCurExOffs 3
#define InfoModeIdTable   4

void PadInitDirect(u_char *pad1, u_char *pad2);
void PadStartCom(void);
void PadStopCom(void);
int  PadSetAct(int port, u_char *table, int len);
int  PadSetActAlign(int port, u_char *table);
int  PadGetState(int port);
int  PadInfoMode(int port, int term, int offs);
int  PadInfoAct(int port, int act, int term);
int  PadInfoComb(int port, int listno, int offs);

#endif /* __PSX_LIBPAD_H__ */
