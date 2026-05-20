/**
 * Stub definitions for external symbols not yet provided by compiled source.
 * Remove stubs from here as real implementations are compiled in.
 */

#include <stdio.h>
#include <string.h>

/* sound init flag — set by game_init before starting SdInt task
   to prevent SdInt from calling sd_init() again */
int port_sd_init_done = 0;

/* game globals */
extern int GM_CurrentMap;
extern int gTotalFrameTime;

/* StageCharacterEntries is defined in source/game/chara.c.
   GM_InitChara sets it via mts_get_bss_tail (we override that in mts.c).
   GM_LoadInitBin (gamed.c) copies stage overlay binary to this address.
   For our port, we register a 'b' loader that points to the compiled overlay. */

/* MainCharacterEntries — from source/main/main.c */
#define DECLARE_NEWCHARA_PROTOS
#include "charalst.h"

CHARA MainCharacterEntries[] = {
    /* Core gameplay characters */
    CHARA_SNAKE,
    CHARA_ITEM,
    CHARA_DOOR,
    CHARA_PADDEMO,
    CHARA_PADVIBRATE,
    CHARA_PADCONTROL,
    /* Cutscene characters (0x0003-0x002F) — used by MakeChara in demo.c */
    CHARA_0003_FADEIN,
    CHARA_0004_FADEOUT,
    CHARA_0005_TEXT,
    CHARA_0009_BLOOD,
    CHARA_000A_BLOOD2,
    CHARA_000B_BULLET,
    CHARA_000D_BLOODCIRCLE,
    CHARA_000E_BREATH,
    CHARA_000F_SHADOW,
    CHARA_0010_FOOTPRINTS,
    CHARA_0011_NINJAEYE,
    CHARA_0012_BUBBLE,
    CHARA_0013_BUBBLE2,
    CHARA_0014_SCOPE,
    CHARA_0015_DARKVISIBLEGOGGLE,
    CHARA_0016_DARKVISIBLEGOGGLE2,
    CHARA_0017_IRRAYSGOGGLE,
    CHARA_0018_IRRAYSGOGGLE2,
    CHARA_001A_OPTICSCAMOUFLAGE,
    CHARA_001B_OPTICSCAMOUFLAGE2,
    CHARA_001C_ENVIRONMENTMAPPING,
    CHARA_001E_WINDCIRCLE,
    CHARA_001F_SEPIA,
    CHARA_0021_UNSHAPEVIEW,
    CHARA_0025_BLUR,
    CHARA_0028_MONOTONE,
    CHARA_002B_URINATIONCIRCLE2,
    CHARA_002D_NINJASWORD,
    CHARA_002E_SUBMARINEROOM,
    /* Cutscene character model (デモ人形) */
    CHARA_DEMODOLL,
    /* Cutscene-camera framing actor — drives the runtime camera during
     * cinematics. Hash 0x8E45 (WT_VIEW). Without this, demo.gcl's
     * `chara $s:8e45` directive logs "func not found", so DG_LookAt
     * runs every frame with stale gUnkCameraStruct values and the
     * camera appears frozen. NewWaterView is in source/takabe/wt_view.c
     * which is already linked into the editor + live game. */
    CHARA_WT_VIEW,
    CHARA_END
};

/* gamed.c is now compiled from source */

/* Linker-defined symbol for end of BSS section.
   On PSX this is provided by psylink; for the port we just point to a buffer. */
unsigned char _bss_orgend[1];
