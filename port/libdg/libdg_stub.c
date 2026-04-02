/**
 * Port rendering: replaces dgd.c, loader.c, trans.c, sort.c
 * Uses a simple direct renderer instead of the PSX OT pipeline.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "libgte.h"
#include "libgpu.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"

/* Globals */
int DG_FrameRate = 2;
int DG_HikituriFlag = 0;

/* dgd.c — our custom init */
extern int DG_LoadInitPcx(unsigned char *buf, int id);
static int port_dg_safe_loader(unsigned char *buf, int id) { (void)buf; (void)id; return 1; }

void DG_ResetPipeline(void)
{
    /* Only reset channel 0 (background) object queue.
       Channel 1 (3D) keeps its objects across stage resets. */
    extern DG_CHANL DG_Chanls[];
    DG_Chanls[0].objs_index = 0;
    DG_Chanls[0].prim_index = DG_Chanls[0].queue_size;
}
void DG_ResetTextureCache(void)
{
    DG_InitTextureSystem();
    DG_LoadResidentTextureCache();
}

void DG_StartDaemon(void)
{
    printf("dg:");
    GV_InitMemorySystemAll();
    GV_ResetPacketMemory();
    GV_InitMemorySystem(GV_NORMAL_MEMORY, 0, GV_NORMAL_MEMORY_TOP, GV_NORMAL_MEMORY_SIZE);

    DG_InitTextureSystem();
    DG_InitLightSystem();
    DG_InitDispEnv(0, 0, 320, 224, 0);
    DG_InitChanlSystem(320);
    DG_RenderPipeline_Init();

    GV_SetLoader('p', (void *)DG_LoadInitPcx);
    { extern int DG_LoadInitKmd(unsigned char *buf, int id); GV_SetLoader('k', (void *)DG_LoadInitKmd); }
    GV_SetLoader('d', (void *)port_dg_safe_loader);
    { extern int DG_LoadInitNar(unsigned char *, int); GV_SetLoader('n', (void *)DG_LoadInitNar); }
    { extern int DG_LoadInitOar(unsigned char *, int); GV_SetLoader('o', (void *)DG_LoadInitOar); }
    GV_SetLoader('r', (void *)port_dg_safe_loader);
    GV_SetLoader('s', (void *)port_dg_safe_loader);
    GV_SetLoader('l', (void *)port_dg_safe_loader); /* LIT data is POD, raw buffer works */
    { extern int DG_LoadInitImg(unsigned char *, int); GV_SetLoader('i', (void *)DG_LoadInitImg); }
    GV_SetLoader('z', (void *)port_dg_safe_loader);
}

/* loader.c stubs */
/*---------------------------------------------------------------------------*/
/* OAR loader — 64-bit safe version                                          */
/* PSX binary layout (32-bit header):                                        */
/*   [4 bytes: archive_ptr (ignored)] [4 bytes: n_joint] [4 bytes: n_motion] */
/*   [4 bytes: table_ptr (ignored)]   [oarData...]                           */
/* The loader computes archive and table pointers into oarData.              */
/*---------------------------------------------------------------------------*/
int DG_LoadInitOar(unsigned char *buf, int id)
{
    /* PSX DG_OAR binary layout (16-byte header):
       offset 0: [unused/overwritten] (4 bytes)
       offset 4: n_joint             (4 bytes)
       offset 8: n_motion            (4 bytes)
       offset C: [unused/overwritten] (4 bytes)
       offset 10: oarData[]          (table + archive bitstream) */
    uint32_t n_joint  = *(uint32_t *)(buf + 4);
    uint32_t n_motion = *(uint32_t *)(buf + 8);

    /* Sanity check */
    if (n_joint > 64 || n_motion > 256) {
        printf("    [oar] Invalid: n_joint=%d n_motion=%d (id=0x%X)\n", n_joint, n_motion, id);
        return 0;
    }

    unsigned char *data = buf + 16;

    /* Allocate persistent DG_OAR — NOT per-frame memory (GV_NORMAL_MEMORY is cleared every frame) */
    DG_OAR *oar = (DG_OAR *)malloc(sizeof(DG_OAR));
    if (!oar) return 0;

    oar->n_joint = n_joint;
    oar->n_motion = n_motion;
    oar->table = (unsigned short *)data;

    int table_size = ((n_joint + 2) * n_motion) * sizeof(unsigned short);
    oar->archive = (unsigned short *)(data + table_size);

    GV_SetCache(id, oar);
    return 1;
}

