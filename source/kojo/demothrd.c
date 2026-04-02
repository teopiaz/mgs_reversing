/******************************************************************************
 * System   : METALGEAR^3 for PlayStation
 * Computer : PlayStation
 * OS       : PlayStation
 * Compiler : psyq
 * Module   : 
 */

/******************************************************************************
 * included
 */

#include "demo.h"

#include <stdio.h>
#include <libsn.h>

#include "common.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libfs/libfs.h"
#include "libgcl/libgcl.h"

extern int demodebug_finish_proc;

/******************************************************************************
 * functions
 */

static void ActStream(LPMGSDEMOACT lpAct);
static void DieStream(LPMGSDEMOACT lpAct);
static void ActFile(LPMGSDEMOACT lpAct);
static void DieFile(LPMGSDEMOACT lpAct);

/******************************************************************************
 * publics
 */

int DM_ThreadStream(int flag, int unused)
{
    LPMGSDEMOACT lpAct;

    printf("[DM_ThreadStream] flag=%d\n", flag);

    lpAct = GV_NewActor(GV_ACTOR_MANAGER, sizeof(MGSDEMOACT));
    if (!lpAct)
    {
        printf("[DM_ThreadStream] FAILED to create actor\n");
        return 0;
    }

    lpAct->flag = flag;
    lpAct->frame = -1;

    GV_SetNamedActor(&lpAct->actor, &ActStream, &DieStream, "demothrd.c");

    lpAct->map = GM_CurrentMap;
    FS_StreamOpen();
    return 1;
}

int DM_ThreadFile(int flag, char *filename)
{
    LPMGSDEMOACT lpAct;
    int         fd;
    char       *buffer;
    int         fsize, length;

    lpAct = GV_NewActor(GV_ACTOR_MANAGER, sizeof(MGSDEMOACT));

    if ( !lpAct )
    {
        return 0;
    }

    lpAct->flag = flag;
    lpAct->frame = -1;

    GV_SetNamedActor(&lpAct->actor, &ActFile, &DieFile, "demothrd.c");

    lpAct->map = GM_CurrentMap;
    FS_EnableMemfile(0, 0);
    lpAct->stream = (void *)0x80200000;

    MakeFullPath(filename, (char *)&lpAct->chain.used);
    printf("Demo file = \"%s\"\n", (char *)&lpAct->chain.used);

    fd = PCopen((char *)&lpAct->chain.used, 0, 0);
    if ( fd < 0 )
    {
        printf("\"%s\" not found\n", (char *)&lpAct->chain.used);
        GV_DestroyActor(&lpAct->actor);
        return 0;
    }

    fsize = PClseek(fd, 0, 2);
    PClseek(fd, 0, 0);

    buffer = (char *)lpAct->stream;

    while ( fsize > 0 )
    {
        length = (fsize <= 0x8000) ? fsize : 0x8000;
        length = PCread(fd, buffer, length);

        fsize -= length;

        if ( length < 0 )
        {
            PCclose(fd);
            GV_DestroyActor(&lpAct->actor);
            return 0;
        }

        buffer += length;
    }

    PCclose(fd);
    return 1;
}

/******************************************************************************
 * statics
 */

