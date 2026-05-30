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
#include <SDL.h>

#include "libgte.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libgcl/libgcl.h"
#include "editor.h"
#include "ed_dmo.h"

EdDemoState g_demo_state = ED_DEMO_STOPPED;
int         g_demo_frame = 0;
int         g_demo_loaded = 0;
float       g_demo_cam_pos[3] = {0, 0, 0};
float       g_demo_cam_rot_yaw   = 0.0f;
float       g_demo_cam_rot_pitch = 0.0f;
float       g_demo_cam_rot_roll  = 0.0f;

/* Engine entry points — all linked into the editor via port/obj/. */
extern void GV_ExecActorSystem(void);
extern void GV_DumpActorSystem(void);
extern void GCL_ExecScript(void);
extern void GCL_StartDaemon(void);
extern void GCL_ChangeSenerioCode(int demo_flag);
/* Game-side init pieces. Live game calls all of these via GM_StartDaemon
 * but that also installs the GameWork master actor which drives the
 * full title→stage→play state machine — too much for the editor. We
 * pull the three Init* calls in by hand:
 *   GM_InitArea   — area / region tracking (GM_CurrentMap etc.)
 *   GM_InitChara  — chara-type → factory table (chara/&CINEMA, &DEMODOLL…)
 *   GM_InitScript — registers GCL command table (chara, light, map,
 *                   mesg, delay…). Without this, GCL_ExecScript NULL
 *                   derefs at the first non-builtin directive. */
extern void GM_InitArea(void);
extern void GM_InitChara(void);
extern void GM_InitScript(void);
/* Per-stage prelude (live-game flow: gamed.c:415-417, run by GameWork
 * after the resident cache is dirty-saved, right before GCL_ExecScript).
 * GM_ResetMap initialises GM_CurrentMap / GM_Camera defaults. NewCamera
 * System spawns the camera-driver actor whose Act() calls DG_LookAt
 * on DG_Chanl(0) every frame — without it the runtime camera never
 * updates, even with the demo's CINEMA actor running. */
extern void  GM_ResetMap(void);
extern void *NewCameraSystem(void);
extern int  ed_load_stage(const char *stage_name);
extern EditorStage g_stage;
extern DG_CHANL DG_Chanls[3];
extern int GV_Clock;
extern void DG_LookAt(DG_CHANL *chanl, SVECTOR *eye, SVECTOR *center, int clip_distance);

/* Live diagnostic counters mirrored out of the engine state every tick.
 * Read by the Demo tab so the user can verify (a) actors are actually
 * executing — non-zero counts here mean GV_ExecActorSystem touched
 * them — and (b) which DG_Chanls slot the runtime camera is writing
 * (channel 0 in the live game; demo overlays may use 1 or 2). */
int g_demo_diag_actor_count = 0;
int g_demo_diag_chanl_dirty = 0;     /* bitmask: bit N if DG_Chanls[N].eye_inv changed last tick */
int g_demo_diag_gv_clock    = 0;
int g_demo_diag_game_status = 0;     /* GM_GameStatus snapshot (camera Act gates on >= 0) */
int g_demo_diag_pause_level = 0;     /* GV_PauseLevel — gates the camera helpers */

/* GCL daemon registration is one-shot — calling it twice would push a
 * second 'g' loader. Track here so editor_engine_init can call us once
 * and Play can be a no-op for the daemon side. */
static int s_gcl_daemon_started = 0;

/* Wall-clock anchor for the 30 Hz throttle in ed_demo_tick — set on each
 * fresh Play (and on Stop) so a Resume after a long Pause doesn't burst
 * a backlog of catch-up ticks. Defined here so ed_demo_play can clear
 * it before the throttle helpers below reference it. */
static unsigned int s_demo_last_tick_ms = 0;

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

    /* Register the game's command table + chara-type factories. Without
     * these the demo bytecode's first `light`/`chara`/`map` lookup
     * returns NULL from FindCommand and dereferencing it crashes. */
    GM_InitArea();
    GM_InitChara();
    GM_InitScript();

    s_gcl_daemon_started = 1;
}