/*---------------------------------------------------------------------------*/
/* NAR loader — 64-bit safe version                                          */
/* PSX binary: [4 bytes: offset_to_data] [data...]                           */
/* The offset is relative to the start of the struct.                        */
/*---------------------------------------------------------------------------*/
int DG_LoadInitNar(unsigned char *buf, int id)
{
    /* NAR just has an offset in the first field pointing to data */
    uint32_t offset = *(uint32_t *)(buf + 4); /* second field on PSX */

    /* Allocate a small wrapper */
    typedef struct { unsigned int field0; unsigned char *field1; } NAR_64;
    NAR_64 *nar = (NAR_64 *)GV_AllocMemory(GV_NORMAL_MEMORY, sizeof(NAR_64));
    if (!nar) return 0;

    nar->field0 = *(uint32_t *)buf;
    nar->field1 = buf + offset;

    GV_SetCache(id, nar);
    return 1;
}

int DG_LoadInitImg(unsigned char *buf, int id)
{
    /* PSX DG_IMG binary layout (matches original loader.c:140):
       On PSX, DG_IMG is {u16, u16, u16, u16, u32, u32, u32} = 20 bytes.
       The three u32 fields are offsets from buf start (patched to pointers on PSX). */
    DG_IMG *img = (DG_IMG *)malloc(sizeof(DG_IMG));
    if (!img) return 0;

    img->image_width  = *(unsigned short *)(buf + 0);
    img->image_height = *(unsigned short *)(buf + 2);
    img->tile_width   = *(unsigned short *)(buf + 4);
    img->tile_height  = *(unsigned short *)(buf + 6);

    /* Read 32-bit offsets from PSX struct positions */
    unsigned int tex_off = *(unsigned int *)(buf + 8);
    unsigned int att_off = *(unsigned int *)(buf + 12);
    unsigned int til_off = *(unsigned int *)(buf + 16);

    /* Original loader: ptr = (char*)img + (unsigned int)ptr — offset from struct start */
    img->textures = (unsigned short *)((char *)buf + tex_off);
    img->attribs  = (DG_IMG_ATTRIB *)((char *)buf + att_off);
    img->tilemap  = (unsigned char *)((char *)buf + til_off);

    printf("    [img] id=0x%X: %dx%d tile=%dx%d tex_off=%d att_off=%d til_off=%d\n",
           id, img->image_width, img->image_height, img->tile_width, img->tile_height,
           tex_off, att_off, til_off);

    GV_SetCache(id, img);
    return 1;
}
int DG_LoadInitSgt(unsigned char *buf, int id) { (void)buf; (void)id; return 0; }
int DG_LoadInitLit(unsigned char *buf, int id) { (void)buf; (void)id; return 0; }
int DG_LoadInitKmdar(unsigned char *buf, int id) { (void)buf; (void)id; return 0; }

/*---------------------------------------------------------------------------*/
/* Direct renderer — bypasses PSX OT/GPU pipeline entirely                   */
/*---------------------------------------------------------------------------*/

extern uint16_t vram[512][1024];
extern void draw_flat_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
extern int DG_CurrentGroupID;
extern DG_CHANL DG_Chanls[3];

/* Simple 3x3 matrix * 3-vector multiply + translate (fixed-point 4.12) */
static void mat_transform(MATRIX *m, SVECTOR *in, int *ox, int *oy, int *oz)
{
    int x = in->vx, y = in->vy, z = in->vz;
    *ox = (m->m[0][0]*x + m->m[0][1]*y + m->m[0][2]*z) / 4096 + m->t[0];
    *oy = (m->m[1][0]*x + m->m[1][1]*y + m->m[1][2]*z) / 4096 + m->t[1];
    *oz = (m->m[2][0]*x + m->m[2][1]*y + m->m[2][2]*z) / 4096 + m->t[2];
}

