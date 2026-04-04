#define __GAME_GAMED_C__
#include "game.h"

#include <stdio.h>
#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include <libapi.h>
#include <libcd.h>
#include <libpad.h>
#include <libspu.h>

#include "common.h"
#include "mts/mts.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libfs/libfs.h"
#include "libgcl/libgcl.h"
#include "memcard/memcard.h"

#include "linkvar.h"
#include "game/loader.h"
#include "game/over.h"
#include "menu/menuman.h"

/*---------------------------------------------------------------------------*/

//both below are defined in gvd.c
extern char            *GM_StageName;
char                   *GM_StageName;

extern GV_PAD                  *GM_CurrentPadData;
GV_PAD        *SECTION(".sbss") GM_CurrentPadData;

int GM_GameStatus = 0;
int GM_LoadRequest = 0;
int GM_GameOverTimer = 0;

SVECTOR *GM_lpsvectWind = NULL;

TPlayerActFunction GM_lpfnPlayerActControl = NULL;
TPlayerActFunction GM_lpfnPlayerActObject2 = NULL;

short GM_uBombHoming = 0;
short GM_uTenageMotion = -1;

TBombFunction  GM_lpfnBombHoming = NULL;
TBombFunction2 GM_lpfnBombBound = NULL;
TBombFunction3 GM_lpfnBombExplosion = NULL;

int GM_PadResetDisable = FALSE;

int          SECTION(".sbss") dword_800AB9CC;
int          SECTION(".sbss") reset_timer;
int          SECTION(".sbss") dword_800AB9D4;
short        SECTION(".sbss") GM_WeaponChanged;
short        SECTION(".sbss") word_800AB9DA;
int          SECTION(".sbss") GM_ClaymoreMap;
int          SECTION(".sbss") GM_AlertMax;
unsigned int SECTION(".sbss") GM_DisableWeapon;
int          SECTION(".sbss") gTotalFrameTime;
short        SECTION(".sbss") GM_Magazine;
int          SECTION(".sbss") GM_PlayerAddress;
CONTROL     *SECTION(".sbss") GM_PlayerControl;
SVECTOR      SECTION(".sbss") GM_NoisePosition;
int          SECTION(".sbss") GM_AlertMode;
int          SECTION(".sbss") GM_Photocode;
int          SECTION(".sbss") dword_800ABA08;
int          SECTION(".sbss") GM_PlayerMap;
SVECTOR      SECTION(".sbss") GM_PlayerPosition;
int          SECTION(".sbss") GM_AlertLevel;
int          SECTION(".sbss") dword_800ABA1C;
OBJECT      *SECTION(".sbss") GM_PlayerBody;
int          SECTION(".sbss") GM_NoisePower;
int          SECTION(".sbss") GM_DisableItem;
short        SECTION(".sbss") GM_MagazineMax;
int          SECTION(".sbss") GM_NoiseLength;
short        SECTION(".sbss") GM_O2;
short        SECTION(".sbss") GM_PDA_ClearRank;
int          SECTION(".sbss") GM_LoadComplete;
int          SECTION(".sbss") GM_PadVibration;
int          SECTION(".sbss") GM_PlayerAction;
STATIC int   SECTION(".sbss") dword_800ABA44;
SVECTOR      SECTION(".sbss") GM_PhotoViewPos;

/**
 * Some known settings via GM_SetPlayerStatusFlag():
 * |= 0x20008000 if Snake dies from sna_check_dead_8004E384() and sna_anim_dying_80055524().
 * |= 0x20 if Snake crouches from sna_anim_crouch_800527DC().
 * |= 0x40 if Snake goes prone from  sna_anim_prone_begin_80053BE8() and sna_anim_prone_idle_800528BC().
 * |= 0x10 if Snake runs from sna_anim_run_begin_80053B88(), sna_anim_run_8005292C(),
 * sna_anim_rungun_begin_80056BDC() and sna_anim_rungun_80056C3C().
 * |= 0x10 if Snake moves while prone from sna_anim_prone_move_800529C0().
 * |= 0x10 if Snake moves while in a box from sna_anim_box_run_8005544C().
 * |= 0x10000 if Snake pushes up against a wall from sna_anim_wall_idle_and_c4_80052A5C().
 * |= 0x10010 if Snake moves while up against a wall from sna_anim_wall_move_80052BA8().
 * |= 0x10020 if Snake crouches while up against a wall from sna_anim_wall_crouch_80052CCC().
 * |= 0x10 from sna_anim_choke_drag_80059054().
 */
PlayerStatusFlag SECTION(".sbss") GM_PlayerStatus;
int              SECTION(".sbss") GM_PadVibration2;

