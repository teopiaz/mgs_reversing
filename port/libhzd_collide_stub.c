/**
 * Stub for source/libhzd/collide.c
 * This file has hardcoded PSX scratchpad addresses and MIPS asm
 * that can't compile on macOS. Stubbed until a proper port is done.
 *
 * HZD_MakeHandler, HZD_FreeHandler, HZD_StartDaemon, HZD_MakeRoute
 * are now in source/libhzd/hzdd.c (compiled from source).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libgte.h"
#include "libgv/libgv.h"
#include "libhzd/libhzd.h"

int HZD_CurrentGroup = 0;

int HZD_StepCheck(SVECTOR *nears, int count, int scale, SVECTOR *out)
{
    (void)nears; (void)count; (void)scale;
    memset(out, 0, sizeof(*out));
    return 0;
}

void HZD_SurfaceNormal(HZD_FLR *floor, SVECTOR *out)
{
    (void)floor;
    out->vx = 0; out->vy = -4096; out->vz = 0; out->pad = 0;
}

int HZD_LineCheck(HZD_HDL *hzd, SVECTOR *from, SVECTOR *to, int flag, int exclude)
{
    (void)hzd; (void)from; (void)to; (void)flag; (void)exclude;
    return 0;
}

int HZD_LineNearFlag(void) { return 0; }

void HZD_LineNearDir(SVECTOR *out)
{
    out->vx = 0; out->vy = 0; out->vz = 0; out->pad = 0;
}

void HZD_LineNearVec(SVECTOR *out)
{
    out->vx = 0; out->vy = 0; out->vz = 0; out->pad = 0;
}

int HZD_PointCheck(HZD_HDL *hzd, SVECTOR *point, int range, int flag, int exclude)
{
    (void)hzd; (void)point; (void)range; (void)flag; (void)exclude;
    return 0;
}

void HZD_PointNearSurface(void **surface)
{
    *surface = NULL;
}

void HZD_PointNearFlag(char *flags)
{
    *flags = 0;
}

void HZD_PointNearVec(SVECTOR *points)
{
    memset(points, 0, sizeof(SVECTOR));
}
