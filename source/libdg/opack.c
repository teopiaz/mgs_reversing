#include "libdg.h"
#include "common.h"

static int AllocPacks( DG_OBJ *obj, int index )
{
    int size;
    DG_OBJ *iter;
#ifdef PORT_BUILD
    int safety = 0;
#endif

#ifdef PORT_BUILD
    if ( obj == NULL ) return -1;
#endif

    size = 0;
    for ( iter = obj; iter != NULL; iter = iter->extend )
    {
#ifdef PORT_BUILD
        /* Validate pointer: must be in userspace range (not freed/scribbled) */
        {
            uintptr_t p = (uintptr_t)iter;
            uintptr_t ep;
            if ( p < 0x1000 || ( p >> 48 ) != 0 ) break;
            if ( iter->n_packs <= 0 || iter->n_packs > 4096 ) break;
            size += iter->n_packs;
            ep = (uintptr_t)iter->extend;
            if ( ep != 0 && ( ep < 0x1000 || ( ep >> 48 ) != 0 ) ) break;
        }
        /* Bound the extend chain: a corrupt DG_OBJ can loop forever. */
        if ( ++safety >= 256 ) break;
#else
        size += iter->n_packs;
#endif
    }

#ifdef PORT_BUILD
    if ( size <= 0 ) return -1;
#endif

    size *= sizeof( POLY_GT4 );
    if ( GV_AllocMemory2( index, size, (void **)&obj->packs[ index ] ) == NULL ) return -1;
    return 0;
}

static void InitPacks( DG_OBJ *obj, int index )
{
    POLY_GT4 *packs;
    int color, i;

#ifdef PORT_BUILD
    if ( obj->model == NULL ) return;
#endif

    color = 0x3E808080;
    if ( !( obj->model->flag & DG_MODEL_TRANS ) ) color &= ~0x2000000;

    packs = obj->packs[ index ];
    for ( ; obj != NULL; obj = obj->extend )
    {
#ifdef PORT_BUILD
        {
            uintptr_t _p = (uintptr_t)obj;
            if ( _p < 0x1000 || ( _p >> 48 ) != 0 ) break;
        }
#endif
        for ( i = obj->n_packs; i > 0; i-- )
        {
            setPolyGT4( packs );
            LSTORE( color, &packs->r0 );
            LSTORE( color, &packs->r1 );
            LSTORE( color, &packs->r2 );
            LSTORE( color, &packs->r3 );
            packs++;
        }
    }
}

void DG_WriteObjPacketUV( DG_OBJ *obj, int index )
{
    static DG_TEX EmptyTex = { 0 };

    int x, y, w, h, i;
    u_short id, next_id;
    DG_TEX *tex;
    POLY_GT4 *packs;
    u_short *texids;
    u_char *texcoords;

    if ( ( packs = obj->packs[ index ] ) == NULL ) return;

    tex = &EmptyTex;
    id = 0;

    for ( ; obj != NULL; obj = obj->extend )
    {
#ifdef PORT_BUILD
        {
            uintptr_t _p = (uintptr_t)obj;
            uintptr_t _m = (uintptr_t)obj->model;
            if ( _p < 0x1000 || ( _p >> 48 ) != 0 ) break;
            if ( _m < 0x1000 || ( _m >> 48 ) != 0 ) break;
        }
#endif
        texids = obj->model->texids;
        texcoords = obj->model->uvs;

        for ( i = obj->n_packs; i > 0 ; i-- )
        {
            next_id = *texids++;

            if ( next_id != id )
            {
                id = next_id;
                tex = DG_GetTexture( id );
            }

            x = tex->off_x;
            y = tex->off_y;
            w = tex->w + 1;
            h = tex->h + 1;
            packs->u0 = ( texcoords[ 0 ] * w / 256 ) + x;
            packs->v0 = ( texcoords[ 1 ] * h / 256 ) + y;
            packs->u1 = ( texcoords[ 2 ] * w / 256 ) + x;
            packs->v1 = ( texcoords[ 3 ] * h / 256 ) + y;
            packs->u2 = ( texcoords[ 6 ] * w / 256 ) + x;
            packs->v2 = ( texcoords[ 7 ] * h / 256 ) + y;
            packs->u3 = ( texcoords[ 4 ] * w / 256 ) + x;
            packs->v3 = ( texcoords[ 5 ] * h / 256 ) + y;
            packs->tpage = tex->tpage;
            packs->clut = tex->clut;
            packs++;
            texcoords += 8;
        }
    }
}

void DG_WriteObjPacketRGB( DG_OBJ *obj, int index )
{
    POLY_GT4 *packs;
    CVECTOR *rgbs;
    int i;

    if ( ( packs = obj->packs[ index ] ) == NULL ) return;
    for ( ; obj != NULL; obj = obj->extend )
    {
#ifdef PORT_BUILD
        {
            uintptr_t _p = (uintptr_t)obj;
            if ( _p < 0x1000 || ( _p >> 48 ) != 0 ) break;
        }
#endif
        if ( ( rgbs = obj->rgbs ) == NULL ) continue;
        for ( i = obj->n_packs; i > 0; i-- )
        {
            LCOPY2( &rgbs[ 0 ], &packs->r0, &rgbs[ 1 ], &packs->r1 );
            LCOPY2( &rgbs[ 3 ], &packs->r2, &rgbs[ 2 ], &packs->r3 );
            packs++;
            rgbs += 4;
        }
    }
}

int DG_MakeObjPacket( DG_OBJ *obj, int index, int flags )
{
#ifdef PORT_BUILD
    if ( obj == NULL || obj->model == NULL || obj->n_packs == 0 ) return -1;
#endif

    if ( AllocPacks( obj, index ) < 0 ) return -1;

#ifdef PORT_BUILD
    if ( obj->packs[ index ] == NULL ) return -1;
#endif

    InitPacks( obj, index );
    if ( flags & DG_FLAG_TEXT ) DG_WriteObjPacketUV( obj, index );
    if ( flags & DG_FLAG_PAINT ) DG_WriteObjPacketRGB( obj, index );
    return 0;
}

void DG_FreeObjPacket( DG_OBJ *obj, int index )
{
    void **packs;

    packs = (void **)&obj->packs[ index ];
    if ( *packs != NULL )
    {
        GV_FreeMemory2( index, packs );
        *packs = NULL;
    }
}

int DG_MakeObjsPacket( DG_OBJS *objs, int index )
{
    DG_OBJ *obj;
    int flag, i;

    obj = objs->objs;
    flag = objs->flag;
    for ( i = objs->n_models; i > 0; i-- )
    {
        if ( obj->packs[ index ] == NULL )
        {
            if ( DG_MakeObjPacket( obj, index, flag ) < 0 ) return -1;
        }
        obj++;
    }
    return 0;
}

void DG_FreeObjsPacket( DG_OBJS *objs, int index )
{
    int i;
    DG_OBJ *obj;

    obj = objs->objs;
    for ( i = objs->n_models; i > 0; i-- )
    {
        DG_FreeObjPacket( obj, index );
        obj++;
    }
}
