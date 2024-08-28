#ifndef _BAKUDAN_H_
#define _BAKUDAN_H_

#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include "Game/game.h"
#include "libgv/libgv.h"
#include "Game/target.h"
#include "Game/control.h"

// c4 (armed)

typedef struct BakudanWork
{
    GV_ACT         field_0_actor;
    CONTROL        control;
    OBJECT_NO_ROTS field_9C_kmd;
    MATRIX         field_C0_light_mtx[2];
    MATRIX        *field_100_pMtx; // rotation matrix (if the c4 is placed on a moving target)
    SVECTOR       *field_104;
    int            detonator_btn_pressed;
    int            detonator_frames_count; // number of actor actions to wait before detonate
    GV_PAD        *field_110_pPad;
    int            idx_current_c4; // the index of the current c4
    int            map;
} BakudanWork;

GV_ACT *NewBakudan_8006A6CC(MATRIX *pMtx, SVECTOR *pVec, int a3, int not_used, void *data);

#endif // _BAKUDAN_H_
