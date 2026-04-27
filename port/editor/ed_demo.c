/* Demo (cutscene) playback driver for the editor.
 *
 * Embeds the engine's actor system inside the editor binary so the user
 * can hit Play on a stage's demo.gcx and watch it tick frame-by-frame
 * without launching ./mgs.
 *
 * Architecture: the editor binary already links every libgv / libgcl /
 * libdg / game-actor object (port/editor/Makefile pulls all of port/obj/
 * except a handful of replaced files). All we need to do is kick the
 * GCL daemon, hand-load the demo.gcx blob from cache, and call
 * GV_ExecActorSystem() each editor frame the user has set state = PLAY.
 *
 * What this DOES populate:
 *   - GCL state (current_script.proc_table / script_body)
 *   - Actor system (every chara directive in demo.gcl spawns its actor)
 *   - DG_Chanls[1].eye_inv (the runtime camera the demo's CINEMA actor
 *     drives). Snapshot exposed via g_demo_cam_pos / g_demo_cam_rot_*.
 *
 * What it does NOT do:
 *   - Render the demo's visuals — the editor's existing render path
 *     (ed_render_frame) keeps drawing the static map; we only mirror
 *     the engine's camera state into editor variables so the user can
 *     SEE where the demo camera is at any moment.
 *   - Reset cleanly between Play sessions yet — Stop reloads the stage,
 *     which is heavy but reliable.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libgte.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libgcl/libgcl.h"
#include "editor.h"

EdDemoState g_demo_state = ED_DEMO_STOPPED;
int         g_demo_frame = 0;
int         g_demo_loaded = 0;
float       g_demo_cam_pos[3] = {0, 0, 0};
float       g_demo_cam_rot_yaw   = 0.0f;
float       g_demo_cam_rot_pitch = 0.0f;
float       g_demo_cam_rot_roll  = 0.0f;

/* Engine entry points — all linked into the editor via port/obj/. */
extern void GV_ExecActorSystem(void);
extern int  GCL_LoadScript(unsigned char *datatop);
extern void GCL_StartDaemon(void);
extern void GCL_ChangeSenerioCode(int demo_flag);
extern int  ed_load_stage(const char *stage_name);
extern EditorStage g_stage;
extern DG_CHANL DG_Chanls[3];

/* GCL daemon registration is one-shot — calling it twice would push a
 * second 'g' loader. Track here so editor_engine_init can call us once
 * and Play can be a no-op for the daemon side. */
static int s_gcl_daemon_started = 0;

void ed_demo_engine_init(void)
{
    if (s_gcl_daemon_started) return;
    GCL_StartDaemon();
    /* Pin scenerio_code to a hash that no real script uses (0xFFFF is a
     * sentinel — neither scenerio.gcx (0xea54) nor demo.gcx (0xa242)
     * matches it). The GCL's `g` loader skips scripts whose id != the
     * sentinel, so subsequent stage loads in the editor won't auto-run
     * any GCL — Play takes explicit responsibility for picking demo. */
    extern int scenerio_code;
    scenerio_code = 0xFFFF;
    s_gcl_daemon_started = 1;
}

/* Locate the demo.gcx blob in the GV cache. The .gcx hash comes from
 * GV_StrCode("demo") = 0xa242, with the 'g' (GCL bytecode) extension
 * encoded as the high byte. Returns NULL if no demo.gcx is loaded for
 * the current stage (custom stages typically have only scenerio.gcx). */
extern GV_CACHE_PAGE GV_CacheSystem;
static unsigned char *find_demo_blob(void)
{
    /* gv_strcode("demo") = 0xa242. Cache id encoding mirrors GV_CacheID:
     * `name + ((ext - 'a') << 16)`. 'g' - 'a' = 6 → 0x60000 | 0xa242. */
    int target = 0x6a242;
    int n = MAX_CACHE_TAGS;
    int start = target % n;
    for (int i = 0; i < n; i++) {
        int slot = (start + i) % n;
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[slot];
        int cur = t->id & 0xFFFFFF;
        if (cur == 0) return NULL;
        if (cur == target) return (unsigned char *)t->ptr;
    }
    return NULL;
}

