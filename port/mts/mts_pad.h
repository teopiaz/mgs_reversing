#ifndef __MTS_PAD_H__
#define __MTS_PAD_H__

#include "mts.h"

#define MTS_PAD_INACTIVE    0

enum {
    PAD_STATE_UNDETECTED = 0,
    PAD_STATE_DETECTED = 1,
    PAD_STATE_IDENTIFIED = 2,
    PAD_STATE_ACTUATORS_READY = 3,
};

typedef struct {
    short          flag;
    unsigned short button;
    unsigned char  rx;
    unsigned char  ry;
    unsigned char  lx;
    unsigned char  ly;
} MTS_PAD_IN;

/* mts_pad.c (private) */
void mts_stop_controller( void );
long mts_PadRead( int unused );
int mts_control_vibration( int enable );

#endif /* __MTS_PAD_H__ */
