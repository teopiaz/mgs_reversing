#include "libdg.h"

#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include "common.h"
#include <stdio.h>
#include <stdlib.h>

STATIC int DG_MakeObjs_helper( DG_MDL *mdl )
{
    int          val;
    int          mask;
    unsigned int flags;

    flags = mdl->flags;
    val = 0;

    if ((flags & 0x300) != 0)
    {
        mask = 4 - (flags >> 12 & 3);
        val = mask * 0xfa;
        if ((flags & 0x100) == 0)
        {
            val = mask * -0xfa;
        }
    }

    return val;
}

DG_OBJS *DG_MakeObjs( DG_DEF *def, int flag, int chanl )
{
    DG_MDL *model = (DG_MDL *)&def[1];

    const int objs_size = sizeof(DG_OBJS) + (sizeof(DG_OBJ) * def->n_models);
    DG_OBJS  *objs_buf = (DG_OBJS *)GV_Malloc(objs_size);

    if (!objs_buf)
    {
        return 0;
    }
    else
    {
        int     numMesh;
        DG_OBJ *obj;

        GV_ZeroMemory(objs_buf, objs_size);
        objs_buf->world = DG_ZeroMatrix;

        objs_buf->def = def;

        objs_buf->n_models = def->n_visible;

        objs_buf->flag = flag;
        objs_buf->chanl = chanl;
        objs_buf->light = &DG_LightMatrix;

        obj = &objs_buf->objs[0];
        for (numMesh = def->n_models; numMesh > 0; numMesh--)
        {
            obj->model = model;
            {
                /* model->extend stores an index (as intptr_t cast to pointer).
                   On PSX it was int; on 64-bit the sign extension is lost. */
                int extend_idx = (int)(intptr_t)model->extend;
                int cur_idx = (int)(obj - &objs_buf->objs[0]);
                {
                    static int bt = -1;
                    if (bt == -1) { const char *e = getenv("DG_BOUND_TRACE"); bt = (e && *e) ? 1 : 0; }
                    if (bt) fprintf(stderr, "[mk] objs=%p cur=%d/%d extend_idx=%d\n",
                                    (void *)objs_buf, cur_idx, def->n_models, extend_idx);
                }
                if (extend_idx < 0 || extend_idx >= def->n_models || extend_idx == cur_idx)
                {
                    obj->extend = 0;
                }
                else
                {
                    obj->extend = &objs_buf->objs[extend_idx];
                }
            }

            obj->raise = DG_MakeObjs_helper(model);
            obj->n_packs = model->n_faces;
            obj++;
            model++;
        }
        return objs_buf;
    }
}

void DG_FreeObjs( DG_OBJS *objs )
{
    int     n_models;
    DG_OBJ *obj;

    n_models = objs->n_models;
    obj = objs->objs;
    while (n_models > 0)
    {
        DG_FreeObjPacket(obj, 0);
        DG_FreeObjPacket(obj, 1);
        --n_models;
        ++obj;
    }
    DG_FreePreshade(objs);
    /* Zero out before freeing so stale render queue references see
       n_models=0 and skip instead of crashing on freed memory. */
    memset(objs->objs, 0, objs->n_models * sizeof(DG_OBJ));
    objs->n_models = 0;
    GV_Free(objs);
}

void DG_SetObjsRots( DG_OBJS *objs, SVECTOR *rot )
{
    objs->rots = rot;
}

void DG_SetObjsMovs( DG_OBJS *objs, SVECTOR *mov )
{
    objs->movs = mov;
}