extern unsigned short   GM_SystemCallbackProc[6];
extern int          str_mute_fg;
extern unsigned int str_status;
extern int          dword_800BF1A8;
extern int          dword_800BF270;
extern int          str_off_idx;
extern char         exe_name[32];
extern char        *MGS_DiskName[3]; /* in main.c */
extern int          FS_DiskNum;
extern int          FS_ResidentCacheDirty;

extern DG_TEX gMenuTextureRec_800B58B0;

extern gameWork GameWork;

extern unsigned char *GV_ResidentMemoryBottom;

extern void *StageCharacterEntries;
extern int gOverlayBinSize_800B5290;

/*---------------------------------------------------------------------------*/

static void GM_ClearWeaponAndItem(void)
{
    GM_CurrentWeaponId = WP_None;
    GM_CurrentItemId = IT_None;
}

static void GM_InitGameSystem(void)
{
    int i;

    GM_PlayerAddress = -1;
    GM_GameStatus = 0;
    GM_GameOverTimer = 0;
    GM_PlayerStatus = 0;
    GM_NoisePower = 0;
    GM_NoiseLength = 0;
    GM_ClaymoreMap = 0;
    GM_AlertLevel = 0;
    GM_AlertMax = 0;
    GM_AlertMode = ALERT_OFF;
    GM_WeaponChanged = 0;
    GM_Magazine = 0;
    GM_MagazineMax = 0;
    GM_DisableItem = 0;
    GM_DisableWeapon = 0;
    GM_O2 = 1024;
    GM_StageName = NULL;
    GM_EnvironTemp = 0;
    GM_PlayerPosition.vx = GM_SnakePosX;
    GM_PlayerPosition.vy = GM_SnakePosY;
    GM_PlayerPosition.vz = GM_SnakePosZ;

    for (i = 5; i >= 0; i--)
    {
        GM_SystemCallbackProc[i] = 0;
    }
}

static void GM_InitNoise(void)
{
    int length;
    int max;

    // isn't this one of the inlines?
    length = GM_NoiseLength;
    if (GM_NoiseLength > 0)
    {
        length = GM_NoiseLength - 1;
    }
    if (!length)
    {
        GM_NoisePower = 0;
    }

    max = GM_AlertMax;
    GM_NoiseLength = length;
    GM_AlertMax = 0;
    GM_AlertLevel = max;
}

static void GM_ResetSystem(void)
{
    menuman_Reset();
    GV_ResetSystem();
    DG_ResetPipeline();
    GCL_ResetSystem();
}

static void GM_ResetMemory(void)
{
    DG_ResetTextureCache();
    GV_ResetMemory();
    GM_ResetChara();
}

// GM_InitStage?
static int port_loader_count = 0;
static void GM_CreateLoader(void)
{
    char *stage = "init";
    if (GM_CurrentStageFlag != 0)
    {
        stage = GM_GetArea(GM_CurrentStageFlag);
    }
#ifdef PORT_BUILD
    /* Port: load select on the second call (after init completes) */
    port_loader_count++;
    if (port_loader_count == 2) stage = "select";
#endif
    NewLoader(stage);
}

static void GM_HidePauseScreen(void)
{
    GV_PauseLevel &= ~GV_PAUSE_PAUSE;
    GM_SetSound(0x01ffff02, SD_ASYNC);
    MENU_JimakuClear();
    GM_GameStatus &= ~GAME_FLAG_BIT_08;
}

static void GM_ShowPauseScreen(void)
{
    char *areaName;

    areaName = "";
    GV_PauseLevel |= GV_PAUSE_PAUSE;
    GM_SetSound(0x01ffff01, SD_ASYNC);
    if (GM_StageName)
    {
        areaName = GM_StageName;
    }
    MENU_AreaNameWrite(areaName);
}

static void GM_TogglePauseScreen(void)
{
    int var1;
    int var2;
    int ret;

    var1 = GV_PauseLevel;
    var2 = var1 & ~2;
    ret = var2; // Why this waste?
    // It should always be true because the only caller
    // does the same check before calling this function.
    if (var2 == 0)
    {
        if ((var1 & 2) == 0)
        {
            GM_ShowPauseScreen();
            return;
        }
        GM_HidePauseScreen();
    }
}

static void GM_ActInit(gameWork *work)
{
    GM_Reset_helper3_80030760();
    GM_InitWhereSystem();
    GM_InitTargetSystem();
    GM_ResetHomingTargets();
    GM_ResetScript();
    GM_InitGameSystem();
    GM_AlertModeInit();
}

/*---------------------------------------------------------------------------*/

