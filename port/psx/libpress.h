#ifndef __PSX_LIBPRESS_H__
#define __PSX_LIBPRESS_H__

#include <sys/types.h>

void DecDCTReset(int mode);
int  DecDCTBufSize(u_long *bs);
void DecDCTin(u_long *mdec_bs, int mode);
void DecDCTout(u_long *buf, int size);
int  DecDCToutSync(int mode);
void DecDCToutCallback(void (*func)(void));
void DecDCTvlc(u_long *bs, u_long *buf);
void DecDCTvlc2(u_long *bs, u_long *buf, int q);
void DecDCTvlcBuild(void);

#endif /* __PSX_LIBPRESS_H__ */
