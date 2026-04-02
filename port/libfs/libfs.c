/**
 * Port filesystem — simple stdio replacement for PSX CD-ROM.
 * Reads game data directly from extracted disc files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "libfs/libfs.h"
#include "libgv/libgv.h"

/*---------------------------------------------------------------------------*/
/* Configuration                                                             */
/*---------------------------------------------------------------------------*/

#ifndef PORT_DATA_PATH
#define PORT_DATA_PATH "data/disc1/MGS/"
#endif

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

static FILE *stage_dir_file = NULL;
static FILE *dat_files[7] = {};

/* Stage directory table */
#define FS_DIRNAME_MAX  8
#define MAX_STAGES      170

typedef struct {
    char name[FS_DIRNAME_MAX];
    int  offset;  /* in sectors */
} DirEntry;

static DirEntry stage_table[MAX_STAGES];
static int      stage_count = 0;

/* Required externs */
FS_FILE_INFO fs_file_info[] = {
    { "STAGE.DIR",  0 },
    { "RADIO.DAT",  0 },
    { "FACE.DAT",   0 },
    { "ZMOVIE.STR", 0 },
    { "VOX.DAT",    0 },
    { "DEMO.DAT",   0 },
    { "BRF.DAT",    0 },
    { NULL,         0 }
};

extern int FS_DiskNum;

/* Exported for main_game.c to display */
void *port_stage_nocache_data = NULL;
int port_stage_nocache_size = 0;

static void FS_CdStageFileInit_port(void);

/*---------------------------------------------------------------------------*/
/* CDBIOS — simple stubs (no async reads needed)                             */
/*---------------------------------------------------------------------------*/

int  CDBIOS_Reset(void) { return 0; }
void CDBIOS_TaskStart(void) {}
void CDBIOS_ReadRequest(void *buffer, unsigned int sector, unsigned int size, void *callback) { (void)buffer; (void)sector; (void)size; (void)callback; }
int  CDBIOS_ReadSync(void) { return 0; }
void CDBIOS_ForceStop(void) {}
int  CDBIOS_TaskState(void) { return 0; }

/*---------------------------------------------------------------------------*/
/* Helper: read bytes from STAGE.DIR at a sector offset                      */
/*---------------------------------------------------------------------------*/

static int stage_dir_read(void *buf, int sector_offset, int size)
{
    if (!stage_dir_file) return -1;
    long byte_offset = (long)sector_offset * FS_SECTOR_SIZE;
    fseek(stage_dir_file, byte_offset, SEEK_SET);
    return (int)fread(buf, 1, size, stage_dir_file);
}

/*---------------------------------------------------------------------------*/
/* FS_StartDaemon — open files and parse STAGE.DIR directory                 */
/*---------------------------------------------------------------------------*/

static const char *dat_names[] = {
    "STAGE.DIR", "RADIO.DAT", "FACE.DAT", "ZMOVIE.STR",
    "VOX.DAT", "DEMO.DAT", "BRF.DAT"
};

void FS_StartDaemon(void)
{
    printf("fs:");

    /* Open all data files */
    for (int i = 0; i < 7; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", PORT_DATA_PATH, dat_names[i]);
        dat_files[i] = fopen(path, "rb");
        if (dat_files[i])
            printf(" %s", dat_names[i]);
        else
            printf(" [%s:MISSING]", dat_names[i]);
    }
    stage_dir_file = dat_files[0];
    printf("\n");

    /* Parse STAGE.DIR directory table */
    if (stage_dir_file)
    {
        unsigned char header[2048];
        fread(header, 1, 2048, stage_dir_file);

        unsigned int table_size = *(unsigned int *)header;
        stage_count = table_size / 12;
        if (stage_count > MAX_STAGES) stage_count = MAX_STAGES;

        /* Copy entries from header+4 */
        memcpy(stage_table, header + 4, stage_count * sizeof(DirEntry));

        printf("  [fs] %d stages in STAGE.DIR:", stage_count);
        for (int i = 0; i < stage_count && i < 10; i++)
            printf(" %.*s", FS_DIRNAME_MAX, stage_table[i].name);
        if (stage_count > 10) printf(" ...");
        printf("\n");

        /* Also set up the original cdstage.c data structures so
           FS_CdGetStageFileTop works (called by stageld.c) */
        FS_CdStageFileInit_port();
    }

    FS_DiskNum = 0;
    printf("  [fs] FS_StartDaemon complete\n");
    fflush(stdout);
}

