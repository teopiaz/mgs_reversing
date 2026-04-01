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
    CHARA_SNAKE,
    CHARA_ITEM,
    CHARA_DOOR,
    CHARA_END
};

/* gamed.c is now compiled from source */
