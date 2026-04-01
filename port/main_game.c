/**
 * Port game initialization — replaces source/main/main.c.
 * Splits the PSX Main() into game_init() and game_tick()
 * for use with the SDL event loop.
 */
#include <stdio.h>
#include <math.h>
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

#include <setjmp.h>
#include <signal.h>
static jmp_buf gcl_jmp;
static void gcl_crash_handler(int sig) { (void)sig; longjmp(gcl_jmp, 1); }
static int port_safe_gcl_exec(void)
{
    void (*ob)(int) = signal(SIGBUS, gcl_crash_handler);
    void (*os)(int) = signal(SIGSEGV, gcl_crash_handler);
    int result = 0;
    if (setjmp(gcl_jmp) == 0) {
        GCL_ExecScript();
    } else {
        result = 1;
    }
    signal(SIGBUS, ob);
    signal(SIGSEGV, os);
    return result;
}

/* Safe no-op loader for formats that crash on 64-bit (OFFSET_TO_PTR) */
static int port_safe_loader(unsigned char *buf, int id)
{
    (void)buf; (void)id;
    return 1;
}

void game_init(void)
{
    printf("port: game_init starting\n");

    port_init_memory();

    /* Original Main() initialization sequence */
    InitGeom();

    printf("mem:");
    memcard_init();

    printf("pad:");
    /* mts_init_controller() — handled by SDL in port/main.c */

    printf("gv:");
    GV_StartDaemon();

    printf("fs:");
    FS_StartDaemon();

    printf("dg:"); fflush(stdout);
    DG_StartDaemon();

    printf("gcl:"); fflush(stdout);
    GCL_StartDaemon();

    printf("hzd:"); fflush(stdout);
    /* Register our 64-bit safe HZD loader (in port/libhzd/hzd_loader.c) */
    {
        extern int HZD_LoadInitHzd(void *, int);
        GV_SetLoader('h', (void *)HZD_LoadInitHzd);
    }

    printf("sound:"); fflush(stdout);

    printf("gm:\n"); fflush(stdout);

    /* Trace GM_StartDaemon internals */
    extern int gTotalFrameTime, GM_GameOverTimer, GM_LoadRequest, GM_LoadComplete;
    gTotalFrameTime = 0;
    GM_GameOverTimer = 0;
    GM_LoadRequest = 0;
    GM_LoadComplete = 0;
    printf("  [gm] pre-init done\n"); fflush(stdout);

    void MENU_StartDeamon(void);
    MENU_StartDeamon();
    printf("  [gm] MENU_StartDeamon done\n"); fflush(stdout);

    GM_InitArea();
    printf("  [gm] GM_InitArea done\n"); fflush(stdout);

    GM_InitChara();
    printf("  [gm] GM_InitChara done\n"); fflush(stdout);

    GM_InitScript();
    printf("  [gm] GM_InitScript done\n"); fflush(stdout);

    printf("  [gm] init done\n"); fflush(stdout);

    /* Initialize the memory system — this is what GM_ResetMemory does */
    GV_ResetMemory();
    printf("  [gm] memory reset done\n"); fflush(stdout);

    /* Skip the problematic parts of GM_StartDaemon:
       - GV_InitActor for GameWork (the gamed actor)
       - GM_ResetSystem / GM_ActInit / GM_ResetMemory
       - GM_CreateLoader (tries to load "init" stage)
       Instead, just load the init stage directly. */

    /* Load a playable stage */
    const char *stage_name = "s00a";
    printf("  [gm] Loading '%s' stage...\n", stage_name); fflush(stdout);
    void *info = FS_LoadStageRequest(stage_name);
    if (info)
    {
        printf("  [gm] Stage load requested, syncing...\n"); fflush(stdout);
        while (FS_LoadStageSync(info) != 0) {}
        FS_LoadStageComplete(info);
        printf("  [gm] Stage 'init' loaded and completed\n"); fflush(stdout);
    }

    /* Set up stage overlay character table AFTER all init (GM_InitChara sets it to NULL) */
    {
        extern void *StageCharacterEntries;
        extern void *_StageCharacterEntries;
        StageCharacterEntries = &_StageCharacterEntries;
        printf("  [gm] StageCharacterEntries = %p\n", StageCharacterEntries);
        fflush(stdout);
    }

    /* Execute the GCL script to create stage actors.
       The GCL parser has residual int/pointer truncation issues on 64-bit.
       Use setjmp/longjmp to recover from crashes during script execution. */
    printf("  [gm] Executing GCL script...\n"); fflush(stdout);
    if (port_safe_gcl_exec() == 0)
        printf("  [gm] GCL script executed OK\n");
    else
        printf("  [gm] GCL script hit a crash (HZD/pointer issue), continuing...\n");
    fflush(stdout);

    printf("start\n"); fflush(stdout);
}

