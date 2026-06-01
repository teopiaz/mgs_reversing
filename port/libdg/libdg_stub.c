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
#include "libdg/gl_renderer.h"

/* Globals */
int DG_FrameRate = 1;
int DG_HikituriFlag = 0;

/* ---- Lighting debug overrides --------------------------------------------
 * Written by the ImGui "Stage" tab, consumed by port_light_debug_apply()
 * which runs once per frame before rendering. On enable->disable transitions
 * we restore cached originals so toggling off fully reverts the state
 * (the engine typically sets these once at stage load). */
int   port_light_ambient_override = 0;
int   port_light_ambient_rgb[3]   = { 32, 32, 32 };
int   port_light_disable_fixed    = 0;           /* master: zero all groups   */
int   port_light_group_disabled[8]= { 0 };       /* per-group zero-count      */
int   port_light_disable_dynamic  = 0;           /* master: zero both buffers */
int   port_light_dyn_slot_muted[2][8] = { { 0 } };/* per-slot color-zero     */
float port_light_ambient_scale    = 1.0f;        /* cheap brightness knob    */
int   port_force_gouraud_neutral  = 0;           /* skip shade colors, force 128 */
int   port_light_dump_request     = 0;           /* set from ImGui, cleared after dump */

    extern DG_FixedLight   gFixedLights_800B1E08[8];
    extern DG_TmpLightList LightSystems_800B1E48[2];

static int      s_prev_amb_override      = 0;
static SVECTOR  s_cached_ambient;
static int      s_prev_group_disabled[8] = { 0 };
static int      s_cached_group_count[8]  = { 0 };
static float    s_prev_ambient_scale     = 1.0f;
static SVECTOR  s_cached_ambient_scale;

void port_light_debug_apply(void)
{
    /* One-shot global lighting dump (Stage tab button). Runs once at the
       start of a frame; the per-face dump in port_RenderChanl then adds
       per-object samples and finally clears the request. */
    static int s_dump_seen_header = 0;
    if (port_light_dump_request && !s_dump_seen_header) {
        s_dump_seen_header = 1;
        fprintf(stderr, "\n===== MGS lighting dump =====\n");
        fprintf(stderr, "DG_Ambient: (%d, %d, %d)\n",
                DG_Ambient.vx, DG_Ambient.vy, DG_Ambient.vz);
        fprintf(stderr, "DG_LightMatrix (rows = main/sub1/sub2 directions, /4096):\n");
        for (int i = 0; i < 3; i++)
            fprintf(stderr, "  [%d] %+.3f %+.3f %+.3f\n", i,
                    DG_LightMatrix.m[i][0]/4096.0,
                    DG_LightMatrix.m[i][1]/4096.0,
                    DG_LightMatrix.m[i][2]/4096.0);
        fprintf(stderr, "DG_ColorMatrix (cols = main/sub1/sub2, /16 for 0..255):\n");
        for (int r = 0; r < 3; r++)
            fprintf(stderr, "  %c: %d %d %d\n", "RGB"[r],
                    DG_ColorMatrix.m[r][0]/16,
                    DG_ColorMatrix.m[r][1]/16,
                    DG_ColorMatrix.m[r][2]/16);
        int fx = 0;
        for (int g = 0; g < 8; g++) fx += gFixedLights_800B1E08[g].field_0_lightCount;
        fprintf(stderr, "Fixed point lights: total=%d across 8 groups\n", fx);
        fprintf(stderr, "Dynamic lights: buf0=%d buf1=%d\n",
                LightSystems_800B1E48[0].n_lights,
                LightSystems_800B1E48[1].n_lights);
    } else if (!port_light_dump_request) {
        s_dump_seen_header = 0;  /* rearm for next press */
    }

    /* --- Ambient (absolute override OR scale). Override takes precedence. */
    if (port_light_ambient_override) {
        if (!s_prev_amb_override) s_cached_ambient = DG_Ambient;
        DG_Ambient.vx = (short)port_light_ambient_rgb[0];
        DG_Ambient.vy = (short)port_light_ambient_rgb[1];
        DG_Ambient.vz = (short)port_light_ambient_rgb[2];
    } else if (s_prev_amb_override) {
        DG_Ambient = s_cached_ambient;   /* restore on disable edge */
    } else if (port_light_ambient_scale != 1.0f) {
        /* Cheap brightness knob: re-apply each frame. Cache the engine's
           value on entering scale mode. */
        if (s_prev_ambient_scale == 1.0f) s_cached_ambient_scale = DG_Ambient;
        int r = (int)(s_cached_ambient_scale.vx * port_light_ambient_scale);
        int g = (int)(s_cached_ambient_scale.vy * port_light_ambient_scale);
        int b = (int)(s_cached_ambient_scale.vz * port_light_ambient_scale);
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        DG_Ambient.vx = (short)r;
        DG_Ambient.vy = (short)g;
        DG_Ambient.vz = (short)b;
    } else if (s_prev_ambient_scale != 1.0f) {
        DG_Ambient = s_cached_ambient_scale;   /* restore on scale->1.0 edge */
    }
    s_prev_amb_override  = port_light_ambient_override;
    s_prev_ambient_scale = port_light_ambient_scale;

    /* --- Fixed point lights: master OR per-group disable. Cache the
       engine-set count on the enable edge so disabling restores it. */
    for (int g = 0; g < 8; g++) {
        int want = port_light_disable_fixed || port_light_group_disabled[g];
        if (want && !s_prev_group_disabled[g]) {
            s_cached_group_count[g] = gFixedLights_800B1E08[g].field_0_lightCount;
            gFixedLights_800B1E08[g].field_0_lightCount = 0;
        } else if (!want && s_prev_group_disabled[g]) {
            gFixedLights_800B1E08[g].field_0_lightCount = s_cached_group_count[g];
        } else if (want) {
            /* Stay off even if the engine re-set it. */
            gFixedLights_800B1E08[g].field_0_lightCount = 0;
        }
        s_prev_group_disabled[g] = want;
    }

    /* --- Dynamic lights: the engine rebuilds n_lights every tick, so we
       can just clobber them each frame. No caching needed. */
    if (port_light_disable_dynamic) {
        LightSystems_800B1E48[0].n_lights = 0;
        LightSystems_800B1E48[1].n_lights = 0;
    }
    for (int b = 0; b < 2; b++) {
        int n = LightSystems_800B1E48[b].n_lights;
        if (n > 8) n = 8;
        for (int i = 0; i < n; i++) {
            if (port_light_dyn_slot_muted[b][i]) {
                LightSystems_800B1E48[b].lights[i].field_C_color.r = 0;
                LightSystems_800B1E48[b].lights[i].field_C_color.g = 0;
                LightSystems_800B1E48[b].lights[i].field_C_color.b = 0;
            }
        }
    }
}

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
    extern void *port_malloc(size_t size);
    DG_OAR *oar = (DG_OAR *)port_malloc(sizeof(DG_OAR));
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
    extern void *port_malloc(size_t size);
    DG_IMG *img = (DG_IMG *)port_malloc(sizeof(DG_IMG));
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
extern int port_tri_r[3], port_tri_g[3], port_tri_b[3];
extern int DG_CurrentGroupID;
extern DG_CHANL DG_Chanls[3];
extern uint16_t port_zbuf[224][320];
extern uint16_t port_current_z;