#define PCC_READ    0xa0be  // GV_StrCode("read")

void GM_InitReadError(void)
{
    DG_TEX *tex;

    tex = DG_GetTexture(PCC_READ);
    gMenuTextureRec_800B58B0 = *tex;
    gMenuTextureRec_800B58B0.id = 0;
}

void DrawReadError(void)
{
    int      u_off;
    DR_TPAGE tpage;
    SPRT     sprt;
    TILE     tile;

    u_off = 16 * gMenuTextureRec_800B58B0.id;
    gMenuTextureRec_800B58B0.id = (gMenuTextureRec_800B58B0.id + 1) % 6;

    DG_DisableClipping();

    setDrawTPage(&tpage, 1, 1, gMenuTextureRec_800B58B0.tpage);
    DrawPrim(&tpage);

    LSTORE(0, &tile.r0);
    setTile(&tile);
    tile.x0 = 287;
    tile.y0 = 15;
    tile.h = 18;
    tile.w = 18;
    DrawPrim(&tile);

    LSTORE(0x80808080, &sprt.r0);
    setSprt(&sprt);
    sprt.w = 16;
    sprt.h = 16;
    sprt.x0 = 288;
    sprt.y0 = 16;
    sprt.u0 = gMenuTextureRec_800B58B0.off_x + u_off;
    sprt.v0 = gMenuTextureRec_800B58B0.off_y;
    sprt.clut = gMenuTextureRec_800B58B0.clut;
    DrawPrim(&sprt);
}

/*---------------------------------------------------------------------------*/

