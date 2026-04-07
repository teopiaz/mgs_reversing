/**
 * Port game initialization — replaces source/main/main.c.
 * Follows the EXACT original PSX initialization sequence.
 */
#include <stdio.h>
#include <math.h>
#include <mach/mach_time.h>
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
    port_ot_next = 0;

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
        /* Don't force DG_UnDrawFrameCount to 0 — it's a countdown used by
           cutscene camera cuts (pad_demo.c) and scene transitions. DG_SwapFrame
           decrements it naturally. Forcing it to 0 breaks cutscene flow. */
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
        static mach_timebase_info_data_t tb = {0};
        if (tb.denom == 0) mach_timebase_info(&tb);

        uint64_t ts0 = mach_absolute_time();
        DG_SwapFrame();
        uint64_t ts1 = mach_absolute_time();

        /* Pad update — matches DG_ActFirst in original dgd.c */
        {
            extern void GV_UpdatePadSystem(void);
            extern GV_PAD *GM_CurrentPadData;
            extern GV_PAD  GV_PadData[];
            GV_UpdatePadSystem();
            GM_CurrentPadData = GV_PadData;

            /* The menu system reads from GM_CurrentPadData[2] (GV_PadData[2]).
               The pad update loop only populates indices 0 and 1.
               Copy pad[0] to pad[2] and pad[3] so menus and codec see input. */
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

            /* Call IntSdMain once per frame. On PSX it runs at vsync (~60Hz).
               The sequence engine advances ngc by 1 per call. */
            IntSdMain();
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

                if (str_fout_fg == 1) str_fout_fg = 2;
                if (dword_800BEFCC) { KeyOffStr(); dword_800BEFCC = 0; }

                { static int last_ss = -1; if (str_status != last_ss) { printf("[str] status %d→%d (before switch)\n", last_ss, str_status); last_ss = str_status; } }
                switch (str_status) {
                case 1:
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
                    { extern int str_unplay_size; int ss_before = str_status;
                      sub_800827A4();
                      if (str_status != ss_before)
                          printf("[str] sub_800827A4: %d→%d unplay=%d\n", ss_before, str_status, str_unplay_size);
                    }
                    break;
                case 7:
                    KeyOffStr();
                    str_status = 0;
                    break;
                }
            }
        }

        /* Actor system: game logic, collision, movement */
        uint64_t ta0 = mach_absolute_time();
        GV_ExecActorSystem();

        /* Sound driver tick — mirrors SdInt's main loop on PSX.
           SPU IRQ fires at ~88Hz (~3 ticks per 30fps frame).
           Must call IntSdMain (sequence processing), StrFadeInt (stream fade),
           WaveSpuTrans (wave data transfer), and StrSpuTrans (stream audio
           transfer to SPU voices 21-22 for codec/cutscene voice). */
        {
            extern void IntSdMain(void);
            extern void StrFadeInt(void);
            extern void WaveSpuTrans(void);
            extern void StrSpuTrans(void);
            extern long SpuIsTransferCompleted(long flag);
            for (int _st = 0; _st < 3; _st++) {
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
            }
        }

        { extern volatile int str_status; static int lst2=-1;
          if (str_status!=lst2) { printf("[str] after_tick: %d→%d\n",lst2,str_status); lst2=str_status; } }

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

        uint64_t ta1 = mach_absolute_time();

        /* OT render pipeline — must run BEFORE 3D so the OT clear happens
           before actors added prims. DG_SwapFrame draws the PREVIOUS frame's
           OT (1-GV_Clock) and clears the CURRENT OT (GV_Clock). Then actors
           already added prims to the cleared OT in GV_ExecActorSystem above. */
        DG_RenderFrame();

        /* Direct 3D renderer — skip when DG_UnDrawFrameCount > 0,
           and also skip when camera hasn't been initialized yet (port fix:
           cam eye == snake pos means zone camera hasn't set proper offset). */
        {
            extern int DG_FrameRate;
            extern int DG_UnDrawFrameCount;
            int skip_render = (DG_FrameRate == 2) || (DG_UnDrawFrameCount > 0);

            if (!skip_render) {
                extern int GM_LoadComplete;
                extern SVECTOR GM_PlayerPosition;
                extern DG_CHANL DG_Chanls[];
                if (GM_LoadComplete == 1) {
                    int cam_x = DG_Chanls[1].eye.t[0];
                    int cam_z = DG_Chanls[1].eye.t[2];
                    if (cam_x == GM_PlayerPosition.vx &&
                        cam_z == GM_PlayerPosition.vz) {
                        skip_render = 1;  /* camera not initialized yet */
                    }
                }
            }

            if (!skip_render)
                port_RenderObjects(GV_Clock);
        }
        uint64_t ta2 = mach_absolute_time();

        /* Draw the CURRENT frame's OT on top of 3D. This renders subtitles,
           HUD, and other 2D prims that actors just added to OT[GV_Clock].
           On PSX this happens at the NEXT VSync; on the port we do it now
           so prims don't get cleared before being drawn. */
        {
            extern void DG_DrawOTag(int which);
            DG_DrawOTag(GV_Clock);
        }
        uint64_t ta3 = mach_absolute_time();

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