/* Perspective project: world → screen (returns 0 if behind camera) */
static int project(MATRIX *screen, SVECTOR *vert, int dist, int *sx, int *sy, int *sz)
{
    int cx, cy, cz;
    mat_transform(screen, vert, &cx, &cy, &cz);
    if (cz <= 4) return 0; /* behind camera or too close */
    *sx = (cx * dist) / cz;
    *sy = (cy * dist) / cz;
    *sz = cz;
    return 1;
}

/* Sample a texel from VRAM given tpage, clut, u, v */
uint16_t sample_vram_texel(uint16_t tpage, uint16_t clut, int u, int v)
{
    /* Decode tpage: tp=color mode, base x/y in VRAM */
    int tp = (tpage >> 7) & 0x3;    /* 0=4bit, 1=8bit, 2=16bit */
    int base_x = (tpage & 0xF) * 64;
    int base_y = ((tpage >> 4) & 0x1) * 256;
    if (tpage & 0x800) base_y += 512; /* bit 11 */

    int clut_x = (clut & 0x3F) * 16;
    int clut_y = (clut >> 6) & 0x1FF;

    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (v > 255) v = 255;

    if (tp == 0) { /* 4-bit CLUT */
        if (u > 255) u = 255;
        int vram_x = base_x + u / 4;
        int vram_y = base_y + v;
        if (vram_x >= 1024 || vram_y >= 512) return 0;
        uint16_t texel = vram[vram_y][vram_x];
        int idx = (texel >> ((u & 3) * 4)) & 0xF;
        int cx = clut_x + idx;
        if (cx >= 1024 || clut_y >= 512) return 0;
        return vram[clut_y][cx];
    } else if (tp == 1) { /* 8-bit CLUT */
        if (u > 255) u = 255;
        int vram_x = base_x + u / 2;
        int vram_y = base_y + v;
        if (vram_x >= 1024 || vram_y >= 512) return 0;
        uint16_t texel = vram[vram_y][vram_x];
        int idx = (u & 1) ? ((texel >> 8) & 0xFF) : (texel & 0xFF);
        int cx = clut_x + idx;
        if (cx >= 1024 || clut_y >= 512) return 0;
        return vram[clut_y][cx];
    } else { /* 16-bit direct */
        int vram_x = base_x + u;
        int vram_y = base_y + v;
        if (vram_x >= 1024 || vram_y >= 512) return 0;
        return vram[vram_y][vram_x];
    }
}

/* Multiply two MATRIX: out = a * b (fixed-point 4.12) */
static void mat_mul(MATRIX *a, MATRIX *b, MATRIX *out)
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out->m[r][c] = (
                (int)a->m[r][0] * b->m[0][c] +
                (int)a->m[r][1] * b->m[1][c] +
                (int)a->m[r][2] * b->m[2][c]
            ) / 4096;
        }
    }
    for (int r = 0; r < 3; r++) {
        out->t[r] = (
            (int)a->m[r][0] * b->t[0] +
            (int)a->m[r][1] * b->t[1] +
            (int)a->m[r][2] * b->t[2]
        ) / 4096 + a->t[r];
    }
}