static void FS_CdStageFileInit_port(void)
{
    /* Nothing to do — stage_table is already populated.
       FS_CdGetStageFileTop uses our static table directly. */
}

/* Override the stage lookup — search our parsed table directly.
   This replaces the one in cdstage.c. */
int FS_CdGetStageFileTop(char *dirname)
{
    for (int i = 0; i < stage_count; i++)
    {
        if (strncmp(stage_table[i].name, dirname, FS_DIRNAME_MAX) == 0)
        {
            return stage_table[i].offset;  /* sector offset within STAGE.DIR */
        }
    }
    printf("  [fs] Stage '%s' not found!\n", dirname);
    return -1;
}

/* Also override FS_CdStageFileInit since cdstage.c's version tries CD reads */
void FS_CdStageFileInit(void *buffer, int sector)
{
    (void)buffer;
    (void)sector;
    /* Already initialized in FS_StartDaemon */
}

/*---------------------------------------------------------------------------*/
/* Stage loading — simplified direct read                                    */
/*---------------------------------------------------------------------------*/

/* Simplified stage info for the port */
typedef struct {
    int   loaded;
    void *buffer;
    int   size;
    DATACNF *datacnf;
} PortStageInfo;

static PortStageInfo port_stage_info;

void *FS_LoadStageRequest(const char *dirname)
{
    int sector = FS_CdGetStageFileTop((char *)dirname);
    if (sector < 0)
    {
        port_stage_info.loaded = 0;
        return &port_stage_info;
    }

    printf("  [fs] Loading stage '%s' at sector %d...\n", dirname, sector);

    /* Read the DATACNF header (first sector) */
    unsigned char header[FS_SECTOR_SIZE];
    stage_dir_read(header, sector, FS_SECTOR_SIZE);

    DATACNF *cnf = (DATACNF *)header;
    int total_size = cnf->size * FS_SECTOR_SIZE;
    printf("  [fs]   DATACNF: version=%d, size=%d sectors (%d bytes)\n",
           cnf->version, cnf->size, total_size);

    /* Allocate buffer and read the entire stage data */
    void *buffer = GV_AllocMemory(GV_NORMAL_MEMORY, total_size);
    if (!buffer)
    {
        printf("  [fs]   ERROR: Could not allocate %d bytes for stage\n", total_size);
        port_stage_info.loaded = 0;
        return &port_stage_info;
    }

    stage_dir_read(buffer, sector, total_size);

    port_stage_info.loaded = 1;
    port_stage_info.buffer = buffer;
    port_stage_info.size = total_size;
    port_stage_info.datacnf = (DATACNF *)buffer;

    /* Process the DATACNF tags */
    DATACNF_TAG *tag = port_stage_info.datacnf->tags;
    int tag_num = 0;
    while (tag->mode != 0)
    {
        // printf("  [fs]   tag[%d]: id=0x%04X mode='%c' ext='%c' size=%d\n",
        //        tag_num, tag->id, tag->mode,
        //        (tag->ext != (char)0xff) ? tag->ext : '?', tag->size);
        tag++;
        tag_num++;
    }

    printf("  [fs]   Loaded %d bytes, %d tags\n", total_size, tag_num);

    /* Process DATACNF tags — parse archives and load resources */
    tag = port_stage_info.datacnf->tags;
    /* Data starts after the DATACNF header (first sector contains header + tags) */
    char *data_ptr = (char *)buffer + FS_SECTOR_SIZE;

    while (tag->mode != 0)
    {
        if (tag->mode == 'r')
        {
            /* DAR archive — sequence of DARFILE_TAG entries */
            extern int FS_ResidentCacheDirty;
            FS_ResidentCacheDirty = 1;
            int region = (tag->mode == 'r') ? GV_REGION_RESIDENT : GV_REGION_NOCACHE;
            DARFILE_TAG *dar = (DARFILE_TAG *)data_ptr;
            int remaining = tag->size;

            printf("  [fs]   Processing '%c' archive (%d bytes):\n", tag->mode, tag->size);
            fflush(stdout);

            while (remaining > (int)sizeof(DARFILE_TAG))
            {
                int entry_size = dar->size + 8; /* 8 bytes header + data */
                if (entry_size <= 0 || entry_size > remaining) break;

                int cache_id = ((dar->ext - 'a') << 16) | dar->id;
                void *file_data = (void *)(dar + 1); /* data after the tag */

                if (region == GV_REGION_RESIDENT)
                {
                    /* Copy to permanent memory — original uses GV_AllocResidentMemory
                       but we use malloc since resident data must survive stage reloads */
                    void *perm = malloc(dar->size);
                    if (perm) {
                        memcpy(perm, file_data, dar->size);
                        file_data = perm;
                    }
                }

               // printf("    dar: id=0x%04X ext='%c' size=%d\n", dar->id, (char)dar->ext, dar->size);

                GV_LoadInit(file_data, cache_id, region);

                remaining -= entry_size;
                dar = (DARFILE_TAG *)((char *)dar + entry_size);
            }

            data_ptr += (tag->size + (FS_SECTOR_SIZE - 1)) & ~(FS_SECTOR_SIZE - 1);
        }
        else if (tag->mode == 'c')
        {
            /* Cache entries use OFFSET-based addressing.
               The 'size' field is actually an offset from the c-region base.
               The c-region base is data_ptr at the first 'c' tag.
               Process all consecutive 'c' tags together. */
            char *c_base = data_ptr;
            int cache_count = 0;

            while (tag->mode == 'c')
            {
                if (tag->ext == (char)0xff)
                {
                    /* Terminator: size = total c-region size */
                    //printf("    c-term: total_size=%d\n", tag->size);
                    data_ptr = c_base + ((tag->size + 3) & ~3);
                    tag++;
                    break;
                }

                /* Find the next tag to compute this entry's size */
                DATACNF_TAG *next = tag + 1;
                int entry_offset = tag->size;  /* offset from c_base */
                int entry_size = next->size - entry_offset;  /* size = next_offset - this_offset */

                if (tag->id != 0 && entry_size > 0)
                {
                    char *entry_data = c_base + ((entry_offset + 3) & ~3);
                    int cache_id = ((tag->ext - 'a') << 16) | tag->id;
                    /* cache print silenced */
                    fflush(stdout);

                    /* GCL bytecode is stored in big-endian format.
                       GCL_GetLong reads big-endian, so no byte-swap needed. */

                    GV_LoadInit(entry_data, cache_id, GV_REGION_CACHE);
                }
                else
                {
                    /* skip print silenced */
                }

                tag++;
            }
            continue;  /* tag already advanced */
        }
        else if (tag->mode == 'n')
        {
            /* Nocache data — raw texture data for VRAM upload.
               Upload to VRAM texture area (below framebuffer). */
            printf("  [fs]   Nocache texture data: %d bytes\n", tag->size);
            port_stage_nocache_data = data_ptr;
            port_stage_nocache_size = tag->size;

            /* Nocache 'd' data is a sequence of LoadImage commands:
               each is a RECT (8 bytes: x,y,w,h as shorts) followed by pixel data.
               Parse and upload each rect to VRAM. */
            /* Nocache data is a DAR archive: sequence of DARFILE_TAGs + data.
               Each sub-file (typically ext='p' PCX) is loaded via GV_LoadInit. */
            {
                DARFILE_TAG *dar = (DARFILE_TAG *)data_ptr;
                int remaining = tag->size;
                int nc_count = 0;
                while (remaining > (int)sizeof(DARFILE_TAG))
                {
                    int entry_size = dar->size + 8;
                    if (entry_size <= 0 || entry_size > remaining) break;

                    int cache_id = ((dar->ext - 'a') << 16) | dar->id;
                    void *file_data = (void *)(dar + 1);

                    /* nocache dar print silenced */

                    GV_LoadInit(file_data, cache_id, GV_REGION_NOCACHE);
                    nc_count++;

                    remaining -= entry_size;
                    dar = (DARFILE_TAG *)((char *)dar + entry_size);
                }
                printf("  [fs]   Processed %d nocache entries\n", nc_count);
            }

            data_ptr += (tag->size + (FS_SECTOR_SIZE - 1)) & ~(FS_SECTOR_SIZE - 1);
        }
        else if (tag->mode == 's')
        {
            /* 's' mode: sound data or overlay binaries.
               ext='b' is the stage overlay binary — process it through the 'b' loader. */
            if (tag->ext == 'b') {
                int cache_id = (('b' - 'a') << 16) | tag->id;
                GV_LoadInit(data_ptr, cache_id, GV_REGION_NOCACHE);
            }
            data_ptr += (tag->size + (FS_SECTOR_SIZE - 1)) & ~(FS_SECTOR_SIZE - 1);
        }

        tag++;
    }

    printf("  [fs]   Stage processing complete\n");
    fflush(stdout);

    return &port_stage_info;
}