/* Simple 3x3 matrix * 3-vector multiply + translate (fixed-point 4.12) */
static void mat_transform(MATRIX *m, SVECTOR *in, int *ox, int *oy, int *oz)
{
    int x = in->vx, y = in->vy, z = in->vz;
    *ox = (m->m[0][0]*x + m->m[0][1]*y + m->m[0][2]*z) / 4096 + m->t[0];
    *oy = (m->m[1][0]*x + m->m[1][1]*y + m->m[1][2]*z) / 4096 + m->t[1];
    *oz = (m->m[2][0]*x + m->m[2][1]*y + m->m[2][2]*z) / 4096 + m->t[2];
}

/* Perspective project: world → screen */
/* Returns the TRUE eye-space z (pre-clamp). *sz still gets the clamped cz so
   existing depth/ordering callers are unaffected; the return lets the software
   path detect verts at/behind the camera (which the clamp would otherwise hide). */
static int project(MATRIX *screen, SVECTOR *vert, int dist, int *sx, int *sy, int *sz)
{
    int cx, cy, cz;
    mat_transform(screen, vert, &cx, &cy, &cz);
    int true_cz = cz;
    if (cz < 4) cz = 4;
    *sx = (int)((long long)cx * dist / cz);
    *sy = (int)((long long)cy * dist / cz);
    *sz = cz;
    return true_cz;
}

/* Multiply two MATRIX: out = a * b (fixed-point 4.12) */
static void mat_mul(MATRIX *a, MATRIX *b, MATRIX *out)
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out->m[r][c] = (short)((
                (int)a->m[r][0] * b->m[0][c] +
                (int)a->m[r][1] * b->m[1][c] +
                (int)a->m[r][2] * b->m[2][c]
            ) / 4096);
        }
    }
    for (int r = 0; r < 3; r++) {
        out->t[r] = (int)(
            (long long)a->m[r][0] * b->t[0] +
            (long long)a->m[r][1] * b->t[1] +
            (long long)a->m[r][2] * b->t[2]
        ) / 4096 + a->t[r];
    }
}

