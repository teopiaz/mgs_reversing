/**
 * 64-bit safe KMD model loader.
 * The KMD binary format uses 32-bit offsets in pointer fields.
 * On PSX (32-bit), casting the buffer directly as DG_DEF/DG_MDL worked.
 * On 64-bit, we must parse the raw bytes and rebuild the structs.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "libgte.h"
#include "libgpu.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"

/*---------------------------------------------------------------------------*/
/* PSX-layout structures (matches the 32-bit binary format on disc)          */
/*---------------------------------------------------------------------------*/

#pragma pack(push, 1)

typedef struct {
    int32_t         flags;
    int32_t         n_faces;
    DG_VECTOR       min, max, pos;
    int32_t         parent;
    int32_t         extend;
    int32_t         n_verts;
    uint32_t        vertices_off;    /* offset, not pointer */
    uint32_t        vindices_off;
    int32_t         n_normals;
    uint32_t        normals_off;
    uint32_t        nindices_off;
    uint32_t        texcoords_off;
    uint32_t        materials_off;
    int32_t         padding;
} KMD_MDL_RAW;  /* 76 bytes, matches PSX DG_MDL */

typedef struct {
    int32_t         n_visible;
    int32_t         n_models;
    DG_VECTOR       min, max;
    KMD_MDL_RAW     model[0];
} KMD_DEF_RAW;  /* 32 bytes header, matches PSX DG_DEF */

#pragma pack(pop)

/*---------------------------------------------------------------------------*/
/* Convert raw KMD data to proper 64-bit DG_DEF + DG_MDL                    */
/*---------------------------------------------------------------------------*/

int DG_LoadInitKmd(unsigned char *buf, int id)
{
    KMD_DEF_RAW *raw = (KMD_DEF_RAW *)buf;
    int n_models = raw->n_models;

    if (n_models <= 0 || n_models > 256)
    {
        printf("    [kmd] Invalid model count: %d\n", n_models);
        return 0;
    }

    /* Allocate a proper 64-bit DG_DEF with DG_MDL array — must be persistent */
    int alloc_size = sizeof(DG_DEF) + sizeof(DG_MDL) * n_models;
    DG_DEF *def = (DG_DEF *)malloc(alloc_size);
    if (!def)
    {
        printf("    [kmd] Failed to allocate %d bytes\n", alloc_size);
        return 0;
    }

    /* Copy header */
    def->n_visible = raw->n_visible;
    def->n_models = raw->n_models;
    def->min = raw->min;
    def->max = raw->max;

    /* Convert each model */
    for (int i = 0; i < n_models; i++)
    {
        KMD_MDL_RAW *rm = &raw->model[i];
        DG_MDL *mdl = &def->model[i];

        mdl->flags = rm->flags;
        mdl->n_faces = rm->n_faces;
        mdl->min = rm->min;
        mdl->max = rm->max;
        mdl->pos = rm->pos;
        mdl->parent = rm->parent;
        mdl->extend = NULL; /* extend chain not used in file format */
        mdl->n_verts = rm->n_verts;
        mdl->n_normals = rm->n_normals;
        mdl->padding = rm->padding;

        /* Convert 32-bit offsets to 64-bit pointers (relative to raw buffer) */
        mdl->vertices  = rm->vertices_off  ? (SVECTOR *)(buf + rm->vertices_off)  : NULL;
        mdl->vindices  = rm->vindices_off   ? (unsigned char *)(buf + rm->vindices_off) : NULL;
        mdl->normals   = rm->normals_off    ? (SVECTOR *)(buf + rm->normals_off)   : NULL;
        mdl->nindices  = rm->nindices_off   ? (unsigned char *)(buf + rm->nindices_off) : NULL;
        mdl->texcoords = rm->texcoords_off  ? (unsigned char *)(buf + rm->texcoords_off): NULL;
        mdl->materials = rm->materials_off  ? (unsigned short *)(buf + rm->materials_off): NULL;
    }

    /* Store in cache — the DG system will use this def pointer */
    GV_SetCache(id, def);

    /* kmd print silenced */
    return 1;
}