int FS_LoadStageSync(void *info)
{
    (void)info;
    return 0;  /* always complete immediately */
}

void FS_LoadStageComplete(void *info)
{
    (void)info;
    /* FS_ResidentCacheDirty is cleared by gamed.c after it saves resident caches.
       Do NOT clear it here — gamed.c reads it during the WAIT_LOAD transition. */
}

/*---------------------------------------------------------------------------*/
/* File loading                                                              */
/*---------------------------------------------------------------------------*/

void FS_LoadFileRequest(int fileno, int offset, int size, void *buffer)
{
    if (fileno < 0 || fileno >= 7 || !dat_files[fileno])
        return;
    fseek(dat_files[fileno], (long)offset * FS_SECTOR_SIZE, SEEK_SET);
    size_t r = fread(buffer, 1, size, dat_files[fileno]);
    if (r < (size_t)size)
        memset((char *)buffer + r, 0, size - r);
}

int FS_LoadFileSync(void) { return 0; }
void MakeFullPath(char *name, char *buffer) { (void)name; (void)buffer; }
int  FS_ResetCdFilePosition(void *buffer) { (void)buffer; return 0; }
void FS_CDInit(void) {}
int  FS_CdMakePositionTable(char *buffer, FS_FILE_INFO *finfo) { (void)buffer; (void)finfo; return 0; }
void FS_CdStageProgBinFix(void) {}