/* Locate the demo.gcx blob in the GV cache. The .gcx hash comes from
 * GV_StrCode("demo") = 0xa242, with the 'g' (GCL bytecode) extension
 * encoded as the high byte. Returns NULL if no demo.gcx is loaded for
 * the current stage (custom stages typically have only scenerio.gcx). */
extern CACHE Caches[MAX_CACHES];
static unsigned char *find_demo_blob(void)
{
    /* gv_strcode("demo") = 0xa242. Cache id encoding mirrors GV_CacheID:
     * `name + ((ext - 'a') << 16)`. 'g' - 'a' = 6 → 0x60000 | 0xa242. */
    int target = 0x6a242;
    int n = MAX_CACHES;
    int start = target % n;
    for (int i = 0; i < n; i++) {
        int slot = (start + i) % n;
        CACHE *t = &Caches[slot];
        int cur = t->id & 0xFFFFFF;
        if (cur == 0) return NULL;
        if (cur == target) return (unsigned char *)t->buf;
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
    /* Auto-load a .dmo for this stage on a fresh Play. The catalogue
     * tracks which `decompiled/<stage>/*.gcl` referenced each .dmo, so we
     * can pick the right one without the user opening the DMO tab. If
     * they've explicitly opened a different one (g_dmo_active set), keep
     * theirs. Stages with multiple chained dmos take the first by
     * sector order — chronological play order. */
    if (!g_demo_loaded && !g_dmo_active) {
        char names[8][32];
        int n = ed_dmo_find_for_stage(g_stage.stage_name, 8, names);
        if (n > 0) {
            printf("[demo] stage '%s' references %d dmo(s); auto-loading %s\n",
                   g_stage.stage_name, n, names[0]);
            ed_dmo_open(names[0]);
        } else {
            printf("[demo] stage '%s' has no .dmo references in the catalogue "
                   "— camera will not animate (GCL-scripted only)\n",
                   g_stage.stage_name);
        }
    }
    /* Reset the DMO scrubber to frame 0 on a fresh Play (not on resume
     * from PAUSE) so the cinematic starts at the beginning regardless of
     * where the user left the DMO tab slider. */
    if (!g_demo_loaded && g_dmo_active) g_dmo_active_frame = 0;
    /* Reset the 30 Hz throttle clock so the first tick fires immediately
     * — without this a Resume after a long Pause would burst-tick to
     * catch up to wall-clock. */
    s_demo_last_tick_ms = 0;
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
        /* Per-stage prelude — mirror gamed.c:415-417 (the live game's
         * GameWork actor runs these right before GCL_ExecScript). */
        GM_ResetMap();
        NewCameraSystem();        /* spawns the DG_LookAt-driving actor */

        /* GCL_LoadScript only sets up the proc table + script_body
         * pointer. The actual chara directives at the top level run
         * via GCL_ExecScript, which walks the script body once and
         * spawns every actor (CINEMA, DEMODOLL, EMITTER, …). The live
         * game does this from gamed.c's GameWork actor; we replicate
         * the same call here. Without it, GV_ExecActorSystem ticks
         * but the actor list is empty (only gvd.c is alive). */
        printf("[demo] GCL_LoadScript ok, executing top-level script\n");
        GCL_ExecScript();
        g_demo_loaded = 1;
        g_demo_frame  = 0;
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
    /* Drop the auto-loaded .dmo so the next Play picks the catalogue's
     * default again (matters when the user switched dmos via the dropdown
     * mid-playback — Stop should fully reset the source too). */
    ed_dmo_close();
}

static void ed_demo_tick_one(void);

void ed_demo_step_one(void)
{
    if (!g_demo_loaded) ed_demo_play();
    if (!g_demo_loaded) return;     /* play refused → nothing to step */
    /* Tick once then freeze. Bypass the 30 Hz throttle — Step is meant
     * to advance exactly one engine frame regardless of wall clock. */
    ed_demo_tick_one();
    g_demo_state = ED_DEMO_PAUSED;
}

/* Decompose an eye_inv 3×3 + translation into world-space camera pose.
 * The matrix maps world → eye (eye = m·world + t), so eye-space origin
 * in world is -m^T · t. Z-Y-X Euler angles for human-friendly display.
 * Returns the chosen channel's matrix CRC so callers can detect change. */
static unsigned long camera_pose_from_chanl(DG_CHANL *ch,
                                            float pos[3],
                                            float *yaw, float *pitch, float *roll)
{
    float m[3][3];
    unsigned long crc = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            short v = ch->eye_inv.m[r][c];
            m[r][c] = (float)v / 4096.0f;
            crc = crc * 31u + (unsigned short)v;
        }
    int tx = ch->eye_inv.t[0], ty = ch->eye_inv.t[1], tz = ch->eye_inv.t[2];
    crc = crc * 1009u + (unsigned)tx;
    crc = crc * 1009u + (unsigned)ty;
    crc = crc * 1009u + (unsigned)tz;
    pos[0] = -(m[0][0]*tx + m[1][0]*ty + m[2][0]*tz);
    pos[1] = -(m[0][1]*tx + m[1][1]*ty + m[2][1]*tz);
    pos[2] = -(m[0][2]*tx + m[1][2]*ty + m[2][2]*tz);
    *yaw = atan2f(m[0][2], m[2][2]);
    float p = -m[1][2];
    if (p >  1.0f) p =  1.0f;
    if (p < -1.0f) p = -1.0f;
    *pitch = asinf(p);
    *roll = atan2f(m[1][0], m[1][1]);
    return crc;
}