static void Act(gameWork *work)
{
    int load_request;
    int status;

    unsigned short pad = mts_read_pad(1);

    if (mts_get_pad_vibration_type(1) == 1)
    {
        GM_OptionFlag &= ~OPTION_VIBRATION_OFF;
    }
    else
    {
        GM_OptionFlag |= OPTION_VIBRATION_OFF;
    }

    if ((GM_OptionFlag & (OPTION_UNKNOWN_2000 | OPTION_VIBRATION_OFF)) == 0)
    {
        int vibration2;
        if (GM_PadVibration != 0)
        {
            mts_set_pad_vibration(1, 10);
        }
        else
        {
            mts_set_pad_vibration(1, 0);
        }

        vibration2 = GM_PadVibration2;
        if (vibration2 > 255)
        {
            vibration2 = 255;
        }
        mts_set_pad_vibration2(1, vibration2);
    }

    GM_PadVibration2 = 0;
    GM_PadVibration = 0;

    if ((GV_PauseLevel & GV_PAUSE_READERROR) != 0)
    {
        if (!str_mute_fg && CDBIOS_TaskState() != 3)
        {
            GV_PauseLevel &= ~GV_PAUSE_READERROR;
        }
        else
        {
            DrawReadError();
        }
    }
    else if (str_mute_fg || CDBIOS_TaskState() == 3)
    {
        GV_PauseLevel |= GV_PAUSE_READERROR;
    }

    if ((GV_PauseLevel & GV_PAUSE_PAUSE) == 0)
    {
        int minutes;
        gTotalFrameTime += GV_PassageTime;
        minutes = gTotalFrameTime / 60;
        GM_TotalHours = minutes / 3600;
        GM_TotalSeconds = minutes % 3600;
    }

    status = work->status;
    if (status != WAIT_LOAD)
    {
        if (status != WORKING)
        {
            return;
        }
    }
    else
    {
        if (GM_LoadComplete == 0)
        {
            return;
        }

        GM_LoadComplete = 1;

        if ((GM_LoadRequest & 0x80) != 0)
        {
            DG_UnDrawFrameCount = 0;
        }

        if (FS_ResidentCacheDirty)
        {
            GV_SaveResidentFileCache();
            DG_SaveResidentTextureCache();
            FS_ResidentCacheDirty = 0;
        }

        GM_ResetMap();
        NewCameraSystem();
        DG_StorePalette();
        GM_AlertReset();

        if ((GM_LoadRequest & 0x10) != 0)
        {
            GCL_SaveVar();
        }

        printf("exec scenario\n");
        load_request = GM_LoadRequest;
        GM_LoadRequest = 0;

        if ((load_request & 0x20) != 0)
        {
            GCL_ExecProc((unsigned int)load_request >> 16, 0);
        }
        else
        {
            GCL_ExecScript();
        }

        printf("end scenario\n");
        MENU_ResetTexture();
        GM_AlertModeReset();
        GM_SoundStart();
#ifdef PORT_BUILD
        /* Port: ensure pad input is enabled when entering gameplay.
           On PSX, the GCL 'pad -s' command clears this, but some stages
           may not run it due to skipped cutscene procs. */
        GM_GameStatus &= ~(STATE_PADRELEASE | STATE_ALL_OFF);
#endif
        work->status = WORKING;

        return;
    }

    if ((work->killing_count <= 0))
    {
        if (GM_GameOverTimer != 0)
        {
            if (GM_GameOverTimer > 0)
            {
                if ((GM_GameOverTimer == status))
                {
                    if (GM_StreamStatus() == -1)
                    {
                        if (NewGameOver(TRUE))
                        {
                            GM_GameOverTimer = -1;
                        }
                        else
                        {
                            GV_DestroyActorSystem( GV_KILL_LEVEL_NORMAL );
                        }
                    }
                    else if (GM_StreamStatus() == status)
                    {
                        GM_StreamPlayStop();
                    }
                }
                else
                {
                    GM_GameOverTimer--;
                }
            }
        }
        else
        {
            if (GM_LoadRequest != 0 && (GV_PauseLevel & GV_PAUSE_PAUSE) == 0)
            {
                if ((GM_LoadRequest & 0x80) != 0)
                {
                    DG_UnDrawFrameCount = 0x7fff0000;
                }

                GV_DestroyActorSystem( GV_KILL_LEVEL_NORMAL );
#ifdef PORT_BUILD
                /* Void the render queue after mass actor destruction.
                   On PSX, freed DG_OBJS memory stays readable. On the port,
                   macOS fills it with patterns, causing crashes in DG_ScreenChanl. */
                { extern void DG_FreeObjectQueue(void); DG_FreeObjectQueue(); }
#endif
                GV_PauseLevel &= ~GV_PAUSE_READERROR;
                GM_ResetMapModel();
                GM_StreamPlayStop();
                work->killing_count = 3;
                GM_GameStatus |= STATE_PADRELEASE | STATE_ALL_OFF;

                return;
            }

            if (GV_PauseLevel == 0)
            {
                GM_AlertAct();
            }

            if ((GM_GameStatus & (STATE_VOX_STREAM | STATE_PAUSE_OFF | STATE_PADMASK | STATE_PADRELEASE |
                                           STATE_PADDEMO | STATE_DEMO)) == 0)
            {
                if (((GV_PauseLevel & ~2) == 0) && ((GM_CurrentPadData->press & PAD_START) != 0))
                {
                    GM_TogglePauseScreen();
                }
            }
            else if ((GV_PauseLevel & GV_PAUSE_PAUSE) != 0)
            {
                GM_HidePauseScreen();
            }

            GM_UpdateMap();
        }

#define RESET_COMBO     (PAD_LR|PAD_START|PAD_SELECT)
#define RESET_DELAY     (90)

        if (((pad & RESET_COMBO) == RESET_COMBO) && (GM_PadResetDisable == FALSE))
        {
            // User must hold the combo for 90 frames.
            if (--reset_timer < 0)
            {
                sprintf(exe_name, "cdrom:\\MGS\\%s;1", MGS_DiskName[FS_DiskNum]);
                EnterCriticalSection();
                SetDispMask(0);
                PadStopCom();
                SpuInit();
                CdInit();
                SpuSetIRQ(SPU_OFF);
                mts_shutdown();
                memcard_exit();
                ResetGraph(3);
                StopCallback();
                SetConf(0x10, 4, 0x801FFFF0); // note: hardcoded addresses
                ResetCallback();
                StopCallback();
                _96_remove();
                _96_init();

                do {
                    printf("load %s\n", exe_name);
                    LoadExec(exe_name, 0x801FFF00, 0);
                } while (1);
            }
        }
        else
        {
            // Reset the countdown if the combo's been released.
            reset_timer = RESET_DELAY;
        }

#undef RESET_COMBO
#undef RESET_DELAY

        if ((GM_GameStatus < 0) && ((GM_CurrentPadData[2].press & (PAD_START | PAD_CROSS)) != 0))
        {
            GM_StreamPlayStop();
        }

        if ((mts_read_pad(2) & PAD_CIRCLE) != 0)
        {
            char         spu_status[24];
            char         spu_stat;
            int          i;
            unsigned int spu_key;

            SpuGetAllKeysStatus(spu_status);
            spu_key = 0;
            for (i = 0; i < 24; ++i)
            {
                spu_key *= 2;
                spu_stat = spu_status[i];
                spu_key |= spu_stat & 1;
            }

            printf("str_status %d irq %x %X %X\n", str_status, dword_800BF1A8, dword_800BF270,
                   str_off_idx);
            printf("key %08X\n", spu_key);
        }

        if (GV_PauseLevel == 0)
        {
            GM_InitNoise();
        }
    }
    else
    {
        GV_PauseLevel &= ~GV_PAUSE_READERROR;

        if ((--work->killing_count <= 0))
        {
            if (GM_StreamStatus() == -1)
            {
                if ((GV_PauseLevel & 5) == 0)
                {
                    work->status = 0;
                    work->killing_count = 0;
                    GM_ResetMapHazard();
                    GM_ResetSystem();
                    GM_ActInit(work);

                    if ((GM_LoadRequest & 0x40) == 0)
                    {
                        GM_ResetMemory();
                        GM_CreateLoader();
                    }

                    return;
                }
                else if ((GM_LoadRequest & 0x80) != 0)
                {
                    DG_UnDrawFrameCount = 0;
                }
            }

            work->killing_count = status;
        }

        if (GV_PauseLevel == 0)
        {
            GM_InitNoise();
        }
    }
}

