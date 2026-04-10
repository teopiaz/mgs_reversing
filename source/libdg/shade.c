#include "libdg.h"
#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include "common.h"

void DG_ShadeStart( void )
{
    /* do nothing */
}

//just an index using an int shifted to get each byte of the face normal idx, but didnt match that way
static inline void DG_ShadePack( unsigned int *nindices, POLY_GT4 *packs, char *colors )
{
    unsigned int ni = *nindices;
    unsigned int i0 = (ni << 2) & 0x3FC;
    unsigned int i1 = (ni >> 6) & 0x3FC;
    unsigned int i2 = (ni >> 22) & 0x3FC;
    unsigned int i3 = (ni >> 14) & 0x3FC;

    char *f0 = colors + i0;
    char *f1 = colors + i1;
    char *f2 = colors + i2;
    char *f3 = colors + i3;

    LCOPY2( (void *)f0, &packs->r0, (void *)f1, &packs->r1 );
    LCOPY2( (void *)f2, &packs->r2, (void *)f3, &packs->r3 );
}

STATIC POLY_GT4 *DG_ShadePacks( unsigned int *nindices, POLY_GT4 *packs, int n_packs )
{
    void *colors;

    for ( n_packs--; n_packs >= 0; n_packs-- )
    {
        colors = getScratchAddr(8);

        if ( packs->tag & 0xFFFF )
        {
            DG_ShadePack( nindices, packs, colors );
        }

        packs++;
        nindices++;
    }

    return packs;
}

STATIC POLY_GT4 *DG_ShadePacksIndirect( unsigned int *nindices, POLY_GT4 *packs, int n_packs, unsigned int *vindices )
{
    char        *colors;
    unsigned int mask;
    uintptr_t    f0, f1, f2, f3;
    unsigned int v0123;
    int          color;

    for ( n_packs--; n_packs >= 0; packs++, nindices++, vindices++, n_packs-- )
    {
        mask = 0x80808080;

        colors = (char *)getScratchAddr(8);

        if ( !( packs->tag & 0xFFFF ) && !( *nindices & mask ) ) continue;

        v0123 = *vindices;

        f0 = (uintptr_t)(colors + ((*nindices << 2)  & 0x1FC));
        f1 = (uintptr_t)(colors + ((*nindices >> 6)  & 0x1FC));
        f2 = (uintptr_t)(colors + ((*nindices >> 22) & 0x1FC));
        f3 = (uintptr_t)(colors + ((*nindices >> 14) & 0x1FC));

        if ( v0123 & mask )
        {
#ifdef PORT_BUILD
            /* On 64-bit, the r0/g0/b0/code fields (4 bytes) can't hold a pointer.
               Always use the GTE-computed normal color instead of following the
               indirect pointer. This loses the color override but avoids crashes. */
            *(int *)&packs->r0 = *(int *)f0; v0123 >>= 8;
            *(int *)&packs->r1 = *(int *)f1; v0123 >>= 8;
            *(int *)&packs->r3 = *(int *)f3; v0123 >>= 8;
            *(int *)&packs->r2 = *(int *)f2; v0123 >>= 8;
#else
            if ( v0123 & 0x80 )
            {
                color = **(int **)&packs->r0;
            }
            else
            {
                color = *(int *)f0;
            }
            v0123 >>= 8;
            *(int *)&packs->r0 = color;

            if ( v0123 & 0x80 )
            {
                color = **(int **)&packs->r1;
            }
            else
            {
                color = *(int *)f1;
            }
            v0123 >>= 8;
            *(int *)&packs->r1 = color;

            if ( v0123 & 0x80 )
            {
                color = **(int **)&packs->r3;
            }
            else
            {
                color = *(int *)f3;
            }
            v0123 >>= 8;
            *(int *)&packs->r3 = color;

            if ( v0123 & 0x80 )
            {
                color = **(int **)&packs->r2;
            }
            else
            {
                color = *(int *)f2;
            }
            v0123 >>= 8;
            *(int *)&packs->r2 = color;
#endif

        }
        else
        {
            LCOPY2( (void *)f0, &packs->r0, (void *)f1, &packs->r1 );
            LCOPY2( (void *)f2, &packs->r2, (void *)f3, &packs->r3 );
        }
    }

    return packs;
}

STATIC void DG_ShadeObj( DG_OBJ *obj, int idx )
{
    typedef struct { int d0, d1, d2; } Unit;

    POLY_GT4 *packs;
    DG_MDL   *model;
    Unit     *normals;
    int       n_normals;
    Unit     *scratch;

    for ( packs = obj->packs[ idx ]; obj; obj = obj->extend )
    {
        model = obj->model;

        gte_ldrgb( ( model->flags & DG_MODEL_TRANS ) ?
                   &DG_PacketCode[1] :
                   &DG_PacketCode[0] );

        normals = (Unit *)model->normals;
        n_normals = model->n_normals;

        *(Unit *)getScratchAddr(8) = normals[0];
        *(Unit *)getScratchAddr(11) = normals[1];

        scratch = (Unit *)getScratchAddr(8);
        while ( n_normals > 0 )
        {
            gte_ldv3c( scratch );

            n_normals -= 3;
            normals += 2;

            gte_nct_b();
            scratch++;

            scratch[0] = normals[0];
            scratch[1] = normals[1];

            gte_strgb3( &scratch[-1].d0, &scratch[-1].d1, &scratch[-1].d2 );
        }

        if ( !( model->flags & DG_MODEL_INDIRECT ) )
        {
            packs = DG_ShadePacks( (unsigned int *)model->nindices, packs, obj->n_packs );
        }
        else
        {
            packs = DG_ShadePacksIndirect( (unsigned int *)model->nindices, packs, obj->n_packs, (unsigned int *)model->vindices );
        }
    }
}

void DG_ShadeChanl( DG_CHANL *chanl, int idx )
{
    DG_OBJS **queue;
    int       n_objs;
    DG_OBJS  *objs;
    DG_OBJ   *obj;
    int       n_models;

    queue = chanl->queue;
    for ( n_objs = chanl->objs_index; n_objs > 0 ; n_objs-- )
    {
        objs = *queue++;

        // TODO: figure out the values for bound_mode
        if ( objs->bound_mode == 0 )
        {
            continue;
        }

        if ( !( objs->flag & DG_FLAG_SHADE ) )
        {
            continue;
        }

        gte_SetRotMatrix( objs->light );
        gte_SetColorMatrix( objs->light + 1 );

        if ( objs->flag & DG_FLAG_AMBIENT )
        {
            gte_SetBackColor( objs->light->t[0], objs->light->t[1], objs->light->t[2] );
        }

        obj = objs->objs;
        for ( n_models = objs->n_models; n_models > 0 ; n_models-- )
        {
            if ( obj->bound_mode != 0 )
            {
                DG_MulRotMatrix0( &obj->world, getScratchAddr(0) );
                gte_SetLightMatrix( getScratchAddr(0) );

                DG_ShadeObj( obj, idx );
            }

            obj++;
        }

        if ( objs->flag & DG_FLAG_AMBIENT )
        {
            gte_SetBackColor( DG_Ambient.vx, DG_Ambient.vy, DG_Ambient.vz );
        }
    }
}

void DG_ShadeEnd( void )
{
    /* do nothing */
}