/* Render all queued DG_OBJS directly to VRAM */
static int render_debug = 0;
void port_RenderObjects(int idx)
{
    DG_CHANL *chanl = &DG_Chanls[1];
    int group_id = DG_CurrentGroupID;
    int dist = chanl->clip_distance;
    if (dist <= 0) dist = 256;

    int total_faces = 0;
    int drawn_faces = 0;

    /* Check for DG_PRIM objects in the channel queue */
    int n_prims = chanl->queue_size - chanl->prim_index;
    if (render_debug < 3 && n_prims > 0) {
        printf("[render] %d DG_PRIM objects in queue\n", n_prims);
    }

    /* Clear Z-buffer each frame */
    extern uint16_t port_zbuf[224][320];
    extern uint16_t port_current_z;
    memset(port_zbuf, 0xFF, sizeof(port_zbuf));

    DG_OBJS **queue = (DG_OBJS **)chanl->queue;
    if (chanl->objs_index > 0 || render_debug < 3) {
        printf("[render] chanl1: objs_index=%d\n", chanl->objs_index);
        render_debug++;
    }
    for (int n = chanl->objs_index; n > 0; n--)
    {
        DG_OBJS *objs = *queue++;
        if (!objs || !objs->def) continue;
        if (objs->group_id && !(objs->group_id & group_id)) continue;

        MATRIX *eye = &chanl->eye_inv;
        DG_OBJ *obj = objs->objs;
        int n_models = objs->def->n_models;

        for (int mi = 0; mi < n_models; mi++, obj++)
        {
            if (!obj->model) continue;
            DG_MDL *mdl = obj->model;
            if (!mdl->vertices || !mdl->vindices) continue;

            MATRIX screen_mat;
            MATRIX *world;
            /* Map objects use the parent world matrix (objs->world).
               Animated models (Snake etc) use per-bone obj->world. */
            if (obj->world.m[0][0] || obj->world.m[1][1] || obj->world.m[2][2])
                world = &obj->world;
            else
                world = &objs->world;
            mat_mul(eye, world, &screen_mat);

            SVECTOR *verts = mdl->vertices;
            unsigned char *vindices = mdl->vindices;
            unsigned char *texcoords = mdl->texcoords;
            unsigned short *materials = mdl->materials;
            CVECTOR *rgbs = obj->rgbs;
            int n_faces = mdl->n_faces;

            for (int fi = 0; fi < n_faces; fi++)
            {
                unsigned int vi = ((unsigned int *)vindices)[fi];
                int i0 = (vi >> 0) & 0x7F;
                int i1 = (vi >> 8) & 0x7F;
                int i2 = (vi >> 16) & 0x7F;
                int i3 = (vi >> 24) & 0x7F;

                int sx0, sy0, sz0, sx1, sy1, sz1, sx2, sy2, sz2, sx3, sy3, sz3;

                if (!project(&screen_mat, &verts[i0], dist, &sx0, &sy0, &sz0)) continue;
                if (!project(&screen_mat, &verts[i1], dist, &sx1, &sy1, &sz1)) continue;
                if (!project(&screen_mat, &verts[i2], dist, &sx2, &sy2, &sz2)) continue;
                if (!project(&screen_mat, &verts[i3], dist, &sx3, &sy3, &sz3)) continue;

                total_faces++;

                /* PSX quad vertex layout:
                   v0---v1
                   |    |
                   v3---v2
                   Backface cull uses nclip(v0,v1,v2) = cross product Z */
                int area = (sx1-sx0)*(sy2-sy0) - (sx2-sx0)*(sy1-sy0);
                /* Backface cull */
                if (area < 0 && !(mdl->flags & 0x4)) continue;

                /* Screen → framebuffer (PSX center at 160,112) */
                int fx0 = sx0 + 160, fy0 = sy0 + 112;
                int fx1 = sx1 + 160, fy1 = sy1 + 112;
                int fx2 = sx2 + 160, fy2 = sy2 + 112;
                int fx3 = sx3 + 160, fy3 = sy3 + 112;

                /* Set Z for depth testing */
                int avgz = (sz0 + sz1 + sz2 + sz3) / 4;
                port_current_z = (avgz > 0xFFFF) ? 0xFFFF : (avgz < 0 ? 0 : avgz);

                /* Texture setup */
                extern uint16_t port_tex_tpage, port_tex_clut;
                extern int port_tex_enabled, port_tex_semi_trans, port_tex_abr;
                extern int port_tri_u[3], port_tri_v[3];

                uint16_t color = 0x4210; /* default grey */
                port_tex_enabled = 0;
                port_tex_semi_trans = (mdl->flags & DG_MODEL_TRANS) ? 1 : 0;

                if (materials && texcoords) {
                    unsigned short mat_id = materials[fi];
                    DG_TEX *tex = (mat_id != 0) ? DG_GetTexture(mat_id) : NULL;
                    if (tex && (tex->w > 0 || tex->h > 0)) {
                        unsigned char *tc = &texcoords[fi * 8];
                        int tw = tex->w + 1, th = tex->h + 1;
                        /* Compute UV for all 4 vertices.
                           KMD texcoord order: v0(0,1) v1(2,3) v2(4,5) v3(6,7)
                           (sequential, unlike POLY_GT4 which swaps v2/v3) */
                        int uv[4][2];
                        uv[0][0] = ((tc[0] * tw) / 256) + tex->off_x;
                        uv[0][1] = ((tc[1] * th) / 256) + tex->off_y;
                        uv[1][0] = ((tc[2] * tw) / 256) + tex->off_x;
                        uv[1][1] = ((tc[3] * th) / 256) + tex->off_y;
                        uv[2][0] = ((tc[4] * tw) / 256) + tex->off_x;
                        uv[2][1] = ((tc[5] * th) / 256) + tex->off_y;
                        uv[3][0] = ((tc[6] * tw) / 256) + tex->off_x;
                        uv[3][1] = ((tc[7] * th) / 256) + tex->off_y;

                        port_tex_tpage = tex->tpage;
                        port_tex_clut = tex->clut;
                        port_tex_abr = (tex->tpage >> 5) & 0x3;
                        port_tex_enabled = 1;

                        /* Sample center for fallback flat color */
                        int cu = (uv[0][0]+uv[1][0]+uv[2][0]+uv[3][0])/4;
                        int cv = (uv[0][1]+uv[1][1]+uv[2][1]+uv[3][1])/4;
                        uint16_t texel = sample_vram_texel(tex->tpage, tex->clut, cu, cv);
                        if (texel) color = texel;

                        /* Quad split: (v0,v1,v3) and (v1,v2,v3) — this worked for mesh.
                           UV must match: screen v0→uv0, v1→uv1, v2→uv2, v3→uv3 */
                        port_tri_u[0] = uv[0][0]; port_tri_v[0] = uv[0][1];
                        port_tri_u[1] = uv[1][0]; port_tri_v[1] = uv[1][1];
                        port_tri_u[2] = uv[3][0]; port_tri_v[2] = uv[3][1];
                        draw_flat_tri(fx0, fy0, fx1, fy1, fx3, fy3, color);

                        port_tri_u[0] = uv[1][0]; port_tri_v[0] = uv[1][1];
                        port_tri_u[1] = uv[2][0]; port_tri_v[1] = uv[2][1];
                        port_tri_u[2] = uv[3][0]; port_tri_v[2] = uv[3][1];
                        draw_flat_tri(fx1, fy1, fx2, fy2, fx3, fy3, color);

                        port_tex_enabled = 0;
                    } else {
                        draw_flat_tri(fx0, fy0, fx1, fy1, fx3, fy3, color);
                        draw_flat_tri(fx1, fy1, fx2, fy2, fx3, fy3, color);
                    }
                } else {
                    draw_flat_tri(fx0, fy0, fx1, fy1, fx3, fy3, color);
                    draw_flat_tri(fx1, fy1, fx2, fy2, fx3, fy3, color);
                }
                drawn_faces++;
            }
        }
    }

    if (render_debug < 100) {
        printf("[render] %d faces drawn (%d visible, %d objs)\n",
               drawn_faces, total_faces, chanl->objs_index);
        render_debug++;
    }
}

/*---------------------------------------------------------------------------*/
/* Pipeline stubs — the original pipeline is bypassed                        */
/*---------------------------------------------------------------------------*/

extern unsigned int *ptr_800B1400[256];

/* sort.c — no-op, rendering done by port_RenderObjects */
void DG_SortChanl(DG_CHANL *chanl, int idx)
{
    if (!chanl || !chanl->ot[idx]) return;
    DrawOTag(chanl->ot[idx]); /* still needed for menu/UI primitives */
}

/* trans.c — no-op, rendering done by port_RenderObjects */
void DG_TransStart(void) {}
void DG_TransChanl(DG_CHANL *chanl, int idx) { (void)chanl; (void)idx; }
void DG_TransEnd(void) {}
