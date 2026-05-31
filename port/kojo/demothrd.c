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
#include <signal.h>
#include <setjmp.h>
static sigjmp_buf demo_sigbus_jmp;
static void demo_sigbus_handler(int sig) { siglongjmp(demo_sigbus_jmp, 1); }

#include "common.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libfs/libfs.h"
#include "libgcl/libgcl.h"

extern int demodebug_finish_proc;

/* Set when PORT_DEMO_PAUSE_AT has clamped lpAct->frame to its target;
   suppresses the natural end-of-demo GV_DestroyActor so the cinematic
   snapshot can be held for screenshot A/B against the editor. */
int port_demo_paused      = 0;
/* Demo scrubber (imgui_debug.cpp "Demo" tab). When active, every SA
   tick reads the seek target frame straight from DEMO.DAT and feeds
   it to FrameRunDemo, bypassing the streaming parser. */
int port_demo_seek_target = 700;
int port_demo_seek_active = 0;
int port_demo_max_frame   = 1980;
/* When set, the direct-from-disk feeder forces every adjust's
   visible flag to 1. The cinematic deliberately marks Snake (and
   other dolls) invisible during transitions / post-action frames,
   which on PSX cuts to the next shot. The editor's "show characters"
   marker render ignores the flag for inspection -- this toggle gives
   the port the same behavior so you can A/B against the editor at
   arbitrary frames. */
int port_demo_force_visible = 1;

/******************************************************************************
 * functions
 */

static void ActStream(LPMGSDEMOACT lpAct);
static void DieStream(LPMGSDEMOACT lpAct);
static void ActFile(LPMGSDEMOACT lpAct);
static void DieFile(LPMGSDEMOACT lpAct);

extern int port_fs_read_dat(int file_id, long byte_off, int len, void *buf);
extern BOOL FrameRunDemo(LPMGSDEMOACT lpAct, DMO_DAT *data);

/* Direct-from-disk DMO feeder. Bypasses the streaming SA actor's
 * heap parser (which truncates d00a around frame 176-643) and reads
 * a target frame's DMO_DAT straight from DEMO.DAT, then hands it to
 * FrameRunDemo. Caches the dmo file contents on first call. */
