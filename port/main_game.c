/**
 * Port game initialization — replaces source/main/main.c.
 * Follows the EXACT original PSX initialization sequence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <setjmp.h>
#include <SDL.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#include <execinfo.h>
#include <unistd.h>

/* Safety net: catch SIGSEGV/SIGBUS from stale DG_OBJS pointers during
   stage transitions. Root cause: DG_TransChanl (commit 578f7e9d7) iterates
   the channel queue and extend chains, hitting freed/reused memory.
   Proper fix needs DG queue lifecycle tracking; this prevents crashes. */
static sigjmp_buf render_jmp;
static volatile int render_guard_active = 0;
static volatile void *render_fault_addr = NULL;
static volatile int render_fault_sig = 0;
#define RENDER_BT_MAX 24
static void *render_fault_bt[RENDER_BT_MAX];
static volatile int render_fault_bt_n = 0;
static volatile int render_bt_logged = 0;

/* Debug-controlled actor speed (imgui "Game speed" slider). Defined here
   because game_tick reads them; UI lives in port/imgui_debug.cpp. */
int port_actor_speed = 1;   /* +N=Nx fast, 0=paused, -N=1/(N+1)x slow */
int port_actor_step  = 0;   /* set >0 from imgui to advance N actor frames */

static void render_signal_handler(int sig, siginfo_t *info, void *uctx) {
    (void)uctx;
    render_fault_sig = sig;
    render_fault_addr = info ? info->si_addr : NULL;
    if (!render_bt_logged) {
        render_fault_bt_n = backtrace(render_fault_bt, RENDER_BT_MAX);
    }
    if (render_guard_active) siglongjmp(render_jmp, sig);
}

#include "libgv/libgv.h"
#include "libfs/libfs.h"
#include "libdg/libdg.h"
#include "libgcl/libgcl.h"
#include "libhzd/libhzd.h"
#include "memcard/memcard.h"
#include "sound/sd_cli.h"
#include <libspu.h>
#include "game/game.h"
#include "linkvar.h"

/* From port_memory.c */
extern int port_init_memory(void);

void game_init(void)
{
    printf("port: game_init starting\n");

    port_init_memory();

    /* === Original Main() sequence from source/main/main.c === */
    InitGeom();

    printf("mem:");
    memcard_init();

    printf("pad:");
    /* mts_init_controller() — handled by SDL */

    printf("gv:");
    GV_StartDaemon();

    printf("fs:");
    FS_StartDaemon();

    printf("dg:");
    DG_StartDaemon();

    printf("gcl:");
    GCL_StartDaemon();

    printf("hzd:");
    HZD_StartDaemon();

    printf("sound:");
    {
        extern void spu_emu_init(void);
        extern void sd_init(void);
        extern int sd_mem_alloc(void);
        extern volatile int sd_task_status;
        extern void SdInt(void);
        extern void SdMain(void);

        spu_emu_init();
        sd_mem_alloc();  /* Must run before stage loading (allocates wave_header etc.) */
        sd_init();       /* Initialize SPU state */
        sd_task_status = 128;

        /* Start sound tasks. SdInt processes BGM/SE each frame.
           SdMain handles deferred loading (LoadSngData, LoadSeFile).
           Both skip redundant init via port_sd_init_done flag. */
        { extern int port_sd_init_done; port_sd_init_done = 1; }
        mts_sta_tsk(1 /* MTSID_SOUND_INT */, SdInt, NULL);
        /* SdMain not started as task — its ucontext crashes on ARM64 macOS
           (PAC pointer auth issue). Instead, we call its loop body directly
           from the main tick below. */

        /* Set master volume (sd_init sets it to 0) */
        {
            SpuCommonAttr c_attr;
            c_attr.mask = SPU_COMMON_MVOLL | SPU_COMMON_MVOLR;
            c_attr.mvol.left = 0x3FFF;
            c_attr.mvol.right = 0x3FFF;
            SpuSetCommonAttr(&c_attr);
        }
    }

    printf("gm:");

    GM_StartDaemon();

    /* After GM_StartDaemon, the GameWork actor starts the init→title→select flow.
       To skip to a specific stage, set GM_CurrentStageFlag after init GCL runs.
       We do this by scheduling a deferred set via the actor system. */

    printf("start\n");
    fflush(stdout);
}

