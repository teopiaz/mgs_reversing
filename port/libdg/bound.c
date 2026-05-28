#include "libdg.h"
#include "common.h"
#include "game/game.h"
#include <stdio.h>
#include <stdlib.h>
#include "psx/port_ptr.h"

STATIC void DG_WriteObjClut(DG_OBJ *obj, int idx);
STATIC void DG_WriteObjClutUV(DG_OBJ *obj, int idx);
STATIC void DG_BoundIrTexture(DG_CHANL *chanl, int idx);

static inline void copy_bounding_box_to_spad(DG_BOUND *bounds)
{
    DG_BOUND *bounding_box = (DG_BOUND *)SCRPAD_ADDR;
    bounding_box->min.vx = bounds->min.vx;
    bounding_box->min.vy = bounds->min.vy;
    bounding_box->min.vz = bounds->min.vz;

    bounding_box->max.vx = bounds->max.vx;
    bounding_box->max.vy = bounds->max.vy;
    bounding_box->max.vz = bounds->max.vz;
}

static inline void set_svec_from_bounding_box(int i, SVECTOR *svec)
{
    svec->vx = i & 1 ? ((int *)SCRPAD_ADDR)[3] : ((int *)SCRPAD_ADDR)[0];
    svec->vy = i & 2 ? ((int *)SCRPAD_ADDR)[4] : ((int *)SCRPAD_ADDR)[1];
    svec->vz = i & 4 ? ((int *)SCRPAD_ADDR)[5] : ((int *)SCRPAD_ADDR)[2];
}

void DG_BoundStart(void)
{
    /* do nothing */
}

STATIC void DG_BoundObjs(DG_OBJS *objs, int idx, unsigned int flag, int in_bound_mode)
{
    int        i, i2, i3, a2, t0, a3, t1;
    int        bound_mode;
    int        n_models;
    int        n_bounding_box_vec;
    int        ret, extra;
    int       *test;
    DG_OBJ    *obj;
    DVECTOR   *dvec;
    SVECTOR   *svec;
    DG_VECTOR *vec3_1;
    DG_VECTOR *vec3_2;
    DG_BOUND  *mdl_bounds;

    n_models = objs->n_models;
    obj = (DG_OBJ *)&objs->objs;

    /* Port: verify the DG_OBJ memory is valid (not freed/scribbled) */
    if (n_models > 0 && obj->model && ((unsigned long)obj->model & 0xFCFCFCFC00000000ULL)) {
        return; /* Memory was freed — skip this object set */
    }

    for (; n_models > 0; --n_models)
    {
        /* Skip objects with NULL/invalid model (freed memory) */
        if (!port_ptr_in_pool(obj->model)) {
            obj++;
            continue;
        }
        /* Also sanitize the extend pointer: if it's out of pool, it's been
           stomped somehow — treat as NULL for this frame. */
        if (obj->extend && !port_ptr_in_pool(obj->extend)) {
            obj->extend = NULL;
        }
        {
            static int bt_enable = -1;
            if (bt_enable == -1) { const char *e = getenv("DG_BOUND_TRACE"); bt_enable = (e && *e) ? 1 : 0; }
            if (bt_enable) {
                fprintf(stderr, "  [bo] model=%p n_packs=%d extend=%p packs[%d]=%p\n",
                        (void *)obj->model, obj->n_packs, (void *)obj->extend, idx, (void *)obj->packs[idx]);
            }
        }
        bound_mode = 0;
        if (in_bound_mode)
        {
            bound_mode = 2;
            /* Skip GTE frustum culling — the port's projection differs from
               PSX GTE, causing incorrect object culling. Always mark visible. */
            (void)flag;
        }

        // loc_800188E4
        obj->bound_mode = bound_mode;
        if (bound_mode)
        {
            obj->free_count = 8;
            if (!obj->packs[idx])
            {
                {
                    DG_OBJ *chk = obj;
                    int ok = 1, cnt = 0;
                    while (chk && cnt++ < 256) {
                        if (!port_ptr_in_pool(chk) || !port_ptr_in_pool(chk->model) ||
                            chk->n_packs <= 0 || chk->n_packs > 4096) {
                            ok = 0; break;
                        }
                        chk = chk->extend;
                    }
                    if (!ok) { obj->bound_mode = 0; goto next_model; }
                }
                int res = DG_MakeObjPacket(obj, idx, flag);
                if (res < 0)
                {
                    obj->bound_mode = 0;
                    if (flag & DG_FLAG_GBOUND)
                    {
                        objs->bound_mode = 0;
                        return;
                    }
                }
            }
        }
        else
        {
            if (obj->packs[idx])
            {
                --obj->free_count;
                if (obj->free_count <= 0)
                {
                    DG_FreeObjPacket(obj, idx);
                }
            }
        }
        next_model:
        obj++;
    }
}

