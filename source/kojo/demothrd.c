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

#ifdef PORT_BUILD
    {
        static int _sa = 0;
        if (_sa < 5)
            printf("[SA] tick=%d start=%d frame=%d/%d\n", ticks, lpAct->start_time, lpAct->frame, lpAct->header->n_frames);
        _sa++;
    }
#endif

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
        /* DMO_DAT in stream has PSX 36-byte layout. Convert pointers.
           Use memcpy for unaligned reads (ARM64 SIGBUS on misaligned int). */
        {
            static DMO_DAT port_dat;
            unsigned char *raw = (unsigned char *)def;
            memcpy(&port_dat.tag,       &raw[0],  4);
            memcpy(&port_dat.frame,     &raw[4],  4);
            memcpy(&port_dat.eye_x,     &raw[8],  2);
            memcpy(&port_dat.eye_y,     &raw[10], 2);
            memcpy(&port_dat.eye_z,     &raw[12], 2);
            memcpy(&port_dat.center_x,  &raw[14], 2);
            memcpy(&port_dat.center_y,  &raw[16], 2);
            memcpy(&port_dat.center_z,  &raw[18], 2);
            memcpy(&port_dat.roll,      &raw[20], 2);
            memcpy(&port_dat.clip_dist, &raw[22], 2);
            memcpy(&port_dat.n_charas,  &raw[24], 2);
            /* PSX DMO_DAT layout:
               offset 24: n_charas (short)
               offset 26: padding (2 bytes, for pointer alignment)
               offset 28: chara (4-byte PSX pointer, relative offset)
               offset 32: n_adjusts (short)
               offset 34: padding (2 bytes)
               offset 36: adjust (4-byte PSX pointer, relative offset)
               Total PSX size: 40 bytes */
            uint32_t chara_off; memcpy(&chara_off, &raw[28], 4);
            /* Copy chara/adjust data to aligned static buffers */
            static DMO_CHA port_charas[16];
            static DMO_ADJ port_adjusts[16];
            if (chara_off && port_dat.n_charas > 0 && port_dat.n_charas <= 16) {
                memcpy(port_charas, raw + chara_off, sizeof(DMO_CHA) * port_dat.n_charas);
                port_dat.chara = port_charas;
            } else {
                port_dat.chara = NULL;
            }
            memcpy(&port_dat.n_adjusts, &raw[32], 2);
            uint32_t adj_off;   memcpy(&adj_off,   &raw[36], 4);
            if (adj_off && port_dat.n_adjusts > 0 && port_dat.n_adjusts <= 16) {
                memcpy(port_adjusts, raw + adj_off, sizeof(DMO_ADJ) * port_dat.n_adjusts);
                port_dat.adjust = port_adjusts;
            } else {
                port_dat.adjust = NULL;
            }
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