static void ActStream(LPMGSDEMOACT lpAct)
{
    int      ticks;
    char    *data;
    int      status;
    int      temp;
    DMO_DEF *def;

    ticks = FS_StreamGetTick();

    if (lpAct->frame == -1)
    {
        data = FS_StreamGetData(5);
        printf("[StreamAct] frame=%d data=%p ticks=%d\n", lpAct->frame, data, ticks);

        if (data)
        {
#ifdef PORT_BUILD
            /* DMO_DEF in stream data uses PSX 28-byte layout with 32-bit
               pointer fields. Convert to 64-bit struct. */
            {
                unsigned char *raw = (unsigned char *)(data - 4);
                static DMO_DEF port_def;
                port_def.tag      = *(unsigned int *)&raw[0];
                port_def.frame    = *(int *)&raw[4];
                port_def.n_frames = *(int *)&raw[8];
                port_def.n_maps   = *(int *)&raw[12];
                port_def.n_models = *(int *)&raw[16];
                /* 32-bit offsets at raw[20] and raw[24] are relative to raw */
                uint32_t maps_off   = *(uint32_t *)&raw[20];
                uint32_t models_off = *(uint32_t *)&raw[24];
                port_def.maps   = (DMO_MAP *)(raw + maps_off);
                port_def.models = (DMO_MDL *)(raw + models_off);
                def = &port_def;
            }
#else
            def = (DMO_DEF *)(data - 4);
#endif
            status = CreateDemo(lpAct, def);

            FS_StreamClear(data);

            if (status == 0)
            {
                GV_DestroyActor(&lpAct->actor);
            }

            lpAct->frame = 0;
        }

        return;
    }

    if (lpAct->start_time == 0)
    {
        lpAct->start_time = ticks - 2;
    }

    lpAct->frame = (ticks - lpAct->start_time) / 2;
    status = 0;
    temp = 0;

    if (lpAct->frame <= lpAct->header->n_frames)
    {
        while (1)
        {
            data = FS_StreamGetData(5);

            if (!data)
            {
                if (FS_StreamGetEndFlag() == 1)
                {
                    GV_DestroyActor(&lpAct->actor);
                }

                return;
            }

            def = (DMO_DEF *)(data - 4);
            if (def->frame >= lpAct->frame)
            {
                break;
            }

            FS_StreamClear(data);
        }

#ifdef PORT_BUILD
        /* DMO_DAT in stream has PSX 36-byte layout. Convert pointers. */
        {
            static DMO_DAT port_dat;
            unsigned char *raw = (unsigned char *)def;
            port_dat.tag       = *(unsigned int *)&raw[0];
            port_dat.frame     = *(int *)&raw[4];
            port_dat.eye_x     = *(short *)&raw[8];
            port_dat.eye_y     = *(short *)&raw[10];
            port_dat.eye_z     = *(short *)&raw[12];
            port_dat.center_x  = *(short *)&raw[14];
            port_dat.center_y  = *(short *)&raw[16];
            port_dat.center_z  = *(short *)&raw[18];
            port_dat.roll      = *(short *)&raw[20];
            port_dat.clip_dist = *(short *)&raw[22];
            port_dat.n_charas  = *(short *)&raw[24];
            uint32_t chara_off = *(uint32_t *)&raw[26];
            port_dat.chara     = chara_off ? (DMO_CHA *)(raw + chara_off) : NULL;
            port_dat.n_adjusts = *(short *)&raw[30];
            uint32_t adj_off   = *(uint32_t *)&raw[32];
            port_dat.adjust    = adj_off ? (DMO_ADJ *)(raw + adj_off) : NULL;
            status = FrameRunDemo(lpAct, &port_dat);
        }
#else
        status = FrameRunDemo(lpAct, (DMO_DAT *)def);
#endif

        if (status == 0)
        {
            FS_StreamStop();
        }
        else
        {
            FS_StreamClear(data);
        }
    }

    if (status == temp)
    {
        GV_DestroyActor(&lpAct->actor);
    }
}

static void DieStream(LPMGSDEMOACT lpAct)
{
    DestroyDemo(lpAct);
    FS_StreamClose();
    DG_UnDrawFrameCount = 0x7fff0000;
}

static void ActFile(LPMGSDEMOACT lpAct)
{
    int time;
    int new_time;
    int success;

    time = VSync(-1);

    if (lpAct->frame == -1)
    {
        if (!CreateDemo(lpAct, (DMO_DEF *)lpAct->stream))
        {
            printf("Error:Initialize demo\n");
            GV_DestroyActor(&lpAct->actor);
        }

        lpAct->frame = 0;
        return;
    }

    if (lpAct->start_time == 0)
    {
        lpAct->start_time = time - 2;
        printf("PlayDemoSound\n");
    }

    if (lpAct->flag & 4)
    {
        new_time = lpAct->frame + 1;
    }
    else
    {
        new_time = (time - lpAct->start_time) / 2;
    }

    lpAct->frame = new_time;

    if (lpAct->header->n_frames < lpAct->frame)
    {
        success = 0;
    }
    else
    {
        while (lpAct->frame != lpAct->stream->frame)
        {
            lpAct->stream = (DMO_DEF *)((char *)lpAct->stream + lpAct->stream->tag);
        }

        success = FrameRunDemo(lpAct, (DMO_DAT *)lpAct->stream);
    }

    if (GV_PadData[1].status & PAD_CROSS)
    {
        success = 0;
    }

    if (success == 0)
    {
        GV_DestroyActor(&lpAct->actor);
    }
}

static void DieFile(LPMGSDEMOACT lpAct)
{
    DestroyDemo(lpAct);
    FS_EnableMemfile(1, 1);

    if (demodebug_finish_proc != -1)
    {
        GCL_ExecProc(demodebug_finish_proc, NULL);
    }
}