void DG_BoundChanl(DG_CHANL *chanl, int idx)
{
    int          i, i2, i3, a2, t0, a3, t1;
    int          n_objs;
    int          bound_mode;
    DG_OBJS    **objs;
    int          local_group_id;
    DVECTOR     *dvec;
    SVECTOR     *svec;
    DG_VECTOR   *vec3_1;
    DG_VECTOR   *vec3_2;
    DG_BOUND    *mdl_bounds;
    int          n_bounding_box_vec;
    int         *test;
    unsigned int flag;

    DG_Clip(&chanl->clip_rect, chanl->clip_distance);

    objs = chanl->queue;
    n_objs = chanl->objs_index;
    local_group_id = DG_CurrentGroupID;

    for (; n_objs > 0; --n_objs)
    {
        DG_OBJS *current_objs = *objs;
        objs++;

        /* Port: skip freed/corrupt queue entries.
           Check for macOS freed-memory scribble (0xfcfc pattern in world matrix) */
        if (!current_objs || current_objs->world.m[0][0] == (short)0xfcfc)
            continue;

        flag = current_objs->flag;

        bound_mode = 0;
        if (!(flag & DG_FLAG_INVISIBLE))
        {
            if (!current_objs->group_id || (current_objs->group_id & local_group_id))
            {
                bound_mode = 2;
                /* Port: the GBOUND frustum test below uses the PSX GTE's
                   rtpt_b + scratchpad store, which produces wrong screen
                   coords on 64-bit and culls everything to 0. DG_BoundObjs
                   already skips its per-model BOUND test for the same
                   reason; mirror that here: trust the group-level flag and
                   always mark visible. Restore the GTE path once the GBOUND
                   projection is verified 64-bit-clean. */
                (void)flag;
            }
        }
        current_objs->bound_mode = bound_mode;
#ifdef PORT_BUILD_VERBOSE
        {
            static int bt_enable = -1;
            if (bt_enable == -1) { const char *e = getenv("DG_BOUND_TRACE"); bt_enable = (e && *e) ? 1 : 0; }
            if (bt_enable) {
                fprintf(stderr, "[dg] BoundObjs chanl=%d objs=%p def=%p n_models=%d flag=%x mode=%d\n",
                        idx, (void *)current_objs, (void *)current_objs->def,
                        current_objs->n_models, flag, bound_mode);
            }
        }
#endif
        DG_BoundObjs(current_objs, idx, flag, bound_mode);
    }

    DG_BoundIrTexture(chanl, idx);
}

void DG_BoundEnd(void)
{
    /* do nothing */
}

// Possibly a different file.

STATIC DG_TEX DG_UnknownTexture = {0};

/* Replace the CLUT for this model with a plain white one for thermal goggles */
STATIC void DG_WriteObjClut(DG_OBJ *obj, int idx)
{
    int       n_packs;
    POLY_GT4 *pPack = obj->packs[idx];
    short     val = 0x3FFF;
    if (pPack && pPack->clut != val)
    {
        while (obj)
        {
            n_packs = obj->n_packs;
            while (n_packs > 0)
            {
                pPack->clut = val;

                ++pPack;
                --n_packs;
            }

            obj = obj->extend;
        }
    }
}

/* Restore the CLUT for this model */
STATIC void DG_WriteObjClutUV(DG_OBJ *obj, int idx)
{
    unsigned short id;
    POLY_GT4      *pack;
    int            n_packs;
    short         *tex_ids;
    DG_TEX        *texture;
    unsigned short current_id;

    pack = obj->packs[idx];

    if (pack && pack->clut == 0x3FFF)
    {
        texture = &DG_UnknownTexture;
        id = 0;
        while (obj)
        {
            tex_ids = obj->model->materials;
            for (n_packs = obj->n_packs; n_packs > 0; --n_packs)
            {
                current_id = *tex_ids;
                tex_ids++;
                if ((current_id & 0xFFFF) != id)
                {
                    id = current_id;
                    texture = DG_GetTexture(id);
                }
                pack->clut = texture->clut;
                pack++;
            }
            obj = obj->extend;
        }
    }
}

// there must be a way to match this without the repetition
STATIC void DG_BoundIrTexture(DG_CHANL *chanl, int idx)
{
    DG_OBJS **queue;
    int       n_objects;
    DG_OBJS  *objs;
    DG_OBJ   *obj;
    int       n_models;

    queue = chanl->queue;
    if (GM_GameStatus & STATE_THERMG)
    {
        for (n_objects = chanl->objs_index; n_objects > 0; n_objects--)
        {
            objs = *queue++;

            if (objs->flag & DG_FLAG_IRTEXTURE && objs->bound_mode != 0)
            {
                obj = objs->objs;

                for (n_models = objs->n_models; n_models > 0; n_models--)
                {
                    if (obj->bound_mode != 0)
                    {
                        DG_WriteObjClut(obj, idx);
                    }

                    obj++;
                }
            }
        }
    }
    else
    {
        for (n_objects = chanl->objs_index; n_objects > 0; n_objects--)
        {
            objs = *queue++;

            if (objs->flag & DG_FLAG_IRTEXTURE && objs->bound_mode != 0)
            {
                obj = objs->objs;

                for (n_models = objs->n_models; n_models > 0; n_models--)
                {
                    DG_WriteObjClutUV(obj, idx);
                    obj++;
                }
            }
        }
    }
}
