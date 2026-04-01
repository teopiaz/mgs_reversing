/**
 * Stub definitions for external symbols not yet provided by compiled source.
 * Remove stubs from here as real implementations are compiled in.
 */

#include <stdio.h>
#include <string.h>

/* game globals */
int GM_CurrentMap = 0;
int gTotalFrameTime = 0;

/* overlays / chara registration */
extern void *_StageCharacterEntries;
void *StageCharacterEntries = (void *)0;

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