static int tick_count = 0;

/* Free-fly camera state */
static float cam_x = 0, cam_y = -1500, cam_z = -4000;
static float cam_yaw = 0, cam_pitch = 0.3f;

extern void port_DrawTile(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b);
extern unsigned char port_keys[256];

static void update_camera(void)
{
    float speed = 100.0f;
    float rot_speed = 0.03f;

    if (port_keys[79]) cam_yaw -= rot_speed;
    if (port_keys[80]) cam_yaw += rot_speed;
    if (port_keys[82]) cam_pitch -= rot_speed;
    if (port_keys[81]) cam_pitch += rot_speed;
    if (port_keys[225] || port_keys[229]) speed = 400.0f;

    if (cam_pitch > 1.4f) cam_pitch = 1.4f;
    if (cam_pitch < -1.4f) cam_pitch = -1.4f;

    float fx = sinf(cam_yaw) * cosf(cam_pitch);
    float fy = sinf(cam_pitch);
    float fz = cosf(cam_yaw) * cosf(cam_pitch);
    float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);

    if (port_keys[26]) { cam_x += fx*speed; cam_y += fy*speed; cam_z += fz*speed; }
    if (port_keys[22]) { cam_x -= fx*speed; cam_y -= fy*speed; cam_z -= fz*speed; }
    if (port_keys[4])  { cam_x -= rx*speed; cam_z += rz*speed; }
    if (port_keys[7])  { cam_x += rx*speed; cam_z -= rz*speed; }
    if (port_keys[20]) cam_y -= speed;
    if (port_keys[8])  cam_y += speed;

    extern DG_CHANL DG_Chanls[3];
    DG_CHANL *chanl = &DG_Chanls[1];
    SVECTOR eye = {(short)cam_x, (short)cam_y, (short)cam_z, 0};
    SVECTOR center = {
        (short)(cam_x + fx * 1000),
        (short)(cam_y + fy * 1000),
        (short)(cam_z + fz * 1000), 0
    };
    DG_LookAt(chanl, &eye, &center, 256);
}