static int tick_count = 0;

/* Free-fly camera state */
static float cam_x = 0, cam_y = -1500, cam_z = -4000;
static float cam_yaw = 0, cam_pitch = 0.3f; /* radians */
static int cam_initialized = 0;

extern void port_DrawTile(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b);

/* Camera controls via exported key state from mts.c */
extern unsigned char port_keys[256]; /* exported from mts.c */

static void update_camera(void)
{
    float speed = 100.0f;
    float rot_speed = 0.03f;

    /* Rotation: arrow keys (scancode values from SDL) */
    if (port_keys[79]) cam_yaw -= rot_speed;  /* RIGHT */
    if (port_keys[80]) cam_yaw += rot_speed;  /* LEFT */
    if (port_keys[82]) cam_pitch -= rot_speed; /* UP */
    if (port_keys[81]) cam_pitch += rot_speed; /* DOWN */

    /* Shift = fast */
    if (port_keys[225] || port_keys[229]) speed = 400.0f;

    if (cam_pitch > 1.4f) cam_pitch = 1.4f;
    if (cam_pitch < -1.4f) cam_pitch = -1.4f;

    float fx = sinf(cam_yaw) * cosf(cam_pitch);
    float fy = sinf(cam_pitch);
    float fz = cosf(cam_yaw) * cosf(cam_pitch);
    float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);

    if (port_keys[26]) { cam_x += fx*speed; cam_y += fy*speed; cam_z += fz*speed; } /* W */
    if (port_keys[22]) { cam_x -= fx*speed; cam_y -= fy*speed; cam_z -= fz*speed; } /* S */
    if (port_keys[4])  { cam_x -= rx*speed; cam_z += rz*speed; } /* A */
    if (port_keys[7])  { cam_x += rx*speed; cam_z -= rz*speed; } /* D */
    if (port_keys[20]) cam_y -= speed; /* Q = down */
    if (port_keys[8])  cam_y += speed; /* E = up */

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

/* Wrap any function call with crash recovery */
#include <setjmp.h>
#include <signal.h>
static sigjmp_buf tick_jmp;
static void tick_crash(int sig) { (void)sig; siglongjmp(tick_jmp, sig); }

static void safe_call(void (*fn)(void), const char *name)
{
    struct sigaction sa, ob, os;
    sa.sa_handler = tick_crash;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, &ob);
    sigaction(SIGSEGV, &sa, &os);
    int sig = sigsetjmp(tick_jmp, 1);
    if (sig == 0) {
        fn();
    } else {
        static int tc = 0;
        if (tc++ < 5) printf("[tick] CRASH in %s (sig=%d)\n", name, sig);
    }
    sigaction(SIGBUS, &ob, NULL);
    sigaction(SIGSEGV, &os, NULL);
}

static sigjmp_buf game_tick_jmp;
static void game_tick_crash(int sig) { (void)sig; siglongjmp(game_tick_jmp, sig); }

void game_tick(void)
{
    extern int GV_Clock;
    extern int DG_CurrentGroupID;
    extern int port_ot_next;
    extern void port_RenderObjects(int idx);

    /* Protect the entire tick from crashes */
    struct sigaction sa, ob, os;
    sa.sa_handler = game_tick_crash;
    sa.sa_flags = SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, &ob);
    sigaction(SIGSEGV, &sa, &os);

    int crashed = sigsetjmp(game_tick_jmp, 1);
    if (crashed) {
        static int tc = 0;
        if (tc++ < 5) printf("[tick] crashed (sig=%d), recovering\n", crashed);
        /* Restore signals and skip to end of frame */
        sigaction(SIGBUS, &ob, NULL);
        sigaction(SIGSEGV, &os, NULL);
        goto frame_end;
    }

    GV_ExecActorSystem();

    update_camera();

    {
        RECT fb = {0, 0, 320, 224};
        ClearImage(&fb, 0, 0, 32);
    }


    DG_CurrentGroupID = 0xFFFFFFFF;
    port_ot_next = 0;

    DG_SwapFrame();
    DG_RenderFrame();
    port_RenderObjects(GV_Clock);

    sigaction(SIGBUS, &ob, NULL);
    sigaction(SIGSEGV, &os, NULL);


frame_end:
    GV_Clock = 1 - GV_Clock;
    tick_count++;
}