static void feed_paused_frame_direct(LPMGSDEMOACT lpAct, int target_frame,
                                     long base_sector)
{
    /* Static cache: read s0102a0.dmo once (it's <2MB). */
    static unsigned char *dmo_buf  = NULL;
    static long           dmo_size = 0;
    static long           cached_sector = -1;
    if (!dmo_buf || cached_sector != base_sector) {
        if (dmo_buf) { free(dmo_buf); dmo_buf = NULL; }
        dmo_size = 2 * 1024 * 1024;
        dmo_buf  = (unsigned char *)malloc(dmo_size);
        if (!dmo_buf) return;
        long byte_off = base_sector * 2048L;
        int got = port_fs_read_dat(5 /*FS_FILEID_DEMO*/, byte_off,
                                   (int)dmo_size, dmo_buf);
        if (got <= 0) { free(dmo_buf); dmo_buf = NULL; return; }
        dmo_size = got;
        cached_sector = base_sector;
        printf("[paused] cached %ld bytes of dmo at sector 0x%lX\n",
               dmo_size, base_sector);
    }

    /* Walk blocks. All DMO blocks share type=0x05 -- the FIRST is the
       DMO_DEF (header, frame field encodes something else / is 0),
       subsequent ones are DMO_DAT records, one per cinematic frame.
       Skip the first then match by data->frame. */
    long off = 0;
    int  seen_def = 0;
    static int once = 0;
    int blocks = 0, dats = 0, min_f = 99999, max_f = -1;
    while (off + 4 <= dmo_size) {
        unsigned int tag;
        memcpy(&tag, &dmo_buf[off], 4);
        unsigned int type = tag & 0xFF;
        unsigned int size = (tag >> 8) & 0xFFFFFF;
        blocks++;
        if (type == 0xFF) {
            /* WRAP marker: sector-pad to next 2K boundary and continue. */
            off = (off + 2048) & ~2047L;
            continue;
        }
        if (type == 0xF0 || size == 0 || size > 0x10000) {
            if (!once) printf("[paused] walker stop off=%ld type=0x%X size=%u blocks=%d dats=%d frame_range=[%d..%d]\n",
                              off, type, size, blocks, dats, min_f, max_f);
            once = 1;
            break;
        }
        if (off + size > (unsigned long)dmo_size) {
            if (!once) printf("[paused] walker oob off=%ld size=%u dmo_size=%ld blocks=%d dats=%d frame_range=[%d..%d]\n",
                              off, size, dmo_size, blocks, dats, min_f, max_f);
            once = 1;
            break;
        }
        if (type == 0x05) {
            if (!seen_def) {
                seen_def = 1;
                if (!once) printf("[paused] DMO_DEF at off=%ld size=%u\n", off, size);
                off += size;
                continue;
            }
            /* DMO_DAT: bytes 4..7 = frame */
            int frame_val;
            memcpy(&frame_val, &dmo_buf[off + 4], 4);
            dats++;
            if (frame_val < min_f) min_f = frame_val;
            if (frame_val > max_f) max_f = frame_val;
            if (frame_val == target_frame) {
                if (!once) printf("[paused] FOUND target=%d at off=%ld (after %d blocks, %d DMO_DATs)\n",
                                  target_frame, off, blocks, dats);
                once = 1;
                /* Build port_dat from this raw block. */
                static DMO_DAT port_dat;
                unsigned char *raw = &dmo_buf[off];
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
                memcpy(&port_dat.n_adjusts, &raw[32], 2);
                static DMO_CHA port_charas[16];
                static DMO_ADJ port_adjusts[32];
                static short   rots_buf[32][64 * 3];
                uint32_t chara_off; memcpy(&chara_off, &raw[28], 4);
                if (chara_off && port_dat.n_charas > 0 &&
                    port_dat.n_charas <= 16) {
                    memcpy(port_charas, raw + chara_off,
                           sizeof(DMO_CHA) * port_dat.n_charas);
                    port_dat.chara = port_charas;
                } else {
                    port_dat.chara = NULL;
                }
                uint32_t adj_off; memcpy(&adj_off, &raw[36], 4);
                if (adj_off && port_dat.n_adjusts > 0 &&
                    port_dat.n_adjusts <= 32) {
                    extern int port_demo_force_visible;
                    unsigned char *adj_raw = raw + adj_off;
                    for (int ai = 0; ai < port_dat.n_adjusts; ai++) {
                        unsigned char *a = adj_raw + ai * 24;
                        memcpy(&port_adjusts[ai].type,    &a[0],  4);
                        memcpy(&port_adjusts[ai].visible, &a[4],  2);
                        memcpy(&port_adjusts[ai].rot_x,   &a[6],  2);
                        memcpy(&port_adjusts[ai].rot_y,   &a[8],  2);
                        memcpy(&port_adjusts[ai].rot_z,   &a[10], 2);
                        memcpy(&port_adjusts[ai].pos_x,   &a[12], 2);
                        memcpy(&port_adjusts[ai].pos_y,   &a[14], 2);
                        memcpy(&port_adjusts[ai].pos_z,   &a[16], 2);
                        memcpy(&port_adjusts[ai].n_rots,  &a[18], 2);
                        /* Force visible=1 when scrubbing so dolls the
                         * cinematic deliberately hides (e.g. Snake during
                         * the post-climb transition at f>700) still
                         * render -- matches the editor's marker render. */
                        if (port_demo_force_visible &&
                            (port_adjusts[ai].pos_x | port_adjusts[ai].pos_y |
                             port_adjusts[ai].pos_z))
                        {
                            port_adjusts[ai].visible = 1;
                        }
                        uint32_t rots_off;
                        memcpy(&rots_off, &a[20], 4);
                        if (rots_off && port_adjusts[ai].n_rots > 0) {
                            int n = port_adjusts[ai].n_rots;
                            if (n > 64) n = 64;
                            memcpy(rots_buf[ai], a + rots_off,
                                   n * 3 * sizeof(short));
                            port_adjusts[ai].rots = rots_buf[ai];
                        } else {
                            port_adjusts[ai].rots = NULL;
                        }
                    }
                    port_dat.adjust = port_adjusts;
                } else {
                    port_dat.adjust = NULL;
                }
                static int dbg = 0;
                if (!dbg) {
                    printf("[paused] feeding frame=%d eye=(%d,%d,%d) "
                           "n_adjusts=%d adj_off=%u\n",
                           port_dat.frame, port_dat.eye_x, port_dat.eye_y,
                           port_dat.eye_z, port_dat.n_adjusts, adj_off);
                    for (int xi = 0; xi < port_dat.n_adjusts; xi++) {
                        printf("[paused]   adj[%d] type=%d visible=%d "
                               "pos=(%d,%d,%d)\n",
                               xi, port_adjusts[xi].type,
                               port_adjusts[xi].visible,
                               port_adjusts[xi].pos_x,
                               port_adjusts[xi].pos_y,
                               port_adjusts[xi].pos_z);
                    }
                    dbg = 1;
                }
                int rc = FrameRunDemo(lpAct, &port_dat);
                static int dbg2 = 0;
                if (!dbg2) { printf("[paused] FrameRunDemo returned %d\n", rc); dbg2 = 1; }
                return;
            }
        }
        off += size;
    }
}

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

    {
        static int actc = 0;
        if (actc++ < 200 || (actc % 60) == 0) {
            printf("[ActStream] call#%d frame=%d ticks=%d\n", actc, lpAct->frame, ticks);
        }
    }

    if (lpAct->frame == -1)
    {
        data = FS_StreamGetData(5);
        printf("[StreamAct] frame=%d data=%p ticks=%d\n", lpAct->frame, data, ticks);

        if (data)
        {
            /* DMO_DEF in stream data uses PSX 28-byte layout with 32-bit
               pointer fields. Convert to 64-bit struct. */
            {
                unsigned char *raw = (unsigned char *)(data - 4);
                static DMO_DEF port_def;
                memcpy(&port_def.tag,      &raw[0],  4);
                memcpy(&port_def.frame,    &raw[4],  4);
                memcpy(&port_def.n_frames, &raw[8],  4);
                memcpy(&port_def.n_maps,   &raw[12], 4);
                memcpy(&port_def.n_models, &raw[16], 4);
                /* 32-bit offsets at raw[20] and raw[24] are relative to raw */
                uint32_t maps_off, models_off;
                memcpy(&maps_off,   &raw[20], 4);
                memcpy(&models_off, &raw[24], 4);
                printf("[DEMO] DMO_DEF: n_frames=%d n_maps=%d n_models=%d\n",
                       port_def.n_frames, port_def.n_maps, port_def.n_models);
                /* Validate offsets — must be reasonable (within ~64KB of struct start) */
                if (maps_off > 0 && maps_off < 0x10000)
                    port_def.maps = (DMO_MAP *)(raw + maps_off);
                else
                    port_def.maps = NULL;
                if (models_off > 0 && models_off < 0x10000)
                    port_def.models = (DMO_MDL *)(raw + models_off);
                else
                    port_def.models = NULL;
                def = &port_def;
            }
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

    {
        static int _sa = 0;
        int dbg = getenv("PORT_DEBUG_SNAKE") && atoi(getenv("PORT_DEBUG_SNAKE")) > 0;
        if (_sa < 5 || (dbg && (_sa % 60 == 0)))
            printf("[SA] tick=%d start=%d frame=%d/%d\n", ticks, lpAct->start_time, lpAct->frame, lpAct->header->n_frames);
        _sa++;
    }

    /* Optional: pause cinema at a specific demo frame so the port can
       be screenshot-compared to the editor at the same frame. Hold by
       sliding start_time forward each tick once frame reaches the
       target. >= so we lock as soon as we hit the target. Sets the
       extern flag port_demo_paused so the stream-end / destruction
       path below skips its GV_DestroyActor (the natural end-of-demo
       would clear the snapshot we're trying to hold). */
    extern int port_demo_paused;
    {
        static int pause_at = -2;
        if (pause_at == -2) {
            const char *e = getenv("PORT_DEMO_PAUSE_AT");
            pause_at = (e && *e) ? atoi(e) : -1;
            if (pause_at >= 0) printf("[SA] PORT_DEMO_PAUSE_AT=%d engaged\n", pause_at);
        }
        if (pause_at >= 0) {
            if (port_demo_paused || lpAct->frame >= pause_at) {
                if (!port_demo_paused) {
                    printf("[SA] paused at frame=%d (target=%d)\n", lpAct->frame, pause_at);
                    port_demo_paused = 1;
                }
                lpAct->frame = pause_at;
                lpAct->start_time = ticks - pause_at * 2;
            }
        }
    }

    if (!lpAct->header) return;
    /* Demo scrubber: when active (env PORT_DEMO_PAUSE_AT or imgui's
       "Enable scrubber" toggle), feed the target frame's data straight
       from DEMO.DAT. PORT_DEMO_BASE_SECTOR defaults to 0x1441
       (s0102a0.dmo / d00a). MUST run AFTER the lpAct->frame == -1
       block so CreateDemo has set up the models. */
    {
        extern int  port_demo_paused;
        extern int  port_demo_seek_target;
        extern int  port_demo_seek_active;
        static int  env_pause_at = -2;
        static long base_sector  = 0x1441;
        if (env_pause_at == -2) {
            const char *e = getenv("PORT_DEMO_PAUSE_AT");
            env_pause_at  = (e && *e) ? atoi(e) : -1;
            const char *bs = getenv("PORT_DEMO_BASE_SECTOR");
            if (bs && *bs) base_sector = strtol(bs, NULL, 0);
            if (env_pause_at >= 0) {
                port_demo_seek_active = 1;
                port_demo_seek_target = env_pause_at;
            }
        }
        /* CreateDemo runs in the lpAct->frame == -1 branch -- only
           engage the scrubber after that's complete (frame >= 0). */
        if (port_demo_seek_active && lpAct->frame >= 0) {
            if (!port_demo_paused) {
                printf("[SA] scrubber engaged target=%d cur_frame=%d "
                       "(stream-end=%d)\n",
                       port_demo_seek_target, lpAct->frame,
                       FS_StreamGetEndFlag());
                port_demo_paused = 1;
            }
            feed_paused_frame_direct(lpAct, port_demo_seek_target, base_sector);
            lpAct->frame      = port_demo_seek_target;
            lpAct->start_time = ticks - port_demo_seek_target * 2;
            return;
        }
    }
    if (lpAct->frame <= lpAct->header->n_frames)
    {
        while (1)
        {
            data = FS_StreamGetData(5);

            if (!data)
            {
                extern int port_demo_paused;
                if (FS_StreamGetEndFlag() == 1 && !port_demo_paused)
                {
                    GV_DestroyActor(&lpAct->actor);
                }

                return;
            }

            def = (DMO_DEF *)(data - 4);
            {
                int frame_val;
                memcpy(&frame_val, (unsigned char *)def + 4, 4);
                if (frame_val >= lpAct->frame) break;
            }

            FS_StreamClear(data);
        }

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
            static DMO_ADJ port_adjusts[32];
            if (chara_off && port_dat.n_charas > 0 && port_dat.n_charas <= 16) {
                memcpy(port_charas, raw + chara_off, sizeof(DMO_CHA) * port_dat.n_charas);
                port_dat.chara = port_charas;
            } else {
                port_dat.chara = NULL;
            }
            memcpy(&port_dat.n_adjusts, &raw[32], 2);
            uint32_t adj_off;   memcpy(&adj_off,   &raw[36], 4);
            if (adj_off && port_dat.n_adjusts > 0 && port_dat.n_adjusts <= 32) {
                /* PSX DMO_ADJ is 24 bytes (short* is 4), port is 32 (short* is 8).
                   Parse each entry from stream with PSX stride. */
                unsigned char *adj_raw = raw + adj_off;
                int ai;
                for (ai = 0; ai < port_dat.n_adjusts; ai++) {
                    unsigned char *a = adj_raw + ai * 24;
                    memcpy(&port_adjusts[ai].type,    &a[0],  4);
                    memcpy(&port_adjusts[ai].visible, &a[4],  2);
                    memcpy(&port_adjusts[ai].rot_x,   &a[6],  2);
                    memcpy(&port_adjusts[ai].rot_y,   &a[8],  2);
                    memcpy(&port_adjusts[ai].rot_z,   &a[10], 2);
                    memcpy(&port_adjusts[ai].pos_x,   &a[12], 2);
                    memcpy(&port_adjusts[ai].pos_y,   &a[14], 2);
                    memcpy(&port_adjusts[ai].pos_z,   &a[16], 2);
                    memcpy(&port_adjusts[ai].n_rots,  &a[18], 2);
                    {
                        static int _adj_dbg = 0;
                        if (_adj_dbg < 20)
                            printf("[DMO_ADJ] ai=%d type=%d pos=(%d,%d,%d) raw=[%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x%02x%02x]\n",
                                   ai, port_adjusts[ai].type,
                                   port_adjusts[ai].pos_x, port_adjusts[ai].pos_y, port_adjusts[ai].pos_z,
                                   a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],
                                   a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],
                                   a[20],a[21],a[22],a[23]);
                        _adj_dbg++;
                    }
                    uint32_t rots_off; memcpy(&rots_off, &a[20], 4);
                    if (rots_off && port_adjusts[ai].n_rots > 0) {
                        /* Copy rots to aligned buffer — raw stream data may be
                           misaligned causing SIGBUS on ARM64. */
                        static short rots_buf[32][64 * 3]; /* [adj_idx][rot*3] */
                        int n = port_adjusts[ai].n_rots;
                        if (n > 64) n = 64;
                        memcpy(rots_buf[ai], a + rots_off, n * 3 * sizeof(short));
                        port_adjusts[ai].rots = rots_buf[ai];
                    } else {
                        port_adjusts[ai].rots = NULL;
                    }
                }
                port_dat.adjust = port_adjusts;
            } else {
                port_dat.adjust = NULL;
            }
            status = FrameRunDemo(lpAct, &port_dat);
        }

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
        printf("[SA] DESTROY: status=%d frame=%d/%d\n", status, lpAct->frame, lpAct->header->n_frames);
        GV_DestroyActor(&lpAct->actor);
    }
}

static void DieStream(LPMGSDEMOACT lpAct)
{
    printf("[DieStream] frame=%d\n", lpAct->frame);
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