void GM_SetSystemCallbackProc(int index, int proc)
{
    GM_SystemCallbackProc[index] = proc;
}

void GM_CallSystemCallbackProc(int id, int arg)
{
    int proc;

    if (id == 4 && GM_PlayerControl != NULL)
    {
        HZD_ReExecEvent(GM_PlayerControl->map->hzd,
                        &GM_PlayerControl->evt, 0x301);
    }

    proc = GM_SystemCallbackProc[id];
    if (proc != 0)
    {
        GCL_ARGS args;
        long     argbuf[2];

        args.argc = 1;
        args.argv = argbuf;
        argbuf[0] = arg;
        GCL_ForceExecProc(proc, &args);
    }
}

void GM_SetLoadCallbackProc(int proc_id)
{
    if (proc_id == -1)
    {
        GM_LoadRequest = 0xc0;
    }
    else
    {
        GM_LoadRequest = proc_id << 16 | 0xe0;
    }
}

void GM_ContinueStart(void)
{
    int total_continues;
    int current_stage;

    GM_CallSystemCallbackProc(1, 0);
    total_continues = GM_TotalContinues;
    current_stage = GM_CurrentStageFlag;
    GCL_RestoreVar();
    if (GM_CurrentStageFlag != current_stage)
    {
        GM_LoadRequest = 1;
    }
    else
    {
        GM_SetLoadCallbackProc(-1);
    }

    GM_TotalContinues = total_continues + 1;

    // Set the bomb to no less than 10 seconds to prevent instant death
    // note: casting needed to produce sltiu and lhu vs lh
    if ((unsigned int)(unsigned short)GM_TimerBombFlag - 1 < 9)
    {
        GM_TimerBombFlag = 10;
    }
}

void GM_GameOver(void)
{
    if (!GM_GameOverTimer)
    {
        GM_GameOverTimer = 4;
        GM_CallSystemCallbackProc(0, 0);
        GM_GameStatus |= (STATE_RADIO_OFF | STATE_PAUSE_OFF | STATE_MENU_OFF);
    }
}

/**
 *  @brief      Overlay binary initialization handler
 *
 *  Copies the overlay from its temporary buffer to the overlay base address.
 *  The overlay's filename will always be xxx.bin (where "xxx" is the name of
 *  the current stage).
 *
 *  @param[in]  buf     pointer to cached overlay buffer
 *  @param[in]  id      strcode of the overlay's basename
 *
 *  @retval     1       on success
 *  @retval     <= 0    on failure (but this can't happen)
 */
