/**
 * Stub for source/libhzd/collide.c
 * This file has hardcoded PSX scratchpad addresses and MIPS asm
 * that can't compile on macOS. Stubbed until a proper port is done.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libgte.h"
#include "libgv/libgv.h"
#include "libhzd/libhzd.h"

/* hzdd.c stubs — the original is excluded because of OFFSET_TO_PTR.
   HZD_LoadInitHzd is in port/libhzd/hzd_loader.c */

int HZD_CurrentGroup = 0;

void HZD_StartDaemon(void)
{
    extern int HZD_LoadInitHzd(void *, int);
    GV_SetLoader('h', (void *)HZD_LoadInitHzd);
}

HZD_HDL *HZD_MakeHandler(HZD_MAP *hzd, int areaIndex, int dynamic_segments, int dynamic_floors)
{
    if (!hzd) return NULL;

    /* Match original: single allocation for struct + arrays */
    int alloc_size = sizeof(HZD_HDL)
                   + sizeof(HZD_FLR *) * dynamic_floors
                   + sizeof(HZD_SEG *) * dynamic_segments
                   + dynamic_segments; /* flags */
    HZD_HDL *hdl = (HZD_HDL *)GV_Malloc(alloc_size);
    if (!hdl) {
        printf("[hzd] Failed to allocate HZD_HDL\n");
        return NULL;
    }

    /* Set up pointers into the allocation (like original) */
    hdl->dynamic_floors = (HZD_FLR **)&hdl[1];
    hdl->dynamic_segments = (HZD_SEG **)&hdl->dynamic_floors[dynamic_floors];
    hdl->dynamic_flags = (char *)&hdl->dynamic_segments[dynamic_segments];

    /* Zero the dynamic arrays */
    memset(hdl->dynamic_floors, 0, sizeof(HZD_FLR *) * dynamic_floors);
    memset(hdl->dynamic_segments, 0, sizeof(HZD_SEG *) * dynamic_segments);
    memset(hdl->dynamic_flags, 0, dynamic_segments);

    hdl->max_dynamic_segments = dynamic_segments;
    hdl->max_dynamic_floors = dynamic_floors;
    hdl->header = hzd;
    hdl->group = &hzd->groups[areaIndex];
    hdl->dynamic_queue_index = 0;
    hdl->dynamic_floor_index = 0;
    hdl->n_cameras = 0;
    hdl->traps = NULL;
    hdl->route = NULL;

    printf("[hzd] Created handler: area=%d, %d dyn_segs, %d dyn_floors\n",
           areaIndex, dynamic_segments, dynamic_floors);
    return hdl;
}

void HZD_MakeRoute(HZD_MAP *hzd, char *arg1) { (void)hzd; (void)arg1; }
void HZD_FreeHandler(void *ptr) { if (ptr) GV_Free(ptr); }

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
