#include "libdg.h"

#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include "common.h"
#ifdef PORT_BUILD
#include <stdio.h>
#include <stdlib.h>
#endif

static int GetRaise( DG_MDL *mdl )
{
    int raise;

    raise = 0;
    if ( mdl->flag & 0x300 )
    {
        raise = ( 4 - ( ( mdl->flag >> 12 ) & 3 ) ) * 250;
        if ( !( mdl->flag & 0x100 ) ) raise *= -1;
    }
    return raise;
}

DG_OBJS *DG_MakeObjs( DG_DEF *def, int flag, int chanl )
{
    DG_OBJS *objs;
    DG_OBJ *obj;
    DG_MDL *mdl;
    int i, buf_size;

    mdl = def->models;

    buf_size = sizeof( DG_OBJS ) + sizeof( DG_OBJ ) * def->n_x_models;
    if ( ( objs = (DG_OBJS *)GV_Malloc( buf_size ) ) == NULL ) return NULL;

    GV_ZeroMemory( objs, buf_size );

    objs->world = DG_ZeroMatrix;
    objs->def = def;
    objs->n_models = def->n_models;
    objs->flag = flag;
    objs->chanl = chanl;
    objs->light = &DG_LightMatrix;

    obj = objs->objs;
    for ( i = def->n_x_models; i > 0; i-- )
    {
        obj->model = mdl;

#ifdef PORT_BUILD
        {
            static int bt = -1;
            if (bt == -1) { const char *e = getenv("DG_BOUND_TRACE"); bt = (e && *e) ? 1 : 0; }
            if (bt) fprintf(stderr, "[mk] objs=%p cur=%d/%d extend_idx=%d\n",
                            (void *)objs, (int)( obj - &objs->objs[ 0 ] ), def->n_models, mdl->extend);
        }
        /* Guard against out-of-range / self-referential extend indices, which
           chain DG_OBJ lists into loops or freed memory on the port. */
        if ( mdl->extend < 0 || mdl->extend >= def->n_models ||
             mdl->extend == (int)( obj - &objs->objs[ 0 ] ) )
#else
        if ( mdl->extend < 0 )
#endif
        {
            obj->extend = 0;
        }
        else
        {
            obj->extend = &objs->objs[ mdl->extend ];
        }

        obj->raise = GetRaise( mdl );
        obj->n_packs = mdl->n_faces;
        obj++;
        mdl++;
    }
        
    return objs;
}

void DG_FreeObjs( DG_OBJS *objs )
{
    DG_OBJ *obj;
    int i;

    obj = objs->objs;
    for ( i = objs->n_models; i > 0; i-- )
    {
        DG_FreeObjPacket( obj, 0 );
        DG_FreeObjPacket( obj, 1 );
        obj++;
    }

    DG_FreePreshade(objs);
#ifdef PORT_BUILD
    /* Zero out before freeing so stale render queue references see
       n_models=0 and skip instead of crashing on freed memory. */
    memset(objs->objs, 0, objs->n_models * sizeof(DG_OBJ));
    objs->n_models = 0;
#endif
    GV_Free(objs);
}

void DG_SetJointFrame( DG_OBJS *objs, SVECTOR *rots )
{
    objs->rots = rots;
}

void DG_SetSlideFrame( DG_OBJS *objs, SVECTOR *movs )
{
    objs->movs = movs;
}

