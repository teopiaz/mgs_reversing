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
    if (!def || def->n_models <= 0 || def->n_models > 256) return;

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

void ed_render_frame(void)
{
    /* 1. Build view matrix via DG_LookAt — sets DG_Chanls[1].eye_inv to the
       same convention the in-game camera uses. We then snapshot it so the
       rest of the editor (HZD, actor markers) projects through the same
       transform without re-deriving it. */
    extern DG_CHANL DG_Chanls[3];
    DG_CHANL *ch = &DG_Chanls[1];
    ed_camera_build_eye_inv(ch);
    s_eye_inv  = ch->eye_inv;
    s_clip_dist = ch->clip_distance;

    /* 2. Map geometry — every KMD the loader classified as map-sized. For
       multi-room stages (e.g. s02a) this is several KMDs; rendering all
       of them at world origin reconstructs the connected level. */
    if (g_stage.loaded) {
        for (int i = 0; i < g_stage.n_map_defs; i++) {
            render_kmd_unlit((DG_DEF *)g_stage.map_defs[i], s_clip_dist, 0, 0, 0);
        }
    }

    /* 3. Optional actor-model render — uses ed_actor_kmd_for_type to look
       up a KMD per actor type. Falls back to cube markers in ed_actors. */
    if (g_show_actor_models) {
        for (int i = 0; i < g_actor_count; i++) {
            EdActor *a = &g_actors[i];
            if (!a->has_pos) continue;
            void *def = NULL;
            if (ed_actor_kmd_for_type(a->type, &def) && def) {
                render_kmd_unlit((DG_DEF *)def, s_clip_dist,
                                 a->pos[0], a->pos[1], a->pos[2]);
            }
        }
    }

    /* 4. World axes gizmo. */
    render_world_axes();

    /* 5. HZD wireframe overlay. */
    if (g_stage.hzd_map) {
        ed_hzd_render();
    }

    /* 6. Actor markers (cubes + rotation arrows). */
    if (g_show_actors) {
        ed_actors_render();
    }
}
