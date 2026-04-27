/* Renders the loaded stage.
   Walks the map's DG_DEF directly (no actor system, no DG_OBJS, no chanl
   pipeline) and submits one textured triangle per face via gl_submit_tri3d.
   This intentionally bypasses port_RenderChanl — we want unlit, no culling
   beyond the obvious near/off-screen test, and full control of the camera. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "libgte.h"
#include "libgpu.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libdg/gl_renderer.h"
#include "editor.h"

extern void ed_camera_build_eye_inv(DG_CHANL *chanl);

int g_show_walls   = 1;
int g_show_floors  = 1;
int g_show_traps   = 1;
int g_show_cameras = 1;
int g_show_zones   = 0;
int g_show_routes  = 1;
int g_show_actors  = 1;
int g_show_axes    = 1;
int g_show_actor_models    = 0;
int g_show_actor_rotations = 1;
int g_show_trap_labels     = 1;

int g_sel_wall = -1, g_sel_floor = -1, g_sel_trap = -1;
int g_sel_cam  = -1, g_sel_zone  = -1, g_sel_route = -1;

/* Cached per-frame view state, used by ed_actors / ed_hzd to convert world
   coords to eye-space without rebuilding the matrix. */
static MATRIX s_eye_inv;
static short  s_clip_dist;

/* Public so ed_actors / ed_hzd can use it. */
const MATRIX *ed_render_eye_inv(void) { return &s_eye_inv; }
short         ed_render_clip_dist(void) { return s_clip_dist; }

void ed_world_to_eye(const int wx, const int wy, const int wz, int eye[3])
{
    const MATRIX *m = &s_eye_inv;
    eye[0] = (m->m[0][0]*wx + m->m[0][1]*wy + m->m[0][2]*wz) / 4096 + m->t[0];
    eye[1] = (m->m[1][0]*wx + m->m[1][1]*wy + m->m[1][2]*wz) / 4096 + m->t[1];
    eye[2] = (m->m[2][0]*wx + m->m[2][1]*wy + m->m[2][2]*wz) / 4096 + m->t[2];
}

/* World → 0..1 normalized screen. Returns 0 when behind near plane or
   off-screen (caller should skip). Mirrors the math in TRI3D_VS. */
int ed_world_to_screen(int wx, int wy, int wz, float *sx, float *sy)
{
    int eye[3];
    ed_world_to_eye(wx, wy, wz, eye);
    if (eye[2] < 4) return 0;
    float ndc_x = (float)eye[0] * (float)s_clip_dist / (160.0f * (float)eye[2]);
    float ndc_y = -(float)eye[1] * (float)s_clip_dist / (112.0f * (float)eye[2]);
    if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f) return 0;
    *sx = (ndc_x + 1.0f) * 0.5f;
    *sy = (1.0f - ndc_y) * 0.5f;
    return 1;
}

/* 0..1 screen → world-space ray. Inverse of ed_world_to_screen with eye-z
   set to 1.0 for the direction vector. */
extern EdCamera g_cam;
void ed_screen_to_world_ray(float sx, float sy, float origin[3], float dir[3])
{
    float ndc_x = sx * 2.0f - 1.0f;
    float ndc_y = 1.0f - sy * 2.0f;
    float dist  = (float)s_clip_dist;
    if (dist < 1.0f) dist = 1.0f;
    float eye_dx = ndc_x * 160.0f / dist;
    float eye_dy = -ndc_y * 112.0f / dist;
    float eye_dz = 1.0f;
    /* eye_inv rows are world basis vectors (right, down, forward). The
       inverse rotation (eye → world) is the transpose, i.e. the columns. */
    const MATRIX *m = &s_eye_inv;
    dir[0] = (m->m[0][0]*eye_dx + m->m[1][0]*eye_dy + m->m[2][0]*eye_dz) / 4096.0f;
    dir[1] = (m->m[0][1]*eye_dx + m->m[1][1]*eye_dy + m->m[2][1]*eye_dz) / 4096.0f;
    dir[2] = (m->m[0][2]*eye_dx + m->m[1][2]*eye_dy + m->m[2][2]*eye_dz) / 4096.0f;
    origin[0] = g_cam.pos[0];
    origin[1] = g_cam.pos[1];
    origin[2] = g_cam.pos[2];
}