/* Pick whichever of DG_Chanls[0..2] is "live" — preferring the channel
 * whose matrix changed since the last tick. The runtime's cinema flow
 * writes channel 0 in normal play; some overlays target 1 or 2; the
 * editor itself was using 1 before demo mode. Tracking deltas means we
 * automatically follow whichever channel the demo's engine code is
 * driving without hard-coding an assumption. */
static unsigned long s_chanl_crc[3] = {0,0,0};
int g_demo_active_chanl = 0;          /* 0..2 — exposed so render path matches */

static void update_camera_snapshot(void)
{
    int dirty = 0;
    for (int ci = 0; ci < 3; ci++) {
        float pos[3], y, p, r;
        unsigned long crc = camera_pose_from_chanl(&DG_Chanls[ci], pos, &y, &p, &r);
        if (crc != s_chanl_crc[ci]) {
            dirty |= (1 << ci);
            s_chanl_crc[ci] = crc;
            /* Adopt the channel that just changed as the active one (last
             * write wins — works for a single-camera demo). */
            g_demo_active_chanl = ci;
            g_demo_cam_pos[0]    = pos[0];
            g_demo_cam_pos[1]    = pos[1];
            g_demo_cam_pos[2]    = pos[2];
            g_demo_cam_rot_yaw   = y;
            g_demo_cam_rot_pitch = p;
            g_demo_cam_rot_roll  = r;
        }
    }
    g_demo_diag_chanl_dirty = dirty;
}

/* Walk the actor list to count live actors per level. Reads the same
 * ActorList array GV_DumpActorSystem traverses so the count
 * is always in sync with what the system would print. */
extern AList ActorList[GV_ACTOR_LEVEL];
static int count_active_actors(void)
{
    int total = 0;
    for (int lv = 0; lv < GV_ACTOR_LEVEL; lv++) {
        GV_ACT *a = ActorList[lv].start.next;
        while (a) { if (a->act) total++; a = a->next; }
    }
    return total;
}

void ed_demo_dump_actors(void)
{
    GV_DumpActorSystem();
}

/* Stand in for FrameRunDemo. Drives the cinematic camera from the active
 * .dmo's per-frame eye/center. Auto-advances the frame; clamps at the
 * last record so the camera holds on the final shot rather than wrapping.
 * No-op when no .dmo is open in the DMO inspector — Play still works for
 * GCL-only demos, the camera just won't animate.
 *
 * Writing to `gUnkCameraStruct2` is useless during the actor tick:
 * camera.c::Act() (level 2) calls camera_act_helper4 which overwrites
 * the struct from `GM_Camera` every frame, then calls DG_LookAt with the
 * just-overwritten values. The live game's FrameRunDemo dodges this by
 * computing DG_Chanls[0].eye_inv directly (see source/kojo/demo.c:553).
 *
 * We do the same: call DG_LookAt on chanl 0 after GV_ExecActorSystem so
 * our matrix is the last write before the renderer reads it. */
