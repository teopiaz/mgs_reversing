/* Port implementation of the abst-stage LOAD_DATA chara (id 0x53c7).
 *
 * The original lives in the undecompiled abst overlay at PSX 0x800c4564.
 * All the machinery it drives is already decompiled in menu/datasave.c
 * (file-mode card/file picker, loadFile_80049CE8 -> GCL_SetLoadFile which
 * restores stage name + GCL variables + radio memory), so this actor only
 * has to run that UI in load mode and request the stage switch when done.
 *
 * Registered via chara_overrides.c, so the stage table's raw PSX address
 * is never consulted.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "libgv/libgv.h"
#include "libgcl/libgcl.h"
#include "game/game.h"
#include "menu/menuman.h"
#include "menu/radio.h"

#define EXEC_LEVEL GV_ACTOR_USER

extern MenuWork  Work;                /* menuman.c resident actor work
                                         (was gMenuWork_800BD360) */
#define gMenuWork_800BD360 Work
extern DATA_INFO dataInfo_8009EC30;   /* datasave.c "SAVE DATA" info; load mode
                                         overrides the captions itself */
extern char      gStageName_800B4D88[16];

typedef struct _LoadDataWork
{
    GV_ACT actor;
    int    done;
} LoadDataWork;

static void LoadDataAct(LoadDataWork *work)
{
    int ret;

    if (work->done)
    {
        return;
    }

    /* 0 = running, 1 = closed without loading, 2 = save loaded */
    ret = menu_radio_do_file_mode(&gMenuWork_800BD360, &GV_PadData[0]);
    if (ret == 0)
    {
        return;
    }

    work->done = 1;
    sub_8004124C(&gMenuWork_800BD360); /* free the font kcb we allocated */

    if (ret == 2)
    {
        /* GCL_SetLoadFile already restored gStageName + variables.
           0x91 = undraw frames (0x80) | keep vars (0x10) | stage load (1). */
        printf("[port] load_data: loading save -> stage '%s'\n", gStageName_800B4D88);
        GM_LoadRequest = 0x91;
    }
    else
    {
        printf("[port] load_data: cancelled, returning to title\n");
        strcpy(gStageName_800B4D88, "title");
        GM_LoadRequest = 0x81;
    }
}

static void LoadDataDie(LoadDataWork *work)
{
}

void *NewLoadData_port(int name, int where, int argc, char **argv)
{
    LoadDataWork *work;
    extern char  *port_dword_800ABB8C_ptr; /* datasave.c area-name script alias */

    /* The abst LOAD_DATA chara command carries the per-area name strings as
       its trailing positional args:
           chara &LOAD_DATA $s:c8bb sub_5FD9 m"Dock" m"Heliport" ...
       GM_Command_chara already consumed the chara id + name, so the GCL parse
       cursor now sits on the 3rd arg (the completion proc, sub_5FD9). Skip it
       and stash the full 64-bit pointer to the first area-name string, which
       getAreaName_8004CF20() walks via GCL_SetArgTop(). The original abst chara
       did the same through dword_800ABB8C, but that int truncates a host
       pointer on 64-bit, so the port reads it back through this alias instead.
       Without it getAreaName returns early, areaName stays uninitialised, and
       the file-picker's sprintf("%s") dereferences garbage and crashes. */
    GCL_GetNextInt();                               /* skip completion proc */
    port_dword_800ABB8C_ptr = (char *)GCL_NextStr(); /* first area-name string */

    work = GV_NewActor(EXEC_LEVEL, sizeof(LoadDataWork));
    if (work != NULL)
    {
        GV_SetNamedActor(&work->actor, (GV_ACTFUNC)LoadDataAct, (GV_ACTFUNC)LoadDataDie,
                         "load_data_chara.c");
        init_radio_message_board_80040F74(&gMenuWork_800BD360);
        init_file_mode(&dataInfo_8009EC30, 1); /* 1 = load mode */
        work->done = 0;
    }
    return work;
}