static void render_world_axes(void)
{
    if (!g_show_axes) return;
    const int L = 5000;
    static const unsigned char R[3] = {255,  60,  60};
    static const unsigned char G[3] = { 60, 255,  60};
    static const unsigned char B[3] = { 60, 100, 255};
    int o[3], a[3];
    ed_world_to_eye(0, 0, 0, o);
    ed_world_to_eye(L, 0, 0, a); gl_submit_line3d(o, a, R, R, s_clip_dist);
    ed_world_to_eye(0, L, 0, a); gl_submit_line3d(o, a, G, G, s_clip_dist);
    ed_world_to_eye(0, 0, L, a); gl_submit_line3d(o, a, B, B, s_clip_dist);
}

static void render_kmd_unlit(DG_DEF *def, int dist,
                             int wx, int wy, int wz)
{
    /* Reject NULL and obviously-bogus tiny/unaligned pointers so a stale
     * cache entry can't take down the editor. DG_DEFs come from FS-loaded
     * blobs which are mmap'd — addresses well above the bottom 64 KiB. */
    if (!def || (uintptr_t)def < 0x10000 || ((uintptr_t)def & 3)) return;
    if (def->n_models <= 0 || def->n_models > 256) return;

    const unsigned char white[3] = {128, 128, 128};   /* neutral modulation */
    const int uv_zero[2] = {0, 0};

    for (int mi = 0; mi < def->n_models; mi++) {
        DG_MDL *mdl = &def->model[mi];
        if (!mdl->vertices || !mdl->vindices) continue;

        SVECTOR        *verts     = mdl->vertices;
        unsigned char  *vindices  = mdl->vindices;
        unsigned char  *texcoords = mdl->texcoords;
        unsigned short *materials = mdl->materials;
        int             n_faces   = mdl->n_faces;

        for (int fi = 0; fi < n_faces; fi++) {
            unsigned int vi = ((unsigned int *)vindices)[fi];
            int idx[4] = { vi & 0x7F, (vi >> 8) & 0x7F,
                           (vi >> 16) & 0x7F, (vi >> 24) & 0x7F };
            int eye[4][3];
            for (int k = 0; k < 4; k++) {
                ed_world_to_eye(verts[idx[k]].vx + wx,
                                verts[idx[k]].vy + wy,
                                verts[idx[k]].vz + wz, eye[k]);
            }
            /* Reject if entire face is behind near plane. */
            if (eye[0][2] < 1 && eye[1][2] < 1 && eye[2][2] < 1 && eye[3][2] < 1)
                continue;

            int face_z = (eye[0][2] + eye[1][2] + eye[2][2] + eye[3][2]) / 4;

            unsigned short tpage = 0, clut = 0;
            unsigned short flags = 0;
            int uv_arr[4][2] = {{0,0},{0,0},{0,0},{0,0}};
            int textured = 0;

            if (materials && texcoords && materials[fi]) {
                DG_TEX *tex = DG_GetTexture(materials[fi]);
                if (tex && (tex->w > 0 || tex->h > 0)) {
                    int tw = tex->w + 1, th = tex->h + 1;
                    unsigned char *tc = &texcoords[fi * 8];
                    for (int k = 0; k < 4; k++) {
                        uv_arr[k][0] = ((tc[k*2 + 0] * tw) / 256) + tex->off_x;
                        uv_arr[k][1] = ((tc[k*2 + 1] * th) / 256) + tex->off_y;
                    }
                    tpage = tex->tpage;
                    clut  = tex->clut;
                    int abr = (tex->tpage >> 5) & 3;
                    flags = 1;
                    if (mdl->flags & DG_MODEL_TRANS)
                        flags |= 2 | ((abr & 3) << 2);
                    /* Maps are designed CW in PSX coords; our basis flips
                       handedness, so disable cull for safety on first pass. */
                    flags |= 64;
                    textured = 1;
                }
            }
            if (!textured) {
                /* Untextured: still draw with neutral gray so the face is
                   visible. Force no-cull. */
                flags = 64;
            }

            /* tri 1: 0,1,3 — tri 2: 1,2,3 (KMD quad winding). */
            gl_submit_tri3d(eye[0], eye[1], eye[3],
                            uv_arr[0], uv_arr[1], uv_arr[3],
                            white, white, white,
                            NULL, NULL, NULL, NULL,
                            dist, face_z, tpage, clut, flags);
            gl_submit_tri3d(eye[1], eye[2], eye[3],
                            uv_arr[1], uv_arr[2], uv_arr[3],
                            white, white, white,
                            NULL, NULL, NULL, NULL,
                            dist, face_z, tpage, clut, flags);
        }
    }
}