/* Sample a texel from VRAM given tpage, clut, u, v */
uint16_t sample_vram_texel(uint16_t tpage, uint16_t clut, int u, int v)
{
    int tp = (tpage >> 7) & 0x3;
    int base_x = (tpage & 0xF) * 64;
    int base_y = ((tpage >> 4) & 0x1) * 256;
    if (tpage & 0x800) base_y += 512;

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

int port_get_objs_count(void)
{
    return DG_Chanls[1].objs_index;
}

/* Last-frame draw count — read by test_server.c for get_state */
int port_last_drawn_faces = 0;

/*---------------------------------------------------------------------------*/
/* Hybrid renderer: uses old projection for screen XY (known working),       */
/* but reads per-vertex colors from POLY_GT4 packs (shade pipeline output)   */
/* and UVs from packs (opack pipeline output) for correct PSX shading.       */
/*---------------------------------------------------------------------------*/
static int render_debug = 0;
static int port_RenderChanl(DG_CHANL *chanl, int idx, int group_id,
                            DG_OBJS *player_objs, short fp_mode)
{
    int dist = chanl->clip_distance;
    if (dist <= 0) dist = 256;
    int drawn_faces = 0;

    extern uint16_t port_tex_tpage, port_tex_clut;
    extern int port_tex_enabled, port_tex_semi_trans, port_tex_abr;
    extern int port_tri_u[3], port_tri_v[3];

    DG_OBJS **queue = (DG_OBJS **)chanl->queue;
    for (int n = chanl->objs_index; n > 0; n--)
    {
        DG_OBJS *objs = *queue++;
        if (!objs) continue;
        {
            uintptr_t dp = (uintptr_t)objs->def;
            if (!dp || dp < 0x100000 || ((dp & 0xFFFF) == 0x1010) || ((dp >> 32) == 0x01010101)) continue;
        }
        if (objs->def->n_models <= 0 || objs->def->n_models > 256) continue;
        if (!objs->objs) continue;
        if (objs->flag & DG_FLAG_INVISIBLE) continue;
        if (objs->group_id && !(objs->group_id & group_id)) continue;
        if (fp_mode && objs == player_objs) continue;

        DG_OBJ *obj = objs->objs;
        int n_models = objs->def->n_models;

        /* ImGui-button-triggered snake render dump. When the user
           clicks "Dump snake render state" in the Demo tab, this
           fires for the snake-demodoll DG_OBJS (type=9 in d00a,
           positioned around Y=-1055 at f=847) and prints the full
           chain. Filtered by Y range to skip the climbing-pose
           type=6 (Y~-4615) and the static map/walls (Y=0).
           Use n_models=16 as an extra hint (character KMDs have
           16 bones; static props have fewer). */
        {
            extern int port_demo_dump_request;
            int ty = objs->world.t[1];
            int is_snake_doll = (ty < -100 && ty > -2000) && (n_models == 16);
            if (port_demo_dump_request && is_snake_doll)
            {
                fprintf(stderr,
                    "\n========== SNAKE RENDER DUMP ==========\n"
                    "chanl=%p clip_distance=%d\n"
                    "chanl->eye_inv.t=(%d,%d,%d)\n"
                    "chanl->eye_inv.m[0]=(%d,%d,%d)\n"
                    "chanl->eye_inv.m[1]=(%d,%d,%d)\n"
                    "chanl->eye_inv.m[2]=(%d,%d,%d)\n"
                    "objs=%p def=%p flag=0x%X group=0x%X n_models=%d\n"
                    "objs->world.t=(%d,%d,%d)\n"
                    "objs->world.m[0]=(%d,%d,%d)\n"
                    "objs->world.m[1]=(%d,%d,%d)\n"
                    "objs->world.m[2]=(%d,%d,%d)\n",
                    (void*)chanl, chanl->clip_distance,
                    chanl->eye_inv.t[0], chanl->eye_inv.t[1], chanl->eye_inv.t[2],
                    chanl->eye_inv.m[0][0], chanl->eye_inv.m[0][1], chanl->eye_inv.m[0][2],
                    chanl->eye_inv.m[1][0], chanl->eye_inv.m[1][1], chanl->eye_inv.m[1][2],
                    chanl->eye_inv.m[2][0], chanl->eye_inv.m[2][1], chanl->eye_inv.m[2][2],
                    (void*)objs, (void*)objs->def, objs->flag, objs->group_id, n_models,
                    objs->world.t[0], objs->world.t[1], objs->world.t[2],
                    objs->world.m[0][0], objs->world.m[0][1], objs->world.m[0][2],
                    objs->world.m[1][0], objs->world.m[1][1], objs->world.m[1][2],
                    objs->world.m[2][0], objs->world.m[2][1], objs->world.m[2][2]);

                DG_OBJ *dump_obj = objs->objs;
                for (int dmi = 0; dmi < n_models && dmi < 16; dmi++, dump_obj++) {
                    fprintf(stderr,
                        "  bone[%d]: world.t=(%d,%d,%d) "
                        "m[0]=(%d,%d,%d) m[1]=(%d,%d,%d) m[2]=(%d,%d,%d)\n",
                        dmi,
                        dump_obj->world.t[0], dump_obj->world.t[1], dump_obj->world.t[2],
                        dump_obj->world.m[0][0], dump_obj->world.m[0][1], dump_obj->world.m[0][2],
                        dump_obj->world.m[1][0], dump_obj->world.m[1][1], dump_obj->world.m[1][2],
                        dump_obj->world.m[2][0], dump_obj->world.m[2][1], dump_obj->world.m[2][2]);
                }
                fprintf(stderr, "========================================\n\n");
                /* Don't reset yet -- the [eye-vert] dump below fires
                 * for the first face of the first model and we want
                 * it in the same dump. Reset there. */
            }
        }

        /* PORT_DEBUG_SNAKE: dump objs->world.t once per (objs, frame_sec)
           so we can compare actor / map / wall positions between editor
           and port for the d00a cinematic. Each line shows the channel,
           the DG_OBJS pointer, world translation, and the first row of
           the world rotation matrix. */
        {
            static int dbg_world = -1;
            if (dbg_world == -1) {
                const char *e = getenv("PORT_DEBUG_SNAKE");
                dbg_world = (e && atoi(e) > 0) ? 1 : 0;
            }
            if (dbg_world) {
                /* Dump objects with high (climbing) Y plus the chanl
                   clip_distance / eye_inv translation used to project
                   them. Compared to the editor's DG_LookAt + s_clip_dist
                   to see whether the projection differs. */
                int ty = objs->world.t[1];
                if (ty < -4000) {
                    fprintf(stderr,
                        "[objs] world.t=(%d,%d,%d) chanl=%p clip=%d "
                        "eye_inv.t=(%d,%d,%d) m[0]=(%d,%d,%d) m[2]=(%d,%d,%d)\n",
                        objs->world.t[0], objs->world.t[1], objs->world.t[2],
                        (void*)chanl, chanl->clip_distance,
                        chanl->eye_inv.t[0], chanl->eye_inv.t[1], chanl->eye_inv.t[2],
                        chanl->eye_inv.m[0][0], chanl->eye_inv.m[0][1], chanl->eye_inv.m[0][2],
                        chanl->eye_inv.m[2][0], chanl->eye_inv.m[2][1], chanl->eye_inv.m[2][2]);
                }
            }
        }

        for (int mi = 0; mi < n_models; mi++, obj++)
        {
            if (!obj->model) continue;
            DG_MDL *mdl = obj->model;
            if (!mdl->vertices || !mdl->vindices) continue;

            /* Compute screen matrix for projection */
            MATRIX screen_mat;
            {
                MATRIX *world;
                short *m = (short *)obj->world.m;
                int is_zero = 1;
                for (int k = 0; k < 9; k++) { if (m[k]) { is_zero = 0; break; } }
                world = is_zero ? &objs->world : &obj->world;
                mat_mul(&chanl->eye_inv, world, &screen_mat);
                screen_mat.m[1][0] = (screen_mat.m[1][0] * 58) / 64;
                screen_mat.m[1][1] = (screen_mat.m[1][1] * 58) / 64;
                screen_mat.m[1][2] = (screen_mat.m[1][2] * 58) / 64;
                screen_mat.t[1] = (screen_mat.t[1] * 58) / 64;
            }


            /* Color pack pointer — valid if shade pipeline wrote correct colors.
               DG_MODEL_INDIRECT is now handled safely in shade.c (PORT_BUILD). */
            POLY_GT4 *color_packs = ((objs->flag & DG_FLAG_SHADE) && objs->bound_mode && obj->bound_mode)
                                    ? obj->packs[idx] : NULL;

            SVECTOR *verts = mdl->vertices;
            unsigned char *vindices = mdl->vindices;
            unsigned char *texcoords = mdl->texcoords;
            unsigned short *materials = mdl->materials;
            int n_faces = mdl->n_faces;

            for (int fi = 0; fi < n_faces; fi++)
            {
                unsigned int vi = ((unsigned int *)vindices)[fi];
                int i0 = (vi >> 0) & 0x7F;
                int i1 = (vi >> 8) & 0x7F;
                int i2 = (vi >> 16) & 0x7F;
                int i3 = (vi >> 24) & 0x7F;

                int sx0, sy0, sz0, sx1, sy1, sz1, sx2, sy2, sz2, sx3, sy3, sz3;

                int tcz0 = project(&screen_mat, &verts[i0], dist, &sx0, &sy0, &sz0);
                int tcz1 = project(&screen_mat, &verts[i1], dist, &sx1, &sy1, &sz1);
                int tcz2 = project(&screen_mat, &verts[i2], dist, &sx2, &sy2, &sz2);
                int tcz3 = project(&screen_mat, &verts[i3], dist, &sx3, &sy3, &sz3);

                /* Off-screen cull */
                {
                    int minx = sx0, maxx = sx0, miny = sy0, maxy = sy0;
                    if (sx1 < minx) minx = sx1; if (sx1 > maxx) maxx = sx1;
                    if (sx2 < minx) minx = sx2; if (sx2 > maxx) maxx = sx2;
                    if (sx3 < minx) minx = sx3; if (sx3 > maxx) maxx = sx3;
                    if (sy1 < miny) miny = sy1; if (sy1 > maxy) maxy = sy1;
                    if (sy2 < miny) miny = sy2; if (sy2 > maxy) maxy = sy2;
                    if (sy3 < miny) miny = sy3; if (sy3 > maxy) maxy = sy3;
                    if (maxx < -320 || minx > 320 || maxy < -224 || miny > 224) continue;
                }

                /* Near-plane cull: only skip if the WHOLE face is behind the
                   near plane. The previous "any vertex < 32" test was too
                   aggressive -- characters close to camera (Snake) had many
                   faces where one corner dipped below 32 units, killing the
                   triangle and producing visible holes in the mesh. Faces
                   that straddle the near plane are clipped correctly by GL
                   in clip space; the vertex shader clamps cz < 4 to avoid
                   perspective-divide blowup. (Uses the true pre-clamp cz —
                   sz is clamped to >=4 and would never be < 1.) */
                if (tcz0 < 1 && tcz1 < 1 && tcz2 < 1 && tcz3 < 1) continue;

                int gl_on = gl_renderer_enabled();
                extern int gl_debug_no_cull;

                /* Software path (PORT_GL=0) has no near-plane clipping. A vertex
                   at/behind the camera (true cz <= 0) projects via the cz<4 clamp
                   to a bogus on-screen position, splattering the face across the
                   view in first-person. GL clips these in clip space; for the
                   software fallback, drop any face that crosses the camera plane. */
                if (!gl_on && (tcz0 < 1 || tcz1 < 1 || tcz2 < 1 || tcz3 < 1)) continue;
                /* Characters / dynamic props use DG_FLAG_SHADE (per-frame
                   lighting, skeletal animation). Level geometry and static
                   props use DG_FLAG_PAINT (preshaded). Snake has SHADE +
                   IRTEXTURE; map walls have PAINT. Distinguishing by SHADE
                   vs PAINT matches PSX behaviour: only SHADE objects have
                   bones whose transforms can mirror-flip per-frame. */
                int is_character = (objs->flag & DG_FLAG_SHADE) != 0;
                int bothface = (mdl->flags & DG_MODEL_BOTHFACE) != 0;

                /* Dynamic-shadow opt-in bit (port-only). DG_FLAG_SHADOW
                 * on an OBJS marks it as a caster: its tris are pushed
                 * into a separate buffer that the shadow pass renders
                 * into the depth map (gl_renderer.c). Receiver-side
                 * filtering is done in the fragment shader by *excluding*
                 * casters — every non-caster fragment samples the shadow
                 * map, so static geometry, enemies, props all receive
                 * without needing a flag each. */
                int is_shadow_caster = (objs->flag & DG_FLAG_SHADOW) != 0;

                /* Backface cull. GL mode defers to the GPU (GL_CULL_FACE in
                   the 3D pass) which uses exact post-projection winding and
                   handles mirror matrices correctly. Per-tri "no cull" flag
                   (bit 6) bypasses GPU cull for characters and DG_MODEL_BOTHFACE.
                   Software (PORT_GL=0) keeps the CPU NCLIP test. */
                int any_clamped = (sz0 <= 4) || (sz1 <= 4) ||
                                  (sz2 <= 4) || (sz3 <= 4);
                if (!gl_on && !gl_debug_no_cull && !any_clamped) {
                    int area = (sx1-sx0)*(sy2-sy0) - (sx2-sx0)*(sy1-sy0);
                    if (area <= 0) {
                        if (!bothface || area == 0) continue;
                    }
                }

                /* Screen → framebuffer */
                int fx0 = sx0+160, fy0 = sy0+112;
                int fx1 = sx1+160, fy1 = sy1+112;
                int fx2 = sx2+160, fy2 = sy2+112;
                int fx3 = sx3+160, fy3 = sy3+112;

                /* Eye-space positions for the GL path (post eye_inv*world).
                   GL does the perspective divide and depth test on the GPU. */
                int eye[4][3];
                if (gl_on) {
                    mat_transform(&screen_mat, &verts[i0], &eye[0][0], &eye[0][1], &eye[0][2]);
                    mat_transform(&screen_mat, &verts[i1], &eye[1][0], &eye[1][1], &eye[1][2]);
                    mat_transform(&screen_mat, &verts[i2], &eye[2][0], &eye[2][1], &eye[2][2]);
                    mat_transform(&screen_mat, &verts[i3], &eye[3][0], &eye[3][1], &eye[3][2]);
                }

                /* PORT_DEBUG_SNAKE: dump the eye-space coords of the
                   first face of the cinematic snake at the climb peak.
                   This is the LAST measurable point before GL projects
                   to NDC. If editor + port differ at this stage, the
                   divergence is in screen_mat or vert; if they agree,
                   the bug is downstream in the shader / GPU stage. */
                {
                    static int dbg_eye = -1;
                    if (dbg_eye == -1) {
                        const char *e = getenv("PORT_DEBUG_SNAKE");
                        dbg_eye = (e && atoi(e) > 0) ? 1 : 0;
                    }
                    extern int port_demo_dump_request;
                    int do_dump = (dbg_eye || port_demo_dump_request) &&
                                  gl_on && fi == 0 && mi == 0 &&
                                  objs->world.t[1] < -100 &&
                                  objs->world.t[1] > -2000 &&
                                  n_models == 16;
                    if (do_dump) {
                        fprintf(stderr,
                            "[eye-vert] mi=%d fi=%d world.t=(%d,%d,%d) clip=%d\n"
                            "  screen_mat.t=(%d,%d,%d)\n"
                            "  screen_mat.m[0]=(%d,%d,%d)\n"
                            "  screen_mat.m[1]=(%d,%d,%d)\n"
                            "  screen_mat.m[2]=(%d,%d,%d)\n"
                            "  v0=(%d,%d,%d) -> eye=(%d,%d,%d)\n"
                            "  v1=(%d,%d,%d) -> eye=(%d,%d,%d)\n"
                            "  v2=(%d,%d,%d) -> eye=(%d,%d,%d)\n"
                            "  v3=(%d,%d,%d) -> eye=(%d,%d,%d)\n",
                            mi, fi,
                            objs->world.t[0], objs->world.t[1], objs->world.t[2],
                            chanl->clip_distance,
                            screen_mat.t[0], screen_mat.t[1], screen_mat.t[2],
                            screen_mat.m[0][0], screen_mat.m[0][1], screen_mat.m[0][2],
                            screen_mat.m[1][0], screen_mat.m[1][1], screen_mat.m[1][2],
                            screen_mat.m[2][0], screen_mat.m[2][1], screen_mat.m[2][2],
                            verts[i0].vx, verts[i0].vy, verts[i0].vz,
                            eye[0][0], eye[0][1], eye[0][2],
                            verts[i1].vx, verts[i1].vy, verts[i1].vz,
                            eye[1][0], eye[1][1], eye[1][2],
                            verts[i2].vx, verts[i2].vy, verts[i2].vz,
                            eye[2][0], eye[2][1], eye[2][2],
                            verts[i3].vx, verts[i3].vy, verts[i3].vz,
                            eye[3][0], eye[3][1], eye[3][2]);
                        /* One-shot: clear the request flag after the
                           first model's first face is dumped (the
                           button trigger). dbg_eye-driven dumps keep
                           firing per-frame as before. */
                        if (port_demo_dump_request) port_demo_dump_request = 0;
                    }
                }

                /* Per-pixel lighting: look up per-vertex normals + per-DG_OBJS
                   light matrices. Opt-in via the ImGui toggle (defaults off --
                   Gouraud is the baseline since the PSX scaling on the NCS
                   formula isn't perfectly calibrated yet). Only valid for
                   DG_FLAG_SHADE objects (level geometry lit by directional
                   lights). DG_FLAG_PAINT actors (preshaded characters with
                   dynamic point lights) always stay on the Gouraud / obj->rgbs
                   path -- their lighting can't be replicated from directional
                   matrices alone. */
                const short *nvecs[4] = {0,0,0,0};
                GLLight gl_light = {0};
                unsigned short light_flag_bit = 0;
                extern int imgui_per_pixel_light;
                if (gl_on && imgui_per_pixel_light &&
                    (objs->flag & DG_FLAG_SHADE) && objs->light &&
                    mdl->normals && mdl->nindices)
                {
                    unsigned int ni = ((unsigned int *)mdl->nindices)[fi];
                    int ni0 = (ni >> 0)  & 0x7F;
                    int ni1 = (ni >> 8)  & 0x7F;
                    int ni2 = (ni >> 16) & 0x7F;
                    int ni3 = (ni >> 24) & 0x7F;
                    nvecs[0] = (const short *)&mdl->normals[ni0];
                    nvecs[1] = (const short *)&mdl->normals[ni1];
                    nvecs[2] = (const short *)&mdl->normals[ni2];
                    nvecs[3] = (const short *)&mdl->normals[ni3];
                    gl_light.light_dir   = (const short *)&objs->light[0].m[0][0];
                    gl_light.light_color = (const short *)&objs->light[1].m[0][0];
                    if (objs->flag & DG_FLAG_AMBIENT)
                        gl_light.ambient = (const int *)&objs->light[0].t[0];
                    light_flag_bit = 16;   /* flags b4 = per-pixel lit */
                }

                int avgz = (sz0+sz1+sz2+sz3) / 4;
                port_current_z = (avgz > 0xFFFF) ? 0xFFFF : (avgz < 0 ? 0 : avgz);

                /* Per-vertex colors (priority order):
                   1. obj->rgbs + DG_FLAG_PAINT — preshaded (characters with dynamic lights)
                   2. POLY_GT4 packs + DG_FLAG_SHADE — shade pipeline output (level geometry)
                   3. Neutral 128 — fallback (no shading) */
                int cr[4], cg[4], cb[4];
                extern int port_force_gouraud_neutral;
                if (port_force_gouraud_neutral) {
                    cr[0]=cr[1]=cr[2]=cr[3] = 128;
                    cg[0]=cg[1]=cg[2]=cg[3] = 128;
                    cb[0]=cb[1]=cb[2]=cb[3] = 128;
                } else if ((objs->flag & DG_FLAG_PAINT) && obj->rgbs) {
                    /* Preshade: 4 CVECTORs per face in KMD vertex order */
                    CVECTOR *fc = &obj->rgbs[fi * 4];
                    cr[0] = fc[0].r; cg[0] = fc[0].g; cb[0] = fc[0].b;
                    cr[1] = fc[1].r; cg[1] = fc[1].g; cb[1] = fc[1].b;
                    cr[2] = fc[2].r; cg[2] = fc[2].g; cb[2] = fc[2].b;
                    cr[3] = fc[3].r; cg[3] = fc[3].g; cb[3] = fc[3].b;
                } else if (color_packs) {
                    /* Shade pipeline: pack vertex order r0=v0, r1=v1, r2=KMD_v3, r3=KMD_v2 */
                    POLY_GT4 *pk = &color_packs[fi];
                    cr[0] = pk->r0; cg[0] = pk->g0; cb[0] = pk->b0;
                    cr[1] = pk->r1; cg[1] = pk->g1; cb[1] = pk->b1;
                    cr[2] = pk->r3; cg[2] = pk->g3; cb[2] = pk->b3;
                    cr[3] = pk->r2; cg[3] = pk->g2; cb[3] = pk->b2;
                } else {
                    cr[0]=cr[1]=cr[2]=cr[3] = 128;
                    cg[0]=cg[1]=cg[2]=cg[3] = 128;
                    cb[0]=cb[1]=cb[2]=cb[3] = 128;
                }

                /* --- Lighting dump (one-shot). Print the first face we see for
                   each of the 3 object kinds (preshade, shade, fallback) so we
                   can correlate rendered colors with the values flowing into
                   the pixel pipe. Port_light_dump_request is set from ImGui. */
                extern int port_light_dump_request;
                if (port_light_dump_request) {
                    static int  dumped_preshade, dumped_shade, dumped_fallback;
                    const char *kind = NULL;
                    if ((objs->flag & DG_FLAG_PAINT) && obj->rgbs && !dumped_preshade) {
                        kind = "PRESHADE"; dumped_preshade = 1;
                    } else if (color_packs && !dumped_shade) {
                        kind = "SHADE";    dumped_shade = 1;
                    } else if (!(objs->flag & DG_FLAG_PAINT) && !color_packs && !dumped_fallback) {
                        kind = "FALLBACK"; dumped_fallback = 1;
                    }
                    if (kind) {
                        fprintf(stderr,
                            "[lightdump %s] flag=0x%X fi=%d verts rgb: "
                            "v0=(%d,%d,%d) v1=(%d,%d,%d) v2=(%d,%d,%d) v3=(%d,%d,%d)\n",
                            kind, objs->flag, fi,
                            cr[0],cg[0],cb[0], cr[1],cg[1],cb[1],
                            cr[2],cg[2],cb[2], cr[3],cg[3],cb[3]);
                        if (objs->light) {
                            fprintf(stderr,
                                "    objs->light[0] dirs: %d %d %d | %d %d %d | %d %d %d\n",
                                objs->light[0].m[0][0], objs->light[0].m[0][1], objs->light[0].m[0][2],
                                objs->light[0].m[1][0], objs->light[0].m[1][1], objs->light[0].m[1][2],
                                objs->light[0].m[2][0], objs->light[0].m[2][1], objs->light[0].m[2][2]);
                            fprintf(stderr,
                                "    objs->light[0] t[0..2] (ambient if DG_FLAG_AMBIENT): %d %d %d\n",
                                objs->light[0].t[0], objs->light[0].t[1], objs->light[0].t[2]);
                            fprintf(stderr,
                                "    objs->light[1] colors (cols=lights, rows=RGB, /16):\n"
                                "      R: %d %d %d\n      G: %d %d %d\n      B: %d %d %d\n",
                                objs->light[1].m[0][0]/16, objs->light[1].m[0][1]/16, objs->light[1].m[0][2]/16,
                                objs->light[1].m[1][0]/16, objs->light[1].m[1][1]/16, objs->light[1].m[1][2]/16,
                                objs->light[1].m[2][0]/16, objs->light[1].m[2][1]/16, objs->light[1].m[2][2]/16);
                        }
                        if (dumped_preshade && dumped_shade && dumped_fallback)
                            port_light_dump_request = 0;
                    }
                }

                /* Texture UVs from model data (reliable, always available) */
                uint16_t color = 0x4210;
                port_tex_enabled = 0;
                port_tex_semi_trans = (mdl->flags & DG_MODEL_TRANS) ? 1 : 0;

                if (materials && texcoords) {
                    unsigned short mat_id = materials[fi];
                    DG_TEX *tex = (mat_id != 0) ? DG_GetTexture(mat_id) : NULL;
                    if (tex && (tex->w > 0 || tex->h > 0)) {
                        unsigned char *tc = &texcoords[fi * 8];
                        int tw = tex->w + 1, th = tex->h + 1;
                        int uv[4][2];
                        uv[0][0] = ((tc[0]*tw)/256) + tex->off_x;
                        uv[0][1] = ((tc[1]*th)/256) + tex->off_y;
                        uv[1][0] = ((tc[2]*tw)/256) + tex->off_x;
                        uv[1][1] = ((tc[3]*th)/256) + tex->off_y;
                        uv[2][0] = ((tc[4]*tw)/256) + tex->off_x;
                        uv[2][1] = ((tc[5]*th)/256) + tex->off_y;
                        uv[3][0] = ((tc[6]*tw)/256) + tex->off_x;
                        uv[3][1] = ((tc[7]*th)/256) + tex->off_y;

                        port_tex_tpage = tex->tpage;
                        port_tex_clut = tex->clut;
                        port_tex_abr = (tex->tpage >> 5) & 0x3;
                        port_tex_enabled = 1;

                        /* Stealth / Optical Camo (source/equip/kogaku2.c):
                           kogaku2 rewrites each POLY_GT4 pack to (a) point
                           at a framebuffer-region tpage and (b) carry a flat
                           SNAKE_COLOR / NINJA_COLOR on every vertex. The port
                           normally uses tex->{tpage,clut} from the material
                           atlas and per-vertex RGBs from obj->rgbs (DG_FLAG_
                           PAINT preshade), both of which BYPASS kogaku2's
                           pack rewrites. Detect the pack-level fb-region
                           tpage and override both the tpage and the per-
                           vertex color so the FS branch downstream gets
                           the right inputs. The actual UV mapping is done
                           in the FS via gl_FragCoord (it ignores vUV), so
                           we don't bother copying the pack's u/v fields. */
                        {
                            POLY_GT4 *kpk = obj->packs[idx]
                                ? &((POLY_GT4 *)obj->packs[idx])[fi]
                                : NULL;
                            if (kpk) {
                                uint16_t pt = kpk->tpage;
                                int kp_tp = (pt >> 7) & 3;
                                int kp_by = ((pt >> 4) & 1) * 256;
                                int kp_bx = (pt & 0xF) * 64;
                                if (kp_tp == 2 && kp_by == 0 && kp_bx < 640) {
                                    port_tex_tpage = pt;
                                    port_tex_clut  = kpk->clut;
                                    port_tex_abr   = (pt >> 5) & 0x3;
                                    /* kogaku2 writes the same flat color to
                                       r0/r1/r2/r3 so we can copy one to all
                                       four vertices and skip the KMD swap. */
                                    cr[0]=cr[1]=cr[2]=cr[3] = kpk->r0;
                                    cg[0]=cg[1]=cg[2]=cg[3] = kpk->g0;
                                    cb[0]=cb[1]=cb[2]=cb[3] = kpk->b0;
                                }
                            }
                        }

                        int cu = (uv[0][0]+uv[1][0]+uv[2][0]+uv[3][0])/4;
                        int cv = (uv[0][1]+uv[1][1]+uv[2][1]+uv[3][1])/4;
                        uint16_t texel = sample_vram_texel(tex->tpage, tex->clut, cu, cv);
                        if (texel) color = texel;

                        if (gl_on) {
                            /* Shade/preshade per-vertex colors (KMD vertex order).
                               Clamp to 0..255 to defend against any stale pack data
                               from indirect chains the shade pipeline skips. */
                            #define CLAMP255(x) ((unsigned char)((x) < 0 ? 0 : ((x) > 255 ? 255 : (x))))
                            unsigned char ca[3]={CLAMP255(cr[0]),CLAMP255(cg[0]),CLAMP255(cb[0])};
                            unsigned char cb_[3]={CLAMP255(cr[1]),CLAMP255(cg[1]),CLAMP255(cb[1])};
                            unsigned char cc_[3]={CLAMP255(cr[2]),CLAMP255(cg[2]),CLAMP255(cb[2])};
                            unsigned char cd_[3]={CLAMP255(cr[3]),CLAMP255(cg[3]),CLAMP255(cb[3])};
                            int face_z = (sz0+sz1+sz2+sz3)/4;
                            /* flags: b0 textured, b1 semi-trans, b2-3 ABR,
                               b4 per-pixel lit (from light_flag_bit),
                               b6 no-cull (characters/BOTHFACE, read by flush). */
                            unsigned short fflags = 1 | light_flag_bit;
                            if (port_tex_semi_trans) fflags |= 2 | ((port_tex_abr & 3) << 2);
                            if (is_character || bothface || gl_debug_no_cull) fflags |= 64;
                            if (is_shadow_caster)   fflags |= 0x100;
                            const GLLight *lp = light_flag_bit ? &gl_light : NULL;
                            /* tri 1: v0, v1, v3.  tpage/clut come from the
                               port_tex_* vars so the kogaku2 fb-readback
                               override above takes effect; otherwise they
                               equal tex->tpage / tex->clut as before. */
                            gl_submit_tri3d(eye[0], eye[1], eye[3],
                                            uv[0], uv[1], uv[3],
                                            ca, cb_, cd_,
                                            nvecs[0], nvecs[1], nvecs[3], lp,
                                            dist, face_z, port_tex_tpage, port_tex_clut, fflags);
                            /* tri 2: v1, v2, v3 */
                            gl_submit_tri3d(eye[1], eye[2], eye[3],
                                            uv[1], uv[2], uv[3],
                                            cb_, cc_, cd_,
                                            nvecs[1], nvecs[2], nvecs[3], lp,
                                            dist, face_z, port_tex_tpage, port_tex_clut, fflags);
                        } else {
                            /* Tri 1: v0, v1, v3 */
                            port_tri_u[0]=uv[0][0]; port_tri_v[0]=uv[0][1];
                            port_tri_u[1]=uv[1][0]; port_tri_v[1]=uv[1][1];
                            port_tri_u[2]=uv[3][0]; port_tri_v[2]=uv[3][1];
                            port_tri_r[0]=cr[0]; port_tri_g[0]=cg[0]; port_tri_b[0]=cb[0];
                            port_tri_r[1]=cr[1]; port_tri_g[1]=cg[1]; port_tri_b[1]=cb[1];
                            port_tri_r[2]=cr[3]; port_tri_g[2]=cg[3]; port_tri_b[2]=cb[3];
                            draw_flat_tri(fx0,fy0, fx1,fy1, fx3,fy3, color);

                            /* Tri 2: v1, v2, v3 */
                            port_tri_u[0]=uv[1][0]; port_tri_v[0]=uv[1][1];
                            port_tri_u[1]=uv[2][0]; port_tri_v[1]=uv[2][1];
                            port_tri_u[2]=uv[3][0]; port_tri_v[2]=uv[3][1];
                            port_tri_r[0]=cr[1]; port_tri_g[0]=cg[1]; port_tri_b[0]=cb[1];
                            port_tri_r[1]=cr[2]; port_tri_g[1]=cg[2]; port_tri_b[1]=cb[2];
                            port_tri_r[2]=cr[3]; port_tri_g[2]=cg[3]; port_tri_b[2]=cb[3];
                            draw_flat_tri(fx1,fy1, fx2,fy2, fx3,fy3, color);
                        }

                        port_tex_enabled = 0;
                    } else {
                        goto untextured;
                    }
                } else {
                untextured:
                    if (gl_on) {
                        int uv_dummy[2] = {0, 0};
                        unsigned char ca[3]={CLAMP255(cr[0]),CLAMP255(cg[0]),CLAMP255(cb[0])};
                        unsigned char cb_[3]={CLAMP255(cr[1]),CLAMP255(cg[1]),CLAMP255(cb[1])};
                        unsigned char cc_[3]={CLAMP255(cr[2]),CLAMP255(cg[2]),CLAMP255(cb[2])};
                        unsigned char cd_[3]={CLAMP255(cr[3]),CLAMP255(cg[3]),CLAMP255(cb[3])};
                        int face_z = (sz0+sz1+sz2+sz3)/4;
                        unsigned short fflags = light_flag_bit;
                        if (is_character || bothface || gl_debug_no_cull) fflags |= 64;
                        if (is_shadow_caster)   fflags |= 0x100;
                        const GLLight *lp = light_flag_bit ? &gl_light : NULL;
                        gl_submit_tri3d(eye[0], eye[1], eye[3],
                                        uv_dummy, uv_dummy, uv_dummy,
                                        ca, cb_, cd_,
                                        nvecs[0], nvecs[1], nvecs[3], lp,
                                        dist, face_z, 0, 0, fflags);
                        gl_submit_tri3d(eye[1], eye[2], eye[3],
                                        uv_dummy, uv_dummy, uv_dummy,
                                        cb_, cc_, cd_,
                                        nvecs[1], nvecs[2], nvecs[3], lp,
                                        dist, face_z, 0, 0, fflags);
                    } else {
                        port_tri_r[0]=cr[0]; port_tri_g[0]=cg[0]; port_tri_b[0]=cb[0];
                        port_tri_r[1]=cr[1]; port_tri_g[1]=cg[1]; port_tri_b[1]=cb[1];
                        port_tri_r[2]=cr[3]; port_tri_g[2]=cg[3]; port_tri_b[2]=cb[3];
                        draw_flat_tri(fx0,fy0, fx1,fy1, fx3,fy3, color);
                        port_tri_r[0]=cr[1]; port_tri_g[0]=cg[1]; port_tri_b[0]=cb[1];
                        port_tri_r[1]=cr[2]; port_tri_g[1]=cg[2]; port_tri_b[1]=cb[2];
                        port_tri_r[2]=cr[3]; port_tri_g[2]=cg[3]; port_tri_b[2]=cb[3];
                        draw_flat_tri(fx1,fy1, fx2,fy2, fx3,fy3, color);
                    }
                }
                drawn_faces++;
            }
        }
    }

    return drawn_faces;
}

void port_RenderObjects(int idx)
{
    int group_id = DG_CurrentGroupID;

    /* Reset draw area and offset for 3D rendering — the OT walker may have
       set draw_x/draw_y to a non-zero offset via GPU E5 command. for the radar */
    {
        extern int clip_x0, clip_y0, clip_x1, clip_y1;
        extern void port_set_draw_offset(int x, int y);
        clip_x0 = 0; clip_y0 = 0; clip_x1 = 319; clip_y1 = 223;
        port_set_draw_offset(0, 0);
    }

    /* Debug: dump chanl[1] (3D actors) eye_inv + clip_distance every
       frame so editor and standalone game can be compared by matching
       matrices content-wise. PORT_DEBUG_CAM=1 enables. */
    {
        static int cam_debug = -1;
        if (cam_debug == -1) {
            const char *e = getenv("PORT_DEBUG_CAM");
            cam_debug = (e && atoi(e) > 0) ? 1 : 0;
        }
        if (cam_debug) {
            DG_CHANL *ch = &DG_Chanls[1];
            fprintf(stderr,
                "[cam] %5d %5d %5d %d  %5d %5d %5d %d  %5d %5d %5d %d  cd=%d\n",
                ch->eye_inv.m[0][0], ch->eye_inv.m[0][1], ch->eye_inv.m[0][2], ch->eye_inv.t[0],
                ch->eye_inv.m[1][0], ch->eye_inv.m[1][1], ch->eye_inv.m[1][2], ch->eye_inv.t[1],
                ch->eye_inv.m[2][0], ch->eye_inv.m[2][1], ch->eye_inv.m[2][2], ch->eye_inv.t[2],
                ch->clip_distance);
        }
    }

    /* Clear Z-buffer each frame */
    memset(port_zbuf, 0xFF, sizeof(port_zbuf));

    /* Drop last tick's GL 3D triangles. gl_renderer_present does NOT clear,
       so "idle" render frames between game ticks redraw stable content. */
    gl_renderer_begin_3d();

    /* Apply lighting debug overrides (ambient / fixed / dynamic). Must run
       before the shade pipeline so the modified values propagate. */
    port_light_debug_apply();

    /* Skip rendering during stage transitions */
    {
        extern int GM_LoadComplete;
        static int frames_since_load = 999;
        if (GM_LoadComplete <= 0) { frames_since_load = 0; return; }
        if (frames_since_load < 3) { frames_since_load++; return; }
    }

    render_debug++;

    /* Hide Snake's body in first person view (offset 0x22 = GM_CAMERA.first_person) */
    extern void *GM_PlayerBody;
    extern char GM_Camera;  /* raw access */
    DG_OBJS *player_objs = GM_PlayerBody ? *(DG_OBJS **)GM_PlayerBody : NULL;
    short fp_mode = *(short *)((char *)&GM_Camera + 0x22);

    /* Auto-flag the player's OBJS as a shadow caster. DG_FLAG_SHADOW is
     * a port-only extension defined in libdg.h; the GL renderer reads
     * the per-OBJS bit and captures the caster's geometry into a
     * shadow-map depth texture. Receivers are any DG_FLAG_SHADE
     * geometry (level walls / floor / props). Other actors can be
     * tagged by ORing DG_FLAG_SHADOW into their OBJS at spawn time —
     * see also chara_overrides.c if more general opt-in is needed. */
    if (player_objs)
        player_objs->flag |= DG_FLAG_SHADOW;

    /* Run the PSX rendering pipeline stages that populate per-vertex RGBs.
       - DG_BoundChanl sets objs->bound_mode / obj->bound_mode (frustum test).
         Without it they stay 0 and the shade + render both fall through to
         neutral 128 for every scene object.
       - DG_TransChanl (port stub) sets pack->tag |= 1 so DG_ShadePacks
         actually processes every face.
       - DG_ShadeChanl loads each DG_OBJS's light/color matrices into the
         emulated GTE and runs gte_nct_b per normal, writing r0..r3 / g0..g3
         / b0..b3 into the pack. */
    for (int ci = 0; ci < 3; ci++) {
        DG_BoundChanl(&DG_Chanls[ci], idx);
        DG_TransChanl(&DG_Chanls[ci], idx);
        DG_ShadeChanl(&DG_Chanls[ci], idx);
    }

    /* Multi-light shadow view — gather up to the 5 closest point lights
     * to Snake from the engine's lighting tables and push one FROM-light
     * direction per light into the GL backend. */
    if (player_objs) {
        DG_CHANL *mc = &DG_Chanls[1];
        const float K = 1.0f / 4096.0f;
        float sx_w = (float)player_objs->world.t[0];
        float sy_w = (float)player_objs->world.t[1];
        float sz_w = (float)player_objs->world.t[2];

        /* Snake's eye-space position — same math as before; rotation
         * entries are 4.12 fixed-point so divide by 4096. */
        float sxe = K * (mc->eye_inv.m[0][0] * sx_w + mc->eye_inv.m[0][1] * sy_w +
                         mc->eye_inv.m[0][2] * sz_w)            + mc->eye_inv.t[0];
        float sye = K * (mc->eye_inv.m[1][0] * sx_w + mc->eye_inv.m[1][1] * sy_w +
                         mc->eye_inv.m[1][2] * sz_w)            + mc->eye_inv.t[1];
        float sze = K * (mc->eye_inv.m[2][0] * sx_w + mc->eye_inv.m[2][1] * sy_w +
                         mc->eye_inv.m[2][2] * sz_w)            + mc->eye_inv.t[2];

        /* Local helper: transform a world-space direction (no
         * translation, just rotation) to eye space and write into
         * out_eye_dir as a FROM-light vector (negation of the passed
         * TOWARD-light). */
        #define WORLD_TOWARD_TO_EYE_FROM(tx, ty, tz, out)              \
            do {                                                       \
                float _lxw = -(tx), _lyw = -(ty), _lzw = -(tz);        \
                (out)[0] = K * (mc->eye_inv.m[0][0] * _lxw +           \
                                mc->eye_inv.m[0][1] * _lyw +           \
                                mc->eye_inv.m[0][2] * _lzw);           \
                (out)[1] = K * (mc->eye_inv.m[1][0] * _lxw +           \
                                mc->eye_inv.m[1][1] * _lyw +           \
                                mc->eye_inv.m[1][2] * _lzw);           \
                (out)[2] = K * (mc->eye_inv.m[2][0] * _lxw +           \
                                mc->eye_inv.m[2][1] * _lyw +           \
                                mc->eye_inv.m[2][2] * _lzw);           \
            } while (0)

        extern int   port_shadow_light_override;
        extern float port_shadow_light_override_dir[3];

        float light_dirs[5][3];
        float light_weights[5] = {1, 1, 1, 1, 1};
        int   n_lights = 0;

        if (port_shadow_light_override) {
            /* Manual override — single direction taken from imgui. */
            WORLD_TOWARD_TO_EYE_FROM(port_shadow_light_override_dir[0],
                                     port_shadow_light_override_dir[1],
                                     port_shadow_light_override_dir[2],
                                     light_dirs[0]);
            light_weights[0] = 1.0f;
            n_lights = 1;
        } else {
            /* Engine lights — walk gFixedLights + LightSystems, pick
             * the 5 closest to Snake. Each DG_LIT has a world-space
             * `pos` (SVECTOR); the TOWARD-light direction is
             * (light.pos - snake.pos) normalized.
             *
             * For each candidate we compute distance² in world units
             * (avoiding sqrt during the gather), keep a sorted top-5
             * via insertion, then transform the survivors to eye-space
             * FROM-light. */
            extern DG_FixedLight  gFixedLights_800B1E08[8];
            extern DG_TmpLightList LightSystems_800B1E48[2];

            /* Each candidate holds enough to compute its eye-space
             * direction AND its physical falloff weight (brightness ×
             * distance-falloff) after we pick the top 5. d2 is the
             * sort key; brightness / radius drive the weight. */
            struct { float dx, dy, dz, d2; float brightness, radius; } cand[5];
            for (int i = 0; i < 5; i++) cand[i].d2 = 1e30f;
            int n_kept = 0;

            #define INSERT_LIGHT(_dx, _dy, _dz, _d2, _br, _rd)        \
                do {                                                  \
                    if ((_d2) >= cand[4].d2) break;                   \
                    int _ins = 4;                                     \
                    while (_ins > 0 && cand[_ins - 1].d2 > (_d2)) {   \
                        cand[_ins] = cand[_ins - 1];                  \
                        _ins--;                                       \
                    }                                                 \
                    cand[_ins].dx = (_dx);                            \
                    cand[_ins].dy = (_dy);                            \
                    cand[_ins].dz = (_dz);                            \
                    cand[_ins].d2 = (_d2);                            \
                    cand[_ins].brightness = (_br);                    \
                    cand[_ins].radius     = (_rd);                    \
                    if (n_kept < 5) n_kept++;                         \
                } while (0)

            for (int slot = 0; slot < 8; slot++) {
                DG_FixedLight *fl = &gFixedLights_800B1E08[slot];
                if (!fl->field_4_pLights) continue;
                int n = fl->field_0_lightCount;
                if (n < 0) continue;
                if (n > 64) n = 64;     /* defensive: bogus pointer guard */
                for (int j = 0; j < n; j++) {
                    DG_LIT *L = &fl->field_4_pLights[j];
                    float dx = (float)L->pos.vx - sx_w;
                    float dy = (float)L->pos.vy - sy_w;
                    float dz = (float)L->pos.vz - sz_w;
                    float d2 = dx*dx + dy*dy + dz*dz;
                    INSERT_LIGHT(dx, dy, dz, d2,
                                 (float)L->field_8_brightness,
                                 (float)L->field_A_radius);
                }
            }
            for (int sys = 0; sys < 2; sys++) {
                DG_TmpLightList *ls = &LightSystems_800B1E48[sys];
                int n = ls->n_lights;
                if (n < 0) continue;
                if (n > 8) n = 8;
                for (int j = 0; j < n; j++) {
                    DG_LIT *L = &ls->lights[j];
                    float dx = (float)L->pos.vx - sx_w;
                    float dy = (float)L->pos.vy - sy_w;
                    float dz = (float)L->pos.vz - sz_w;
                    float d2 = dx*dx + dy*dy + dz*dz;
                    INSERT_LIGHT(dx, dy, dz, d2,
                                 (float)L->field_8_brightness,
                                 (float)L->field_A_radius);
                }
            }
            #undef INSERT_LIGHT

            /* Transform direction + compute physical weight per light.
             * Linear falloff to radius (clamped 0 when beyond radius
             * or when radius==0). Brightness is u_short in the PSX data,
             * so the raw value is the relative-intensity factor — the
             * FS divides by sum_w so absolute scale doesn't matter. */
            float max_weight = 0.0f;
            for (int i = 0; i < n_kept; i++) {
                WORLD_TOWARD_TO_EYE_FROM(cand[i].dx, cand[i].dy, cand[i].dz,
                                         light_dirs[i]);
                float falloff = 0.0f;
                if (cand[i].radius > 0.5f) {
                    float d = sqrtf(cand[i].d2);
                    float t = d / cand[i].radius;
                    if (t < 1.0f) falloff = 1.0f - t;   /* linear */
                }
                light_weights[i] = cand[i].brightness * falloff;
                if (light_weights[i] > max_weight) max_weight = light_weights[i];
            }
            n_lights = n_kept;

            /* Every gathered light fell off to zero (or no lights in the
             * stage)? Fall back to a sane top-down direction so Snake
             * still gets *some* shadow. */
            if (n_lights == 0 || max_weight <= 0.0f) {
                WORLD_TOWARD_TO_EYE_FROM(0.0f, -1.0f, 0.0f, light_dirs[0]);
                light_weights[0] = 1.0f;
                n_lights = 1;
            }
        }

        gl_renderer_set_shadow_lights(sxe, sye, sze,
                                      light_dirs, light_weights, n_lights);

        #undef WORLD_TOWARD_TO_EYE_FROM
    }

    int drawn_faces = 0;
    /* Render all 3 channels: 0=background, 1=main, 2=overlay */
    for (int ci = 0; ci < 3; ci++)
        drawn_faces += port_RenderChanl(&DG_Chanls[ci], idx, group_id, player_objs, fp_mode);

    port_last_drawn_faces = drawn_faces;
}

/*---------------------------------------------------------------------------*/
/* Pipeline stubs — the original pipeline is bypassed                        */
/*---------------------------------------------------------------------------*/

extern unsigned int *ptr_800B1400[256];

/* sort.c — Sort DG_PRIM packs into the OT, then walk the OT chain.
   DG_PrimChanl (step 4) already projected prim vertices into the POLY_FT4
   packs via GTE emulation. We add those packs to the OT by Z depth so the
   OT walker renders them (e.g. evpanel buttons, shadows, blood effects). */
void DG_SortChanl(DG_CHANL *chanl, int idx)
{
    /* Sort prim packs into the OT (PSX sort.c lines 73-124) */
    int n_prims_queued = chanl->queue_size - chanl->prim_index;
    if (n_prims_queued > 0)
    {
        u_long *ot = chanl->ot[idx] + 1;  /* +1: matches PSX (skips env1 link) */
        DG_PRIM **pqueue = (DG_PRIM **)&chanl->queue[chanl->prim_index];
        int group_id = DG_CurrentGroupID;

        for (int i = 0; i < n_prims_queued; i++)
        {
            DG_PRIM *prim = pqueue[i];
            if (!prim) continue;
            if (prim->type & DG_PRIM_INVISIBLE) continue;
            if (prim->group_id && !(prim->group_id & group_id)) continue;

            int prim_count = prim->prim_count;
            char *pack = (char *)prim->packs[idx];
            int psize = prim->psize;
            int raise = prim->raise;

            while (--prim_count >= 0)
            {
                int z = *(unsigned short *)pack;
                if (z > 0)
                {
                    int ot_idx = z - raise;
                    if (ot_idx < 0) ot_idx = 0;
                    ot_idx >>= 8;
                    addPrim(&ot[ot_idx], pack);
                }
                pack += psize;
            }
        }
    }

    /* OT is walked later by main_game.c's DG_DrawOTag call */
}

/* trans.c stub — set pack tags to non-zero so shade.c processes all faces
   (shade checks tag & 0xFFFF, skips faces where it's 0). */
void DG_TransStart(void) {}
void DG_TransChanl(DG_CHANL *chanl, int idx)
{
    DG_OBJS **queue = chanl->queue;
    for (int n = chanl->objs_index; n > 0; n--)
    {
        DG_OBJS *objs = *queue++;
        if (!objs || objs->n_models <= 0 || objs->bound_mode == 0) continue;

        DG_OBJ *obj = objs->objs;
        for (int mi = objs->n_models; mi > 0; mi--, obj++)
        {
            if (!obj->model || obj->bound_mode == 0) continue;
            POLY_GT4 *pack = obj->packs[idx];
            if (!pack) continue;

            DG_OBJ *cur = obj;
            int safety = 0;
            while (cur && safety++ < 256) {
                uintptr_t p = (uintptr_t)cur;
                if (p < 0x1000 || (p >> 48) != 0) break;
                if (!cur->model || cur->n_packs <= 0 || cur->n_packs > 4096) break;
                for (int fi = 0; fi < cur->n_packs; fi++)
                    pack[fi].tag |= 1;  /* ensure shade processes this face */
                pack += cur->n_packs;
                cur = cur->extend;
            }
        }
    }
}
void DG_TransEnd(void) {}
