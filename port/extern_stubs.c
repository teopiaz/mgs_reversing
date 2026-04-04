/**
 * Stub definitions for external symbols not yet provided by compiled source.
 * Remove stubs from here as real implementations are compiled in.
 */

#include <stdio.h>
#include <string.h>

/* game globals */
int GM_CurrentMap = 0;
int gTotalFrameTime = 0;

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
    CHARA_0003_FADEIO,
    CHARA_0004_FADEIO,
    CHARA_0005_TELOP,
    CHARA_0009_BLOOD,
    CHARA_000A_SPLASH,
    CHARA_000B_BULLET,
    CHARA_000D_D_BLOODS,
    CHARA_000E,
    CHARA_000F_DEMOKAGE,
    CHARA_0010_DEMOASI,
    CHARA_0011,
    CHARA_0012_BUBBLE_T,
    CHARA_0013_BUBBLE_P,
    CHARA_0014_SCOPE,
    CHARA_0015_GOGGLE,
    CHARA_0016_GGLSIGHT,
    CHARA_0017_GOGGLEIR,
    CHARA_0018_GGLSIGHT,
    CHARA_001A_KOGAKU2,
    CHARA_001B_KOGAKU3,
    CHARA_001C_ENVMAP3,
    CHARA_001E_WINDCRCL,
    CHARA_001F_SEPIA,
    CHARA_0021_FOCUS,
    CHARA_0025_BLUR,
    CHARA_0028_SEPIA,
    CHARA_002B_D_BLOODS,
    CHARA_002D_KATANA,
    CHARA_002E_SUB_ROOM,
    /* Cutscene character model (デモ人形) */
    CHARA_DEMODOLL,
    CHARA_END
};

/* gamed.c is now compiled from source */

/* Linker-defined symbol for end of BSS section.
   On PSX this is provided by psylink; for the port we just point to a buffer. */
unsigned char _bss_orgend[1];