/* Submit every renderable element of the loaded stage to the GL renderer,
 * using whatever s_eye_inv / s_clip_dist were set by the calling viewport.
 * Shared between the 3D perspective viewport and the Top/Front/Side ortho
 * viewports — only the camera setup differs. */
static void ed_render_scene(void)
{
    /* Map geometry — every KMD the loader classified as map-sized. */
    if (g_stage.loaded) {
        for (int i = 0; i < g_stage.n_map_defs; i++) {
            render_kmd_unlit((DG_DEF *)g_stage.map_defs[i], s_clip_dist, 0, 0, 0);
        }
    }
    /* Optional actor-model render. */
    if (g_show_actor_models) {
        for (int i = 0; i < g_actor_count; i++) {
            EdActor *a = &g_actors[i];
            if (!a->has_pos || !a->kmd_def) continue;
            render_kmd_unlit((DG_DEF *)a->kmd_def, s_clip_dist,
                             a->pos[0], a->pos[1], a->pos[2]);
        }
    }
    render_world_axes();
    if (g_stage.hzd_map)  ed_hzd_render();
    if (g_show_actors)    ed_actors_render();
}

void ed_render_frame(void)
{
    /* 3D perspective viewport. Builds eye_inv via DG_LookAt — matches the
     * in-game camera convention so HZD, actor markers, and pick rays all
     * project through the same transform. */
    extern DG_CHANL DG_Chanls[3];
    DG_CHANL *ch = &DG_Chanls[1];
    ed_camera_build_eye_inv(ch);
    s_eye_inv   = ch->eye_inv;
    s_clip_dist = ch->clip_distance;
    ed_render_scene();
}

/* Ortho viewport render entry point. The renderer must already be bound to
 * the right viewport FBO and have ortho/wireframe modes enabled before this
 * is called. The eye_inv we set here is hand-built (not via DG_LookAt) so
 * we can pin one world axis to "depth" and have screen-X / screen-Y align
 * with the remaining two. */
void ed_render_frame_ortho(EdOrthoCam *cam, int viewport_w, int viewport_h)
{
    ed_camera_ortho_build_eye_inv(cam, &s_eye_inv);
    s_clip_dist = 1024;     /* unused by the ortho shader path; safe default */
    ed_render_scene();
}

/* Demo-playback render: use the engine's runtime camera (driven by the
 * cinema actor) and let the engine's PSX-replica render path push every
 * actor's animated mesh into gl_renderer's tri buffer. The editor's
 * static-map walk runs after so the level geometry shows underneath the
 * demo's character animations.
 *
 * Channel selection: the live game writes DG_Chanls[0]; demo overlays
 * may write 1 or 2. ed_demo.c tracks which channel changed last tick
 * and exposes its index via g_demo_active_chanl, so the camera follows
 * whichever the demo's engine code is driving. */