/*---------------------------------------------------------------------------*/
/* Movie / Memfile / Streaming — stubs                                       */
/*---------------------------------------------------------------------------*/

void FS_MovieFileInit(void *buffer, int sector) { (void)buffer; (void)sector; }
FS_MOVIE_FILE *FS_GetMovieInfo(unsigned int to_find) { (void)to_find; return NULL; }

void FS_EnableMemfile(int read, int write) { (void)read; (void)write; }
void FS_ClearMemfile(void) {}
int  FS_WriteMemfile(int id, int **buf_ptr, int size) { (void)id; (void)buf_ptr; (void)size; return 0; }
int  FS_ReadMemfile(int id, int **buf_ptr) { (void)id; (void)buf_ptr; return 0; }

void FS_StreamTaskStart(int sector) { (void)sector; }
int  FS_StreamTaskState(void) { return 0; }
void FS_StreamTaskInit(void) {}
int  FS_StreamSync(void) { return 0; }
void FS_StreamCD(void) {}
int  FS_StreamGetTop(int is_demo) { (void)is_demo; return 0; }
int  FS_StreamInit(void *pHeap, int heapSize) { (void)pHeap; (void)heapSize; return 0; }
void FS_StreamStop(void) {}
void FS_StreamOpen(void) {}
void FS_StreamClose(void) {}
int  FS_StreamIsEnd(void) { return 1; }
void *FS_StreamGetData(int target_type) { (void)target_type; return NULL; }
int  FS_StreamGetSize(void *stream) { (void)stream; return 0; }
void FS_StreamUngetData(void *stream) { (void)stream; }
void FS_StreamClear(void *stream) { (void)stream; }
void FS_StreamClearType(void *stream, int target_type) { (void)stream; (void)target_type; }
int  FS_StreamGetEndFlag(void) { return 1; }
int  FS_StreamIsForceStop(void) { return 1; /* port: skip FMV streams */ }
void FS_StreamTickStart(void) {}
void FS_StreamSoundMode(void) {}
int  FS_StreamGetTick(void) { return 0; }
