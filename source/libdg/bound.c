#include "libdg.h"
#include "common.h"
#include "game/game.h"
#ifdef PORT_BUILD
#include <stdio.h>
#include <stdlib.h>
#include "psx/port_ptr.h"
#endif
#ifdef PORT_BUILD
#include <stdio.h>
#include <stdlib.h>
#include "psx/port_ptr.h"
#endif

static void UpdateThermalTexture( DG_CHANL *chanl, int index );

typedef struct {
    int vx, vy, vz;
} VECTOREX;

typedef	struct {
    VECTOREX bound_min;
    VECTOREX bound_max;
    SVECTOR  clip[ 3 ];
    DVECTOR  vxy[ 4 ][ 3 ];
#ifdef PORT_BUILD
    int      vzp[ 4 ][ 3 ];    /* 4-byte stride: PSX long is 32-bit */
#else
    long     vzp[ 4 ][ 3 ];
#endif
} ScrPad;

#define SCRPAD      ((ScrPad *)SCRPAD_ADDR)

#define	BOUND_MIN   (&(SCRPAD->bound_min))
#define	BOUND_MAX   (&(SCRPAD->bound_max))
#define	CLIP        (SCRPAD->clip)
#define	VXY         (SCRPAD->vxy)
#define	VZP         (SCRPAD->vzp)

void DG_BoundStart( void )
{
    /* do nothing */
}

static inline void MakeBoundVerts( int *bound )
{
    int i, j;
    SVECTOR *clip;
    DVECTOR *vxy;
#ifdef PORT_BUILD
    int *vzp;
#else
    long *vzp;
#endif

    BOUND_MIN->vx = bound[ 0 ];
    BOUND_MIN->vy = bound[ 1 ];
    BOUND_MIN->vz = bound[ 2 ];
    BOUND_MAX->vx = bound[ 3 ];
    BOUND_MAX->vy = bound[ 4 ];
    BOUND_MAX->vz = bound[ 5 ];

    clip = CLIP;
    vxy = VXY[ 0 ];
    vzp = VZP[ 0 ];

    for ( i = 9; i > 0; )
    {
        for ( j = 3; j > 0; j-- )
        {
            clip->vx = ( i & 1 ) ? BOUND_MAX->vx : BOUND_MIN->vx;
            clip->vy = ( i & 2 ) ? BOUND_MAX->vy : BOUND_MIN->vy;
            clip->vz = ( i & 4 ) ? BOUND_MAX->vz : BOUND_MIN->vz;
            clip++;
            i--;
        }

        clip = CLIP;
        gte_stsxy3c( vxy );
        gte_stsz3c( vzp );

        gte_ldv3c( CLIP );
        vxy += 3;
        vzp += 3;
        gte_rtpt_b();
    }

    gte_stsxy3c( vxy );
    gte_stsz3c( vzp );
}

static inline int BoundCheckDepth( int xl, int yl, int xh, int yh )
{
    int i, bound_flag;
#ifdef PORT_BUILD
    int *depth;
#else
    long *depth;
#endif

    if ( xh > 160 || xl < -160 || yh > 112 || yl < -112 )
    {
        /* prim partially on-screen */
        bound_flag = 1;
    }
    else
    {
        /* prim entirely on-screen */
        bound_flag = 2;
    }

    depth = VZP[ 1 ];
    for ( i = 8; i > 0; i-- )
    {
        if ( *depth != 0 ) return bound_flag;
        depth++;
    }

    /* clip prim with zero depth */
    return 0;
}

static inline int BoundCheck( DVECTOR *verts )
{
    int i, bound_flag;
    int xl, yl, xh, yh;

    xh = xl = VXY[ 1 ][ 0 ].vx;
    yh = yl = VXY[ 1 ][ 0 ].vy;

    for ( i = 7; i > 0; i-- )
    {
        verts++;

        if ( verts->vx < xl )
        {
            xl = verts->vx;
        }
        else if ( verts->vx > xh )
        {
            xh = verts->vx;
        }

        if ( verts->vy < yl )
        {
            yl = verts->vy;
        }
        else if ( verts->vy > yh )
        {
            yh = verts->vy;
        }
    }

    if ( xl > 160 || xh < -160 || yl > 112 || yh < -112 )
    {
        /* prim completely off-screen */
        bound_flag = 0;
    }
    else
    {
        /* prim on-screen, check against depth */
        bound_flag = BoundCheckDepth( xl, yl, xh, yh );
    }

    return bound_flag;
}