extern int  GV_Clock;
extern int  g_demo_active_chanl;
extern void port_RenderObjects(int idx);
void ed_render_frame_demo(void)
{
    extern DG_CHANL DG_Chanls[3];
    int ci = g_demo_active_chanl;
    if (ci < 0 || ci > 2) ci = 0;
    DG_CHANL *ch = &DG_Chanls[ci];
    s_eye_inv   = ch->eye_inv;
    s_clip_dist = ch->clip_distance ? ch->clip_distance : 300;

    /* Engine pipeline: this calls gl_renderer_begin_3d() which clears
     * last frame's tris, then walks DG_OBJS submitting each animated
     * model. Snake / DEMODOLLs / EMITTERs all show up here. */
    port_RenderObjects(GV_Clock);

    /* Layer the editor's map + HZD + actor-marker overlays on top so
     * the level geometry is visible behind the demo's characters. */
    ed_render_scene();
}

/* ---------------------------------------------------------------------------
 * AABB sources for "Frame selection" (F key). Selection takes precedence;
 * if nothing is selected we fall back to the union of every map KMD's
 * embedded bbox so the user can quickly re-center on the whole stage.
 * ------------------------------------------------------------------------- */

int ed_compute_selection_aabb(float bmin[3], float bmax[3])
{
    /* Selected actor: pad ±1000 so the cube/model has visible context. */
    if (g_actor_selected >= 0 && g_actor_selected < g_actor_count) {
        EdActor *a = &g_actors[g_actor_selected];
        if (a->has_pos) {
            const float r = 1000.0f;
            for (int k = 0; k < 3; k++) {
                bmin[k] = (float)a->pos[k] - r;
                bmax[k] = (float)a->pos[k] + r;
            }
            return 1;
        }
    }
    /* HZD trap / camera. We only have centers via the inspector list API,
     * so pad to a sensible viewing radius. */
    EdHzdItem item;
    if (g_sel_trap >= 0 && ed_hzd_get_trap(g_sel_trap, &item)) {
        const float r = 1500.0f;
        bmin[0] = item.cx - r; bmax[0] = item.cx + r;
        bmin[1] = item.cy - r; bmax[1] = item.cy + r;
        bmin[2] = item.cz - r; bmax[2] = item.cz + r;
        return 1;
    }
    if (g_sel_cam >= 0 && ed_hzd_get_camera(g_sel_cam, &item)) {
        const float r = 1500.0f;
        bmin[0] = item.cx - r; bmax[0] = item.cx + r;
        bmin[1] = item.cy - r; bmax[1] = item.cy + r;
        bmin[2] = item.cz - r; bmax[2] = item.cz + r;
        return 1;
    }
    return 0;
}

int ed_compute_stage_aabb(float bmin[3], float bmax[3])
{
    int got = 0;
    for (int i = 0; i < g_stage.n_map_defs; i++) {
        DG_DEF *d = (DG_DEF *)g_stage.map_defs[i];
        if (!d) continue;
        if (!got) {
            bmin[0] = (float)d->min.vx; bmax[0] = (float)d->max.vx;
            bmin[1] = (float)d->min.vy; bmax[1] = (float)d->max.vy;
            bmin[2] = (float)d->min.vz; bmax[2] = (float)d->max.vz;
            got = 1;
        } else {
            if ((float)d->min.vx < bmin[0]) bmin[0] = (float)d->min.vx;
            if ((float)d->min.vy < bmin[1]) bmin[1] = (float)d->min.vy;
            if ((float)d->min.vz < bmin[2]) bmin[2] = (float)d->min.vz;
            if ((float)d->max.vx > bmax[0]) bmax[0] = (float)d->max.vx;
            if ((float)d->max.vy > bmax[1]) bmax[1] = (float)d->max.vy;
            if ((float)d->max.vz > bmax[2]) bmax[2] = (float)d->max.vz;
        }
    }
    return got;
}

int ed_compute_active_aabb(float bmin[3], float bmax[3])
{
    if (ed_compute_selection_aabb(bmin, bmax)) return 1;
    return ed_compute_stage_aabb(bmin, bmax);
}