static int GM_LoadInitBin(void *buf, int id)
{
#ifdef PORT_BUILD
    /* Port: all stage overlays are compiled statically.
       Map the strcode id to the correct _StageCharacterEntries symbol. */
    {
        extern void *_StageCharacterEntries_abst, *_StageCharacterEntries_brf;
        extern void *_StageCharacterEntries_camera, *_StageCharacterEntries_change;
        extern void *_StageCharacterEntries_d00a, *_StageCharacterEntries_d01a;
        extern void *_StageCharacterEntries_d03a, *_StageCharacterEntries_d11c;
        extern void *_StageCharacterEntries_d16e, *_StageCharacterEntries_d18a;
        extern void *_StageCharacterEntries_d18ar, *_StageCharacterEntries_demosel;
        extern void *_StageCharacterEntries_ending, *_StageCharacterEntries_endingr;
        extern void *_StageCharacterEntries_opening, *_StageCharacterEntries_option;
        extern void *_StageCharacterEntries_preope, *_StageCharacterEntries_rank;
        extern void *_StageCharacterEntries_roll;
        extern void *_StageCharacterEntries_s00a, *_StageCharacterEntries_s01a;
        extern void *_StageCharacterEntries_s02a, *_StageCharacterEntries_s02b;
        extern void *_StageCharacterEntries_s02c, *_StageCharacterEntries_s02d;
        extern void *_StageCharacterEntries_s02e;
        extern void *_StageCharacterEntries_s03a, *_StageCharacterEntries_s03ar;
        extern void *_StageCharacterEntries_s03b, *_StageCharacterEntries_s03c;
        extern void *_StageCharacterEntries_s03d, *_StageCharacterEntries_s03dr;
        extern void *_StageCharacterEntries_s03e, *_StageCharacterEntries_s03er;
        extern void *_StageCharacterEntries_s04a, *_StageCharacterEntries_s04b;
        extern void *_StageCharacterEntries_s04br, *_StageCharacterEntries_s04c;
        extern void *_StageCharacterEntries_s05a, *_StageCharacterEntries_s06a;
        extern void *_StageCharacterEntries_s07a, *_StageCharacterEntries_s07b;
        extern void *_StageCharacterEntries_s07br, *_StageCharacterEntries_s07c;
        extern void *_StageCharacterEntries_s07cr;
        extern void *_StageCharacterEntries_s08a, *_StageCharacterEntries_s08b;
        extern void *_StageCharacterEntries_s08br, *_StageCharacterEntries_s08c;
        extern void *_StageCharacterEntries_s08cr;
        extern void *_StageCharacterEntries_s09a, *_StageCharacterEntries_s09ar;
        extern void *_StageCharacterEntries_s10a, *_StageCharacterEntries_s10ar;
        extern void *_StageCharacterEntries_s11a, *_StageCharacterEntries_s11b;
        extern void *_StageCharacterEntries_s11c, *_StageCharacterEntries_s11d;
        extern void *_StageCharacterEntries_s11e, *_StageCharacterEntries_s11g;
        extern void *_StageCharacterEntries_s11h, *_StageCharacterEntries_s11i;
        extern void *_StageCharacterEntries_s12a, *_StageCharacterEntries_s12b;
        extern void *_StageCharacterEntries_s12c, *_StageCharacterEntries_s13a;
        extern void *_StageCharacterEntries_s14e;
        extern void *_StageCharacterEntries_s15a, *_StageCharacterEntries_s15b;
        extern void *_StageCharacterEntries_s15c, *_StageCharacterEntries_s16a;
        extern void *_StageCharacterEntries_s16b, *_StageCharacterEntries_s16c;
        extern void *_StageCharacterEntries_s16d;
        extern void *_StageCharacterEntries_s17a, *_StageCharacterEntries_s17ar;
        extern void *_StageCharacterEntries_s18a, *_StageCharacterEntries_s18ar;
        extern void *_StageCharacterEntries_s19a, *_StageCharacterEntries_s19ar;
        extern void *_StageCharacterEntries_s19b, *_StageCharacterEntries_s19br;
        extern void *_StageCharacterEntries_s20a, *_StageCharacterEntries_s20ar;
        extern void *_StageCharacterEntries_select, *_StageCharacterEntries_select1;
        extern void *_StageCharacterEntries_select2, *_StageCharacterEntries_select3;
        extern void *_StageCharacterEntries_select4, *_StageCharacterEntries_selectd;
        extern void *_StageCharacterEntries_sound, *_StageCharacterEntries_title;

        unsigned short stage_id = (unsigned short)id;
        switch (stage_id) {
        case 0x1706: StageCharacterEntries = &_StageCharacterEntries_abst; break;
        case 0x96A7: StageCharacterEntries = &_StageCharacterEntries_brf; break;
        case 0xEEE9: StageCharacterEntries = &_StageCharacterEntries_camera; break;
        case 0x11F8: StageCharacterEntries = &_StageCharacterEntries_change; break;
        case 0xC693: StageCharacterEntries = &_StageCharacterEntries_d00a; break;
        case 0xC6B3: StageCharacterEntries = &_StageCharacterEntries_d01a; break;
        case 0xC6F3: StageCharacterEntries = &_StageCharacterEntries_d03a; break;
        case 0xCAB5: StageCharacterEntries = &_StageCharacterEntries_d11c; break;
        case 0xCB57: StageCharacterEntries = &_StageCharacterEntries_d16e; break;
        case 0xCB93: StageCharacterEntries = &_StageCharacterEntries_d18a; break;
        case 0x72EB: StageCharacterEntries = &_StageCharacterEntries_d18ar; break;
        case 0x2A2F: StageCharacterEntries = &_StageCharacterEntries_demosel; break;
        case 0x833B: StageCharacterEntries = &_StageCharacterEntries_ending; break;
        case 0x67E2: StageCharacterEntries = &_StageCharacterEntries_endingr; break;
        case 0x58CC: StageCharacterEntries = &_StageCharacterEntries_opening; break;
        case 0x978A: StageCharacterEntries = &_StageCharacterEntries_option; break;
        case 0x31BA: StageCharacterEntries = &_StageCharacterEntries_preope; break;
        case 0x9265: StageCharacterEntries = &_StageCharacterEntries_rank; break;
        case 0xCA26: StageCharacterEntries = &_StageCharacterEntries_roll; break;
        case 0x469B: StageCharacterEntries = &_StageCharacterEntries_s00a; break;
        case 0x46BB: StageCharacterEntries = &_StageCharacterEntries_s01a; break;
        case 0x46DB: StageCharacterEntries = &_StageCharacterEntries_s02a; break;
        case 0x46DC: StageCharacterEntries = &_StageCharacterEntries_s02b; break;
        case 0x46DD: StageCharacterEntries = &_StageCharacterEntries_s02c; break;
        case 0x46DE: StageCharacterEntries = &_StageCharacterEntries_s02d; break;
        case 0x46DF: StageCharacterEntries = &_StageCharacterEntries_s02e; break;
        case 0x46FB: StageCharacterEntries = &_StageCharacterEntries_s03a; break;
        case 0xDFDA: StageCharacterEntries = &_StageCharacterEntries_s03ar; break;
        case 0x46FC: StageCharacterEntries = &_StageCharacterEntries_s03b; break;
        case 0x46FD: StageCharacterEntries = &_StageCharacterEntries_s03c; break;
        case 0x46FE: StageCharacterEntries = &_StageCharacterEntries_s03d; break;
        case 0xE03A: StageCharacterEntries = &_StageCharacterEntries_s03dr; break;
        case 0x46FF: StageCharacterEntries = &_StageCharacterEntries_s03e; break;
        case 0xE05A: StageCharacterEntries = &_StageCharacterEntries_s03er; break;
        case 0x471B: StageCharacterEntries = &_StageCharacterEntries_s04a; break;
        case 0x471C: StageCharacterEntries = &_StageCharacterEntries_s04b; break;
        case 0xE3FA: StageCharacterEntries = &_StageCharacterEntries_s04br; break;
        case 0x471D: StageCharacterEntries = &_StageCharacterEntries_s04c; break;
        case 0x473B: StageCharacterEntries = &_StageCharacterEntries_s05a; break;
        case 0x475B: StageCharacterEntries = &_StageCharacterEntries_s06a; break;
        case 0x477B: StageCharacterEntries = &_StageCharacterEntries_s07a; break;
        case 0x477C: StageCharacterEntries = &_StageCharacterEntries_s07b; break;
        case 0xEFFA: StageCharacterEntries = &_StageCharacterEntries_s07br; break;
        case 0x477D: StageCharacterEntries = &_StageCharacterEntries_s07c; break;
        case 0xF01A: StageCharacterEntries = &_StageCharacterEntries_s07cr; break;
        case 0x479B: StageCharacterEntries = &_StageCharacterEntries_s08a; break;
        case 0x479C: StageCharacterEntries = &_StageCharacterEntries_s08b; break;
        case 0xF3FA: StageCharacterEntries = &_StageCharacterEntries_s08br; break;
        case 0x479D: StageCharacterEntries = &_StageCharacterEntries_s08c; break;
        case 0xF41A: StageCharacterEntries = &_StageCharacterEntries_s08cr; break;
        case 0x47BB: StageCharacterEntries = &_StageCharacterEntries_s09a; break;
        case 0xF7DA: StageCharacterEntries = &_StageCharacterEntries_s09ar; break;
        case 0x4A9B: StageCharacterEntries = &_StageCharacterEntries_s10a; break;
        case 0x53DB: StageCharacterEntries = &_StageCharacterEntries_s10ar; break;
        case 0x4ABB: StageCharacterEntries = &_StageCharacterEntries_s11a; break;
        case 0x4ABC: StageCharacterEntries = &_StageCharacterEntries_s11b; break;
        case 0x4ABD: StageCharacterEntries = &_StageCharacterEntries_s11c; break;
        case 0x4ABE: StageCharacterEntries = &_StageCharacterEntries_s11d; break;
        case 0x4ABF: StageCharacterEntries = &_StageCharacterEntries_s11e; break;
        case 0x4AC1: StageCharacterEntries = &_StageCharacterEntries_s11g; break;
        case 0x4AC2: StageCharacterEntries = &_StageCharacterEntries_s11h; break;
        case 0x4AC3: StageCharacterEntries = &_StageCharacterEntries_s11i; break;
        case 0x4ADB: StageCharacterEntries = &_StageCharacterEntries_s12a; break;
        case 0x4ADC: StageCharacterEntries = &_StageCharacterEntries_s12b; break;
        case 0x4ADD: StageCharacterEntries = &_StageCharacterEntries_s12c; break;
        case 0x4AFB: StageCharacterEntries = &_StageCharacterEntries_s13a; break;
        case 0x4B1F: StageCharacterEntries = &_StageCharacterEntries_s14e; break;
        case 0x4B3B: StageCharacterEntries = &_StageCharacterEntries_s15a; break;
        case 0x4B3C: StageCharacterEntries = &_StageCharacterEntries_s15b; break;
        case 0x4B3D: StageCharacterEntries = &_StageCharacterEntries_s15c; break;
        case 0x4B5B: StageCharacterEntries = &_StageCharacterEntries_s16a; break;
        case 0x4B5C: StageCharacterEntries = &_StageCharacterEntries_s16b; break;
        case 0x4B5D: StageCharacterEntries = &_StageCharacterEntries_s16c; break;
        case 0x4B5E: StageCharacterEntries = &_StageCharacterEntries_s16d; break;
        case 0x4B7B: StageCharacterEntries = &_StageCharacterEntries_s17a; break;
        case 0x6FDB: StageCharacterEntries = &_StageCharacterEntries_s17ar; break;
        case 0x4B9B: StageCharacterEntries = &_StageCharacterEntries_s18a; break;
        case 0x73DB: StageCharacterEntries = &_StageCharacterEntries_s18ar; break;
        case 0x4BBB: StageCharacterEntries = &_StageCharacterEntries_s19a; break;
        case 0x77DB: StageCharacterEntries = &_StageCharacterEntries_s19ar; break;
        case 0x4BBC: StageCharacterEntries = &_StageCharacterEntries_s19b; break;
        case 0x77FB: StageCharacterEntries = &_StageCharacterEntries_s19br; break;
        case 0x4E9B: StageCharacterEntries = &_StageCharacterEntries_s20a; break;
        case 0xD3DB: StageCharacterEntries = &_StageCharacterEntries_s20ar; break;
        case 0x8D5C: StageCharacterEntries = &_StageCharacterEntries_select; break;
        case 0xABC2: StageCharacterEntries = &_StageCharacterEntries_select1; break;
        case 0xABC3: StageCharacterEntries = &_StageCharacterEntries_select2; break;
        case 0xABC4: StageCharacterEntries = &_StageCharacterEntries_select3; break;
        case 0xABC5: StageCharacterEntries = &_StageCharacterEntries_select4; break;
        case 0xABF5: StageCharacterEntries = &_StageCharacterEntries_selectd; break;
        case 0x698D: StageCharacterEntries = &_StageCharacterEntries_sound; break;
        case 0x655B: StageCharacterEntries = &_StageCharacterEntries_title; break;
        default:     StageCharacterEntries = &_StageCharacterEntries_s00a; break;
        }
        printf("[bin] Stage overlay 0x%X → %p\n", stage_id, StageCharacterEntries);
    }
    return 1;
#endif
#ifdef DEV_EXE
    return 1;
#endif

    if (((u_char *)StageCharacterEntries + gOverlayBinSize_800B5290) > GV_ResidentMemoryBottom)
    {
        printf("TOO LARGE STAGE BINARY!!\n");
    }

    memcpy(StageCharacterEntries, buf, gOverlayBinSize_800B5290);
    return 1;
}

void GM_StartDaemon(void)
{
    gTotalFrameTime = 0;
    GM_GameOverTimer = 0;
    GM_LoadRequest = 0;
    GM_LoadComplete = 0;
    MENU_StartDeamon();
    GM_InitArea();
    GM_InitChara();
    GM_InitScript();
    GV_SetLoader('b', GM_LoadInitBin);
    GM_ClearWeaponAndItem();
    GV_InitActor(GV_ACTOR_MANAGER, &GameWork.actor, NULL);
    GV_SetNamedActor(&GameWork.actor, Act, NULL, "gamed.c");
    GM_ResetSystem();
    GM_ActInit(&GameWork);
    GM_ResetMemory();
    GM_CurrentPadData = GV_PadData;
    GM_CurrentDiskFlag = FS_DiskNum + 1;
    GV_SaveResidentTop();
    GameWork.status = 0;
    GameWork.killing_count = 0;
    GM_CreateLoader();
}
