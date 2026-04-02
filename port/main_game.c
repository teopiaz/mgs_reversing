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
#include "game/game.h"

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
    /* sound skipped — no SPU */

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

    /* Clear framebuffer */
    {
        RECT fb = {0, 0, 320, 224};
        ClearImage(&fb, 0, 0, 32);
    }

    DG_CurrentGroupID = 0xFFFFFFFF;
    port_ot_next = 0;

    /* Clear scratchpad each frame — collision stubs don't initialize it properly */
    {
        extern char port_scratchpad[1024];
        memset(port_scratchpad, 0, 1024);
    }

    /* Force draw: clear blockers that GameWork sets during transitions */
    {
        extern int DG_UnDrawFrameCount;
        extern int DG_HikituriFlag;
        extern int DG_HikituriFlagOld;
        DG_UnDrawFrameCount = 0;
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

        /* Actor system FIRST: game logic, collision, movement
           (uses scratchpad for collision results) */
        uint64_t ta0 = mach_absolute_time();
        GV_ExecActorSystem();
        uint64_t ta1 = mach_absolute_time();

        /* Direct 3D renderer FIRST — draws geometry to VRAM */
        port_RenderObjects(GV_Clock);
        uint64_t ta2 = mach_absolute_time();

        /* OT-based render pipeline AFTER 3D — menu/UI prims draw on top.
           On PSX, menu prims were sorted at the front of the OT (nearest depth),
           so they drew last and appeared on top of 3D geometry. */
        DG_RenderFrame();
        uint64_t ta3 = mach_absolute_time();

        t_swap += ts1 - ts0;
        t_actors += ta1 - ta0;
        t_dgrender += ta2 - ta1;
        t_portrender += ta3 - ta2;
        perf_frames++;
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
    }

    /* Debug: print pad state every second */
    if ((tick_count % 60) == 0) {
        extern int GM_GameStatus;
        extern long mts_PadRead(int);
        long raw = mts_PadRead(0);
        printf("[tick %d] raw_pad=0x%lX status=0x%lX press=0x%lX gs=0x%X snake=(%d,%d,%d)\n",
               tick_count,
               raw,
               (unsigned long)GV_PadData[0].status,
               (unsigned long)GV_PadData[0].press,
               GM_GameStatus,
               GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz);
    }

    GV_Clock = 1 - GV_Clock;
    tick_count++;
}
