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

static void render_kmd_unlit(DG_DEF *def, int dist)
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
                ed_world_to_eye(verts[idx[k]].vx, verts[idx[k]].vy,
                                verts[idx[k]].vz, eye[k]);
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

    /* 2. Map model. */
    if (g_stage.loaded && g_stage.map_def) {
        render_kmd_unlit((DG_DEF *)g_stage.map_def, s_clip_dist);
    }

    /* 3. HZD wireframe overlay. */
    if (g_stage.hzd_map) {
        ed_hzd_render();
    }

    /* 4. Actor markers. */
    if (g_show_actors) {
        ed_actors_render();
    }
}