void game_tick(void)
{
    extern int GV_Clock;
        extern int DG_CurrentGroupID;
        extern int port_ot_next;
        extern void port_RenderObjects(int idx);

    /* Clear framebuffer — deferred clear from PutDrawEnv handles
       background color during codec/menu mode. Only clear here if
       no deferred clear is pending. */
    {
        extern int deferred_clear;
        if (!deferred_clear) {
            RECT fb = {0, 0, 320, 224};
            ClearImage(&fb, 0, 0, 0);
        }
    }

    DG_CurrentGroupID = 0xFFFFFFFF;
    /* Port: do NOT reset port_ot_next here. OT link tags (e.g. chanl-1/2
       linking into chanl-0's ot[which] at link position) are written at
       DG_SwapFrame time and must remain resolvable for several frames
       until DG_ClearChanlSystem rewrites them. Resetting per-frame made
       old handles point at freshly-registered unrelated objects, which
       silently broke the OT chain (e.g. GAME OVER text invisible). Let
       port_ot_next grow naturally and wrap at PORT_OT_TABLE_SIZE. */

    /* Clear scratchpad each frame — collision stubs don't initialize it properly */
    {
        extern char port_scratchpad[256 * 32 + 1024];
        memset(port_scratchpad, 0, 256 * 32 + 1024);
    }

    /* Track draw state for rendering */
    {
           extern int DG_UnDrawFrameCount;
           extern int DG_HikituriFlag;
           extern int DG_HikituriFlagOld;
           DG_HikituriFlagOld = DG_HikituriFlag;
           DG_HikituriFlag = 0;
    }


    /* Clear STATE_PADRELEASE each frame before pad reading.
       On PSX, actors set this during cutscenes and clear it when done.
       In the port, many sequences don't complete (stubbed actors), leaving
       the flag stuck and blocking ALL pad input including L2/R2 menus.
       Clearing here lets actors re-set it each frame if they're still active. */
    {
           extern int GM_GameStatus;
           GM_GameStatus &= ~STATE_PADRELEASE;
    }
    /* Match original PSX frame order:
       1. DG_ActFirst (DAEMON): swap frame, read pad
       2. Game actors: collision, movement, animation
       3. DG_ActLast (DAEMON2): render pipeline */
    {
        static uint64_t t_swap = 0, t_actors = 0, t_dgrender = 0, t_portrender = 0, t_drawotag = 0;
        static int perf_frames = 0;
        #ifdef __APPLE__
        static mach_timebase_info_data_t tb = {0};
        if (tb.denom == 0) mach_timebase_info(&tb);
        uint64_t ts0 = mach_absolute_time();
        DG_SwapFrame();
        uint64_t ts1 = mach_absolute_time();
        #else
        DG_SwapFrame();
        #endif

        /* Pad update — matches DG_ActFirst in original dgd.c */
        {
              extern void GV_UpdatePadSystem(void);
              extern GV_PAD *GM_CurrentPadData;
              extern GV_PAD  GV_PadData[];
              GV_UpdatePadSystem();
              GM_CurrentPadData = GV_PadData;
              GV_PadData[2] = GV_PadData[0];
              GV_PadData[3] = GV_PadData[1];
        }

        /* Sound update — process SE requests, load files, SPU transfers */
        {
            extern void IntSdMain(void);
            extern void WaveSpuTrans(void);
            extern void StrSpuTrans(void);
            extern void StrFadeInt(void);
            extern int LoadSeFile(void);
            extern int LoadSngData(void);
            extern volatile int se_load_code;
            extern volatile int sng_status;

            /* IntSdMain runs at the PSX SPU IRQ rate (~88 Hz) -- in the
               sound-tick loop below (3 calls × 30 fps game_tick = ~90 Hz).
               Don't add a fourth call here: it pushes the sequencer to
               120 Hz which plays BGM ~36% too fast ("sped up"). */
            WaveSpuTrans();
            /* StrSpuTrans NOT called here — it's StrSpuTransWithNoLoop which
               advances str_status. The sound tick loop below handles it with
               proper gating (only in playback states >= 5). */
            StrFadeInt();
            if (se_load_code) LoadSeFile();
            if (sng_status == 1) {
                if (LoadSngData()) sng_status = 0;
                else sng_status = 2;
            }
            /* Wave file loading (normally in SdMain loop) */
            {
                extern volatile int dword_800BF27C;
                extern int wave_load_code;
                extern int LoadWaveHeader(void);
                if (dword_800BF27C == 1 && wave_load_code) {
                    LoadWaveHeader();
                }
            }
            /* Stream (VOX/BGM) handling (normally in SdMain loop) */
            {
                extern volatile int str_status;
                extern volatile int str_fout_fg;
                extern volatile int dword_800BEFCC;
                extern int StartStream(void);
                extern void sub_800827A4(void);
                extern void KeyOffStr(void);
                extern int dword_800BF1A4;
                extern int dword_800BF270;

                if (str_fout_fg == 1) str_fout_fg = 2;
                if (dword_800BEFCC) { KeyOffStr(); dword_800BEFCC = 0; }

                switch (str_status) {
                case 1:
                    { extern void stream_reset(void); stream_reset(); }
                    if (StartStream()) { str_status = 0; }
                    else {
                        str_status = 2; dword_800BF1A4 = 0;
                        /* Reset str_tick_count after StartStream sets it to -1.
                           On PSX, the SPU IRQ quickly advances to state 4 which
                           sets it to 0. On the port, we do it here so jimctrl
                           (subtitle actor) doesn't return early for 12+ frames. */
                        { extern int str_tick_count; str_tick_count = 0; }
                    }
                    break;
                case 2: case 3: case 4: case 5: case 6:
                    sub_800827A4();
                    break;
                case 7:
                    KeyOffStr();
                    str_status = 0;
                    break;
                }
            }
        }

        /* Actor system + render frame: wrapped in signal handler to catch
           stale DG_OBJS pointer crashes during stage transitions.

           Debug speed control (driven by imgui_debug):
             port_actor_speed > 0  -> run actor system N times per tick (Nx)
             port_actor_speed == 0 -> paused (actors frozen, render continues)
             port_actor_speed < 0  -> run once every (1 + |speed|) ticks
             port_actor_step       -> single-step N frames then re-pause
        */
        #ifdef __APPLE__
        uint64_t ta0 = mach_absolute_time();
        #endif
        {
            extern int port_actor_speed;
            extern int port_actor_step;
            static int s_slow_count = 0;
            int runs = 0;
            if (port_actor_step > 0) {
                runs = port_actor_step;
                port_actor_step = 0;
            } else if (port_actor_speed > 0) {
                runs = port_actor_speed;
                s_slow_count = 0;
            } else if (port_actor_speed < 0) {
                int period = 1 + (-port_actor_speed);  /* 2, 3, ..., 9 */
                if (++s_slow_count >= period) {
                    s_slow_count = 0;
                    runs = 1;
                }
            } /* port_actor_speed == 0 -> runs stays 0 (paused) */

            struct sigaction sa = {.sa_sigaction = render_signal_handler, .sa_flags = SA_SIGINFO};
            struct sigaction old_segv, old_bus;
            sigaction(SIGSEGV, &sa, &old_segv);
            sigaction(SIGBUS, &sa, &old_bus);
            render_guard_active = 1;
            for (int i = 0; i < runs; i++) {
                if (sigsetjmp(render_jmp, 1) == 0) {
                    GV_ExecActorSystem();
                } else {
                    printf("[game] Signal caught in actor system, sig=%d addr=%p\n",
                           render_fault_sig, (void *)render_fault_addr);
                    break;
                }
            }
            render_guard_active = 0;
            sigaction(SIGSEGV, &old_segv, NULL);
            sigaction(SIGBUS, &old_bus, NULL);
        }

        /* Sound driver tick — mirrors SdInt's main loop on PSX. On PSX
           IntSdMain runs from the SPU IRQ handler. sd_main.c configures
           voice 23 to play the 512-byte blank_data region at pitch 0x1000
           (full 44.1 kHz). The IRQ ping-pongs between blank_data_addr
           and +256 -- 256 bytes = 16 ADPCM blocks = 448 samples, so the
           SPU IRQ period is 448 / 44100 s = 10.16 ms => 98.44 Hz.
           Drive the port off a real-time accumulator at the same rate so
           the music sequencer tempo matches PSX. Override via PORT_SD_HZ
           for A/B testing. */
        {
            extern void IntSdMain(void);
            extern void StrFadeInt(void);
            extern void WaveSpuTrans(void);
            extern void StrSpuTrans(void);
            extern long SpuIsTransferCompleted(long flag);

            static double sd_target_hz = 0.0;
            static double sd_accumulator_ms = 0.0;
            static Uint64 sd_prev_counter = 0;
            static Uint64 sd_freq = 0;
            if (sd_target_hz == 0.0) {
                const char *e = getenv("PORT_SD_HZ");
                /* 44100 / 448 = 98.4375 -- the exact PSX IRQ rate for MGS. */
                sd_target_hz = (e && *e) ? atof(e) : (44100.0 / 448.0);
                if (sd_target_hz < 1.0)   sd_target_hz = 1.0;
                if (sd_target_hz > 500.0) sd_target_hz = 500.0;
                sd_freq = SDL_GetPerformanceFrequency();
                sd_prev_counter = SDL_GetPerformanceCounter();
                printf("[sd] sequencer cadence = %.4f Hz (PSX SPU IRQ rate)\n", sd_target_hz);
            }
            Uint64 now = SDL_GetPerformanceCounter();
            sd_accumulator_ms += (double)(now - sd_prev_counter) * 1000.0 / (double)sd_freq;
            sd_prev_counter = now;
            /* Cap accumulator to one game frame (33 ms) so a freeze doesn't
               cause a burst of catch-up ticks that audibly skips music. */
            if (sd_accumulator_ms > 33.0) sd_accumulator_ms = 33.0;

            double tick_ms = 1000.0 / sd_target_hz;
            int n_ticks = 0;
            while (sd_accumulator_ms >= tick_ms && n_ticks < 8) {
                sd_accumulator_ms -= tick_ms;
                n_ticks++;
            }
            for (int _st = 0; _st < n_ticks; _st++) {
                IntSdMain();
                if (SpuIsTransferCompleted(0) == 1)
                    WaveSpuTrans();
                StrFadeInt();
                /* Call StrSpuTrans during playback (str_status >= 5) to
                   continuously feed audio data. During setup (states 2-4),
                   the stream block's sub_800827A4 handles data loading once
                   per frame. StrSpuTrans during setup consumes blocks too fast. */
                {
                    extern volatile int str_status;
                    if (str_status >= 5 && SpuIsTransferCompleted(0) == 1) {
                        extern void StrSpuTrans(void);
                        StrSpuTrans();
                    }
                }
                {
                    extern void UserSpuIRQProc(void);
                    UserSpuIRQProc();
                }

                /* Override dword_800BF270 based on stream PCM read cursor.
                   On PSX, UserSpuIRQProc increments this at ~88Hz in sync with
                   SPU hardware. In the port, the stream PCM bypass reads decoded
                   audio directly — voice 21 doesn't advance cur_addr. Instead,
                   derive the position from stream_pcm_rd, converting PCM samples
                   back to ADPCM byte position (28 samples = 16 bytes). */
                {
                    extern volatile int str_status;
                    extern int dword_800BF270;
                    extern volatile int stream_pcm_rd;
                    if (str_status >= 5) {
                        /* Convert PCM sample position to ADPCM byte position.
                           28 PCM samples = 16 ADPCM bytes. Wrap at 0x2000 (8KB). */
                        int adpcm_pos = (stream_pcm_rd / 28) * 16;
                        dword_800BF270 = adpcm_pos & 0x1FFF;
                    }
                }
            }
        }

        /* SdMain loop body — deferred sound loading (replaces SdMain task).
           On PSX this runs as a MTS task; on port we call it inline to avoid
           ucontext crash on ARM64 macOS. */
        {
            extern volatile int sng_status;
            extern volatile int se_load_code;
            extern int LoadSngData(void);
            extern int LoadSeFile(void);

            if (sng_status == 1) {
                if (LoadSngData())
                    sng_status = 0;
                else
                    sng_status = 2;
            }
            if (se_load_code) {
                LoadSeFile();
            }
        }

        /* Tick MTS cooperative scheduler — runs one frame of any active tasks
           (codec, save, sound). Tasks yield at mts_slp_tsk/mts_wait_vbl. */
        {
            extern void mts_scheduler_tick(void);
            mts_scheduler_tick();
        }

        #ifdef __APPLE__
        uint64_t ta1 = mach_absolute_time();
        #endif

        /* OT render pipeline — must run BEFORE 3D so the OT clear happens
           before actors added prims. DG_SwapFrame draws the PREVIOUS frame's
           OT (1-GV_Clock) and clears the CURRENT OT (GV_Clock). Then actors
           already added prims to the cleared OT in GV_ExecActorSystem above. */
        {
            struct sigaction sa = {.sa_sigaction = render_signal_handler, .sa_flags = SA_SIGINFO};
            struct sigaction old_segv, old_bus;
            sigaction(SIGSEGV, &sa, &old_segv);
            sigaction(SIGBUS, &sa, &old_bus);
            render_guard_active = 1;
            if (sigsetjmp(render_jmp, 1) == 0) {
                DG_RenderFrame();
            } else {
                printf("[game] Signal caught in render frame, sig=%d addr=%p\n",
                       render_fault_sig, (void *)render_fault_addr);
                if (!render_bt_logged && render_fault_bt_n > 0) {
                    backtrace_symbols_fd(render_fault_bt, render_fault_bt_n, fileno(stdout));
                    render_bt_logged = 1;
                }
            }
            render_guard_active = 0;
            sigaction(SIGSEGV, &old_segv, NULL);
            sigaction(SIGBUS, &old_bus, NULL);
        }

        /* Direct 3D renderer — skip when DG_UnDrawFrameCount > 0,
           and also skip when camera hasn't been initialized yet (port fix:
           cam eye == snake pos means zone camera hasn't set proper offset). */
        {
            extern int DG_FrameRate;
            extern int DG_UnDrawFrameCount;
            int skip_render = (DG_FrameRate == 2) || (DG_UnDrawFrameCount > 0);

            if (!skip_render) {
                /* Apply deferred clear BEFORE 3D rendering, not during
                   DG_DrawOTag (which runs after and would erase 3D). */
                extern void port_apply_deferred_clear(void);
                port_apply_deferred_clear();
                port_RenderObjects(GV_Clock);
            }
        }
        #ifdef __APPLE__
        uint64_t ta2 = mach_absolute_time();
        #endif

        /* Draw the CURRENT frame's OT on top of 3D. This renders subtitles,
           HUD, and other 2D prims that actors just added to OT[GV_Clock].
           On PSX this happens at the NEXT VSync; on the port we do it now
           so prims don't get cleared before being drawn. */
        {
            extern void DG_DrawOTag(int which);
            DG_DrawOTag(GV_Clock);
        }

        #ifdef __APPLE__
        uint64_t ta3 = mach_absolute_time();
        #endif

        #ifdef __APPLE__
        t_swap += ts1 - ts0;
        t_actors += ta1 - ta0;
        t_dgrender += ta2 - ta1;
        t_portrender += ta3 - ta2;
        perf_frames++;
    #ifdef PORT_BUILD_VERBOSE
        if (perf_frames == 60) {
            double ns = (double)tb.numer / (double)tb.denom;
            printf("[tick-perf] swap=%.2fms actors=%.2fms DG_Render=%.2fms port_Render=%.2fms\n",
               (double)t_swap * ns / 1e6 / 60.0,
               (double)t_actors * ns / 1e6 / 60.0,
               (double)t_dgrender * ns / 1e6 / 60.0,
               (double)t_portrender * ns / 1e6 / 60.0);
            t_swap = t_actors = t_dgrender = t_portrender = 0;
            perf_frames = 0;
        }
    #endif
        #endif
    }

    /* Debug: print pad state every second */
    if ((tick_count % 60) == 0) {
        extern int GM_GameStatus;
        extern long mts_PadRead(int);
        long raw = mts_PadRead(0);
        printf("[tick %d] pad=0x%lX gs=0x%X hp=%d/%d item=%d snake=(%d,%d,%d)\n",
               tick_count,
               (unsigned long)GV_PadData[0].status,
               GM_GameStatus,
               GM_SnakeCurrentHealth, GM_SnakeMaxHealth, GM_CurrentItemId,
               GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);
    }

    /* GV_Clock is toggled by the GV daemon actor (gvd.c) inside
       GV_ExecActorSystem — do NOT toggle it here or it double-flips. */
    tick_count++;
}