static void feed_dmo_frame_to_engine(void)
{
    if (!g_dmo_active || g_dmo_active->n_extracted == 0) return;
    if (g_dmo_active_frame < 0)
        g_dmo_active_frame = 0;
    if (g_dmo_active_frame >= g_dmo_active->n_extracted)
        g_dmo_active_frame = g_dmo_active->n_extracted - 1;

    EdDmoFrame *f = &g_dmo_active->frames[g_dmo_active_frame];
    SVECTOR eye    = { f->eye[0],    f->eye[1],    f->eye[2],    0 };
    SVECTOR center = { f->center[0], f->center[1], f->center[2], 0 };
    int clip = f->clip_dist > 0 ? f->clip_dist : 200;
    DG_LookAt(DG_Chanl(0), &eye, &center, clip);

    extern int g_dmo_freeze_frame;
    if (g_dmo_freeze_frame) return;
    if (g_dmo_active_frame < g_dmo_active->n_extracted - 1)
        g_dmo_active_frame++;
    else if (g_dmo_loop)
        g_dmo_active_frame = 0;
}

/* Cap the engine tick rate to PSX-native 30 Hz regardless of how fast the
 * editor's render loop is running. Without this, on a 60+Hz display the
 * cinematic plays 2× too fast and timed cues (CINEMA -t lifetime,
 * audio sync) drift. We accumulate elapsed milliseconds since the last
 * tick and step the actor system once per ~33.3ms slot.
 *
 * The user can override the rate via PORT_DEMO_HZ env var at startup
 * (e.g. PORT_DEMO_HZ=60 to play at 60). 0 falls back to one-tick-per-call
 * so Step still works for single-step debugging. */
static int s_demo_target_hz = -1;

static int demo_target_hz(void)
{
    if (s_demo_target_hz < 0) {
        const char *e = getenv("PORT_DEMO_HZ");
        s_demo_target_hz = (e && *e) ? atoi(e) : 30;
        if (s_demo_target_hz < 0) s_demo_target_hz = 30;
    }
    return s_demo_target_hz;
}

static void ed_demo_tick_one(void)
{
    GV_ExecActorSystem();
    /* Camera write goes AFTER the actor tick so it's the last writer to
     * DG_Chanls[0].eye_inv before render. camera.c::Act() runs inside
     * GV_ExecActorSystem and overwrites the matrix from GM_Camera; we
     * stomp on top with the cinematic eye/center. */
    feed_dmo_frame_to_engine();
    g_demo_frame++;
    update_camera_snapshot();
    g_demo_diag_actor_count = count_active_actors();
    g_demo_diag_gv_clock    = GV_Clock;
    {
        extern int GM_GameStatus;
        extern int GV_PauseLevel;
        g_demo_diag_game_status = GM_GameStatus;
        g_demo_diag_pause_level = GV_PauseLevel;
    }
}

void ed_demo_tick(void)
{
    if (g_demo_state != ED_DEMO_PLAYING) return;

    int hz = demo_target_hz();
    if (hz <= 0) {
        /* Uncapped — one tick per call (useful for Step). */
        ed_demo_tick_one();
        return;
    }

    unsigned int now = SDL_GetTicks();
    if (s_demo_last_tick_ms == 0) s_demo_last_tick_ms = now;
    /* slot_ms = floor(1000 / hz). At 30 Hz that's 33ms; the small drift
     * (1000 - 33*30 = 10ms per second) is below human perception for
     * cutscene playback. */
    unsigned int slot_ms = 1000u / (unsigned)hz;
    /* Cap catch-up to avoid death spirals after a stall (window resize,
     * file load): no more than 4 slots in one render frame. */
    int max_steps = 4;
    while ((now - s_demo_last_tick_ms) >= slot_ms && max_steps-- > 0) {
        ed_demo_tick_one();
        s_demo_last_tick_ms += slot_ms;
    }
}