#ifdef PORT_BUILD
/* Validate the whole extend chain before packet creation - skip the model if any
   link points to freed/invalid memory (use-after-free). */
static int ValidExtendChain( DG_OBJ *obj )
{
    DG_OBJ *chk = obj;
    int cnt = 0;

    while ( chk && cnt++ < 256 )
    {
        if ( !port_ptr_in_pool( chk ) || !port_ptr_in_pool( chk->model ) ||
             chk->n_packs <= 0 || chk->n_packs > 4096 )
        {
            return 0;
        }
        chk = chk->extend;
    }
    return 1;
}
#endif

static void BoundObjs( DG_OBJS *objs, int pack, int flag, int arg_flag )
{
    int n_models, bound_flag;
    DG_OBJ *obj;

    obj = objs->objs;

    /* Port: verify the DG_OBJ memory is valid (not freed/scribbled) */
    if ( objs->n_models > 0 && obj->model && ( (unsigned long)obj->model & 0xFCFCFCFC00000000ULL ) )
    {
        return; /* Memory was freed - skip this object set */
    }

    for ( n_models = objs->n_models; n_models > 0; n_models-- )
    {
#ifdef PORT_BUILD
        /* Skip objects with NULL/invalid model (freed memory) */
        if ( !port_ptr_in_pool( obj->model ) )
        {
            obj++;
            continue;
        }
        /* Also sanitize the extend pointer: if it's out of pool, it's been
           stomped somehow - treat as NULL for this frame. */
        if ( obj->extend && !port_ptr_in_pool( obj->extend ) )
        {
            obj->extend = NULL;
        }
        {
            static int bt_enable = -1;
            if (bt_enable == -1) { const char *e = getenv("DG_BOUND_TRACE"); bt_enable = (e && *e) ? 1 : 0; }
            if (bt_enable) {
                fprintf(stderr, "  [bo] model=%p n_packs=%d extend=%p packs[%d]=%p\n",
                        (void *)obj->model, obj->n_packs, (void *)obj->extend, pack, (void *)obj->packs[pack]);
            }
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
#endif
        bound_flag = 0;

        if ( arg_flag != 0 )
        {
            bound_flag = 2;

#ifdef PORT_BUILD
            /* Skip GTE frustum culling - the port's projection differs from
               PSX GTE, causing incorrect object culling. Always mark visible. */
            (void)flag;
#else
            if ( flag & DG_FLAG_BOUND )
            {
                gte_SetRotMatrix( &obj->screen );
                gte_SetTransMatrix( &obj->screen );
                MakeBoundVerts( &obj->model->lx );
                bound_flag = BoundCheck( VXY[ 1 ] );
            }
#endif
#endif
        }

        obj->bound_mode = bound_flag;
        if ( bound_flag != 0 )
        {
            obj->free_count = 8;

#ifdef PORT_BUILD
            if ( obj->packs[ pack ] == NULL && !ValidExtendChain( obj ) )
            {
                obj->bound_mode = 0;
                goto next_model;
            }
#endif

            if ( obj->packs[ pack ] == NULL && DG_MakeObjPacket( obj, pack, flag ) < 0 )
            {
                obj->bound_mode = 0;

                if ( flag & DG_FLAG_GBOUND )
                {
                    objs->bound_mode = 0;
                    return;
                }
            }
        }
        else
        {
            if ( obj->packs[ pack ] != NULL && --obj->free_count <= 0 )
            {
                DG_FreeObjPacket( obj, pack );
            }
        }

#ifdef PORT_BUILD
        next_model:
#endif
        obj++;
    }
}

void DG_BoundChanl( DG_CHANL *chanl, int index )
{
    int group_id, n_objs, flag, bound_flag;
    DG_OBJS **queue, *objs;

    DG_Clip( &chanl->clip_rect, chanl->screen );

    queue = chanl->queue;
    group_id = DG_CurrentGroupID;
    for ( n_objs = chanl->objs_index; n_objs > 0; n_objs-- )
    {
        objs = *queue++;

        /* Port: skip freed/corrupt queue entries.
           Check for macOS freed-memory scribble (0xfcfc pattern in world matrix) */
        if ( !objs || objs->world.m[ 0 ][ 0 ] == (short)0xfcfc ) continue;

        flag = objs->flag;
        bound_flag = 0;

        if ( !( flag & DG_FLAG_INVISIBLE ) && ( objs->group_id == 0 || ( objs->group_id & group_id ) ) )
        {
            bound_flag = 2;

#ifdef PORT_BUILD
            /* Port: the GBOUND frustum test below uses the PSX GTE's
               rtpt_b + scratchpad store, which produces wrong screen
               coords on 64-bit and culls everything to 0. BoundObjs
               already skips its per-model BOUND test for the same
               reason; mirror that here: trust the group-level flag and
               always mark visible. */
            (void)flag;
#else
            if ( flag & DG_FLAG_GBOUND )
            {
                gte_SetRotMatrix( &objs->objs[ 0 ].screen );
                gte_SetTransMatrix( &objs->objs[ 0 ].screen );
                MakeBoundVerts( &objs->def->lx );
                bound_flag = BoundCheck( VXY[ 1 ] );
            }
#endif /* PORT_BUILD */
        }

        objs->bound_mode = bound_flag;
#ifdef PORT_BUILD_VERBOSE
        {
            static int bt_enable = -1;
            if (bt_enable == -1) { const char *e = getenv("DG_BOUND_TRACE"); bt_enable = (e && *e) ? 1 : 0; }
            if (bt_enable) {
                fprintf(stderr, "[dg] BoundObjs chanl=%d objs=%p def=%p n_models=%d flag=%x mode=%d\n",
                        index, (void *)objs, (void *)objs->def,
                        objs->n_models, flag, bound_flag);
            }
        }
#endif
        BoundObjs( objs, index, flag, bound_flag );
    }

    UpdateThermalTexture( chanl, index );
}

void DG_BoundEnd( void )
{
    /* do nothing */
}

static void SetThermalClut( DG_OBJ *obj, int index )
{
    int n_packs;
    u_short clut;
    POLY_GT4 *packs;

    packs = obj->packs[ index ];
    clut = getClut( 1008, 255 );

    if ( packs != NULL && packs->clut != clut )
    {
        while ( obj != NULL )
        {
            for ( n_packs = obj->n_packs; n_packs > 0; n_packs-- )
            {
                packs->clut = clut;
                packs++;
            }

            obj = obj->extend;
        }
    }
}

static void RestoreThermalClut( DG_OBJ *obj, int index )
{
    static DG_TEX EmptyTex = { 0 };

    int n_packs;
    u_short id, next_id;
    DG_TEX *tex;
    POLY_GT4 *packs;
    u_short *texids;

    packs = obj->packs[ index ];
    if ( packs != NULL && packs->clut == getClut( 1008, 255 ) )
    {
        tex = &EmptyTex;
        id = 0;

        while ( obj != NULL )
        {
            texids = obj->model->texids;

            for ( n_packs = obj->n_packs; n_packs > 0; n_packs-- )
            {
                next_id = *texids++;

                if ( next_id != id )
                {
                    id = next_id;
                    tex = DG_GetTexture( id );
                }

                packs->clut = tex->clut;
                packs++;
            }

            obj = obj->extend;
        }
    }
}

static void UpdateThermalTexture( DG_CHANL *chanl, int index )
{
    int n_objs, n_models;
    DG_OBJS **queue, *objs;
    DG_OBJ *obj;

    queue = chanl->queue;
    if ( GM_GameStatus & STATE_THERMG )
    {
        for ( n_objs = chanl->objs_index; n_objs > 0; n_objs-- )
        {
            objs = *queue++;

            if ( ( objs->flag & DG_FLAG_IRTEXTURE ) && objs->bound_mode != 0 )
            {
                obj = objs->objs;

                for ( n_models = objs->n_models; n_models > 0; n_models-- )
                {
                    if ( obj->bound_mode != 0 ) SetThermalClut( obj, index );
                    obj++;
                }
            }
        }
    }
    else
    {
        for ( n_objs = chanl->objs_index; n_objs > 0; n_objs-- )
        {
            objs = *queue++;

            if ( ( objs->flag & DG_FLAG_IRTEXTURE ) && objs->bound_mode != 0 )
            {
                obj = objs->objs;

                for ( n_models = objs->n_models; n_models > 0; n_models-- )
                {
                    RestoreThermalClut( obj, index );
                    obj++;
                }
            }
        }
    }
}