/* ---------------------------------------------------------------------------
 * Diagnostic stats collectors. Walk the live GV heap table, the DG texture
 * cache, and the actor + KMD list to fill out the Info-tab counters. The
 * heap walk mirrors GV_CheckMemorySystem in source/libgv/memory.c (which
 * only printf's; we want the numbers in the inspector). MemorySystems_*
 * lives in source/data/bss.c with external linkage — we just declare it
 * extern here. Same for TexSets in source/libdg/text.c. */

extern GV_HEAP MemorySystems_800AD2F0[GV_MEMORY_MAX];
extern DG_TEX  TexSets[DG_MAX_TEXTURES];

static void collect_one_heap(int which, EdHeapStat *out)
{
    GV_HEAP *heap = &MemorySystems_800AD2F0[which];
    out->total = (int)((char *)heap->end - (char *)heap->start);
    out->used = 0;
    out->freed = 0;
    out->max_free_block = 0;
    out->n_units = heap->used;
    GV_ALLOC *alloc = &heap->units[0];
    for (int i = heap->used; i > 0; i--, alloc++) {
        int size = (int)((char *)alloc[1].start - (char *)alloc[0].start);
        if (alloc->state == GV_ALLOC_STATE_FREE) {
            out->freed += size;
            if (size > out->max_free_block) out->max_free_block = size;
        } else {
            /* USED + VOIDED both count as taken (VOIDED is reserved). */
            out->used += size;
        }
    }
}

void ed_collect_memory_stats(EdMemStats *out)
{
    static const char *names[3] = { "PACKET0", "PACKET1", "NORMAL" };
    out->total = 0; out->used = 0; out->freed = 0;
    for (int i = 0; i < 3; i++) {
        out->heap[i].name = names[i];
        collect_one_heap(i, &out->heap[i]);
        out->total += out->heap[i].total;
        out->used  += out->heap[i].used;
        out->freed += out->heap[i].freed;
    }
}

void ed_collect_texture_stats(EdTexStats *out)
{
    out->n_textures = 0;
    out->n_4bpp = out->n_8bpp = out->n_16bpp = 0;
    out->vram_pixels = 0;
    out->vram_bytes  = 0;
    for (int i = 0; i < DG_MAX_TEXTURES; i++) {
        DG_TEX *t = &TexSets[i];
        if (!t->used) continue;
        out->n_textures++;
        int pixels = ((int)t->w + 1) * ((int)t->h + 1);
        out->vram_pixels += pixels;
        /* PSX tpage bits 7..8: 0 = 4bpp, 1 = 8bpp, 2 = 16bpp. */
        int bpp = (t->tpage >> 7) & 3;
        if (bpp == 0)      { out->n_4bpp++;  out->vram_bytes += pixels / 2; }
        else if (bpp == 1) { out->n_8bpp++;  out->vram_bytes += pixels; }
        else               { out->n_16bpp++; out->vram_bytes += pixels * 2; }
    }
}

void ed_collect_actor_stats(EdActorStats *out)
{
    out->total         = g_actor_count;
    out->with_pos      = 0;
    out->with_model    = 0;
    out->with_rotation = 0;
    out->from_demo     = 0;
    out->from_scen     = 0;
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        if (a->has_pos)   out->with_pos++;
        if (a->kmd_def)   out->with_model++;
        if (a->rot_b >= 0) out->with_rotation++;
        if (a->from_demo) out->from_demo++; else out->from_scen++;
    }
}

void ed_collect_geom_stats(EdGeomStats *out)
{
    out->n_kmds = 0; out->n_models = 0; out->n_faces = 0; out->n_vertices = 0;
    if (!g_stage.loaded) return;
    for (int i = 0; i < g_stage.n_map_defs; i++) {
        DG_DEF *d = (DG_DEF *)g_stage.map_defs[i];
        if (!d) continue;
        out->n_kmds++;
        out->n_models += d->n_models;
        for (int mi = 0; mi < d->n_models; mi++) {
            DG_MDL *mdl = &d->model[mi];
            out->n_faces    += mdl->n_faces;
            out->n_vertices += mdl->n_verts;
        }
    }
}