void ed_demo_play(void)
{
    if (g_demo_state == ED_DEMO_PLAYING) return;
    if (!g_stage.loaded) {
        printf("[demo] no stage loaded — cannot play\n");
        return;
    }
    /* First Play after stage load: bring the demo's bytecode into the GCL
     * runtime. Subsequent Play after Pause is just a state flip. */
    if (!g_demo_loaded) {
        unsigned char *blob = find_demo_blob();
        if (!blob) {
            printf("[demo] stage '%s' has no demo.gcx in cache\n",
                   g_stage.stage_name);
            return;
        }
        ed_demo_engine_init();
        if (GCL_LoadScript(blob) != 0) {
            printf("[demo] GCL_LoadScript refused the blob\n");
            return;
        }
        g_demo_loaded = 1;
        g_demo_frame  = 0;
        printf("[demo] loaded demo.gcx for '%s' (%p)\n",
               g_stage.stage_name, (void *)blob);
    }
    g_demo_state = ED_DEMO_PLAYING;
}

void ed_demo_pause(void)
{
    if (g_demo_state == ED_DEMO_PLAYING) g_demo_state = ED_DEMO_PAUSED;
}

void ed_demo_stop(void)
{
    if (g_demo_state == ED_DEMO_STOPPED && !g_demo_loaded) return;
    /* Reload the stage to nuke the actor system + any lingering GCL
     * pointers. ed_load_stage already calls GV_InitMemorySystem on the
     * NORMAL pool, GV_InitCacheSystem, etc., so this drops every actor
     * the demo spawned. */
    char name[16];
    snprintf(name, sizeof(name), "%s", g_stage.stage_name);
    ed_load_stage(name);
    g_demo_state  = ED_DEMO_STOPPED;
    g_demo_loaded = 0;
    g_demo_frame  = 0;
    g_demo_cam_pos[0] = g_demo_cam_pos[1] = g_demo_cam_pos[2] = 0.0f;
    g_demo_cam_rot_yaw = g_demo_cam_rot_pitch = g_demo_cam_rot_roll = 0.0f;
}

void ed_demo_step_one(void)
{
    if (!g_demo_loaded) ed_demo_play();
    if (!g_demo_loaded) return;     /* play refused → nothing to step */
    /* Tick once then freeze. */
    g_demo_state = ED_DEMO_PLAYING;
    ed_demo_tick();
    g_demo_state = ED_DEMO_PAUSED;
}

/* Decompose the runtime camera's eye_inv 3×3 matrix into Z-Y-X Euler
 * angles (yaw / pitch / roll), matching the convention the engine's
 * camera setup uses. The translation column gives the eye position
 * directly; we negate so the displayed value is "where the camera is",
 * not "where it was translated from". Eye_inv rotation rows are unit
 * vectors in 4.12 fixed-point (×4096), so we work in floats. */
static void update_camera_snapshot(void)
{
    DG_CHANL *ch = &DG_Chanls[1];
    /* `t` is the translation that takes a world point into eye space:
     * eye = m * world + t. Eye-space origin in world = -m^T * t. */
    float m[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r][c] = (float)ch->eye_inv.m[r][c] / 4096.0f;
    float t[3] = {
        (float)ch->eye_inv.t[0],
        (float)ch->eye_inv.t[1],
        (float)ch->eye_inv.t[2],
    };
    /* world_origin = -m^T * t (transpose because m is the rotation that
     * maps world → eye, and we want eye → world to recover the camera's
     * world position). */
    g_demo_cam_pos[0] = -(m[0][0]*t[0] + m[1][0]*t[1] + m[2][0]*t[2]);
    g_demo_cam_pos[1] = -(m[0][1]*t[0] + m[1][1]*t[1] + m[2][1]*t[2]);
    g_demo_cam_pos[2] = -(m[0][2]*t[0] + m[1][2]*t[1] + m[2][2]*t[2]);
    /* Z-Y-X Euler from a row-major rotation:
     *   yaw   = atan2( m02, m22)        // around Y
     *   pitch = asin (-m12)             // around X
     *   roll  = atan2( m10, m11)        // around Z   */
    g_demo_cam_rot_yaw   = atan2f(m[0][2], m[2][2]);
    /* clamp arg to asin so floating-point drift doesn't NaN it. */
    float p = -m[1][2];
    if (p >  1.0f) p =  1.0f;
    if (p < -1.0f) p = -1.0f;
    g_demo_cam_rot_pitch = asinf(p);
    g_demo_cam_rot_roll  = atan2f(m[1][0], m[1][1]);
}

void ed_demo_tick(void)
{
    if (g_demo_state != ED_DEMO_PLAYING) return;
    GV_ExecActorSystem();
    g_demo_frame++;
    update_camera_snapshot();
}
