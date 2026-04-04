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
               PSX loads each file from sector boundaries on CD.
               Align data_ptr to next sector boundary relative to buffer start. */
            {
                uintptr_t base = (uintptr_t)port_stage_info.buffer;
                uintptr_t cur = (uintptr_t)data_ptr;
                uintptr_t off = cur - base;
                uintptr_t aligned = (off + FS_SECTOR_SIZE - 1) & ~(FS_SECTOR_SIZE - 1);
                data_ptr = (char *)base + aligned;
            }
            if (tag->ext == 'b') {
                int cache_id = (('b' - 'a') << 16) | tag->id;
                GV_LoadInit(data_ptr, cache_id, GV_REGION_NOCACHE);
            }
            else if (tag->ext == 'w') {
                /* .wvx wave data — load directly to SPU RAM.
                   Parse the wave header ourselves and upload via SpuWrite. */
                /* WAVE_W from sd_incl.h — addr is unsigned long (8 bytes on port) */
                typedef struct { unsigned long addr; char sample_note; char sample_tune;
                    unsigned char a_mode, ar, dr, s_mode, sr, sl, r_mode, rr, pan, decl_vol; } WAVE_W;
                extern WAVE_W *wave_header;
                extern unsigned long spu_wave_start_ptr;
                extern unsigned char wavs;

                unsigned char *wp = (unsigned char *)data_ptr;
                /* Header: [4] offset into wave_header, [4] size of header data,
                   [8] padding, then header data, then [4] spu_offset, [4] spu_size,
                   [8] padding, then SPU ADPCM data */
                unsigned int hdr_off  = (wp[0]<<24)|(wp[1]<<16)|(wp[2]<<8)|wp[3];
                unsigned int hdr_size = (wp[4]<<24)|(wp[5]<<16)|(wp[6]<<8)|wp[7];
                unsigned char *hdr_data = wp + 16;

                if (hdr_size > 0 && hdr_size < (unsigned)tag->size) {
                    /* Parse PSX-format WAVE_W entries (16 bytes each on PSX)
                       into port WAVE_W (sizeof may differ due to unsigned long) */
                    {
                        int n_entries = hdr_size / 16; /* PSX WAVE_W = 16 bytes */
                        typedef struct { unsigned int addr; char sample_note; char sample_tune;
                            unsigned char a_mode, ar, dr, s_mode, sr, sl, r_mode, rr, pan, decl_vol; } WAVE_W_PSX;
                        WAVE_W_PSX *src = (WAVE_W_PSX *)hdr_data;
                        WAVE_W *dst = (WAVE_W *)((char*)wave_header + hdr_off);
                        for (int wi = 0; wi < n_entries; wi++) {
                            dst[wi].addr = src[wi].addr;
                            dst[wi].sample_note = src[wi].sample_note;
                            dst[wi].sample_tune = src[wi].sample_tune;
                            dst[wi].a_mode = src[wi].a_mode;
                            dst[wi].ar = src[wi].ar;
                            dst[wi].dr = src[wi].dr;
                            dst[wi].s_mode = src[wi].s_mode;
                            dst[wi].sr = src[wi].sr;
                            dst[wi].sl = src[wi].sl;
                            dst[wi].r_mode = src[wi].r_mode;
                            dst[wi].rr = src[wi].rr;
                            dst[wi].pan = src[wi].pan;
                            dst[wi].decl_vol = src[wi].decl_vol;
                        }
                    }
                    wavs = 0x4F;

                    /* SPU data follows after header + padding */
                    unsigned char *spu_hdr = hdr_data + hdr_size;
                    unsigned int spu_off  = (spu_hdr[0]<<24)|(spu_hdr[1]<<16)|(spu_hdr[2]<<8)|spu_hdr[3];
                    unsigned int spu_size = (spu_hdr[4]<<24)|(spu_hdr[5]<<16)|(spu_hdr[6]<<8)|spu_hdr[7];
                    unsigned char *spu_data = spu_hdr + 16;

                    if (spu_size > 0 && spu_off + spu_size <= 512*1024) {
                        extern void SpuSetTransferStartAddr(unsigned long addr);
                        extern unsigned long SpuWrite(unsigned char *addr, unsigned long size);
                        SpuSetTransferStartAddr(spu_wave_start_ptr + spu_off);
                        SpuWrite(spu_data, spu_size);
                        printf("  [fs]   Loaded .wvx: hdr=%u bytes → wave_header+%u, spu=%u bytes → 0x%x\n",
                               hdr_size, hdr_off, spu_size, (unsigned)(spu_wave_start_ptr + spu_off));
                    }
                }
            }
            else if (tag->ext == 'e') {
                /* .e SE header data — stage-specific sound effect definitions.
                   PSX SETBL is 16 bytes (4 chars + 3 x 4-byte pointers).
                   Port SETBL is larger (4 chars + 3 x 8-byte pointers).
                   Parse and convert. */
                extern unsigned char *SD_SeDataLoadInit(unsigned short id);
                unsigned char *buf = SD_SeDataLoadInit(tag->id);
                if (buf) {
                    /* Copy raw data — SePlay accesses se_header as SETBL*
                       but uses (unsigned int)addr as offset. We need to convert
                       the PSX 16-byte entries to port SETBL size. */
                    typedef struct { unsigned char pri, tracks, kind, character;
                        unsigned int addr[3]; } SETBL_PSX; /* 16 bytes */
                    typedef struct { unsigned char pri, tracks, kind, character;
                        unsigned char *addr[3]; } SETBL_PORT;
                    int n_entries = tag->size / 16;
                    if (n_entries > 128) n_entries = 128;
                    SETBL_PSX *src = (SETBL_PSX *)data_ptr;
                    SETBL_PORT *dst = (SETBL_PORT *)buf;
                    for (int ei = 0; ei < n_entries; ei++) {
                        dst[ei].pri = src[ei].pri;
                        dst[ei].tracks = src[ei].tracks;
                        dst[ei].kind = src[ei].kind;
                        dst[ei].character = src[ei].character;
                        /* Store 32-bit offsets as pointer-sized values.
                           SePlay casts these back to (unsigned int) for offset math. */
                        dst[ei].addr[0] = (unsigned char *)(uintptr_t)src[ei].addr[0];
                        dst[ei].addr[1] = (unsigned char *)(uintptr_t)src[ei].addr[1];
                        dst[ei].addr[2] = (unsigned char *)(uintptr_t)src[ei].addr[2];
                    }
                    printf("  [fs]   Loaded .e SE data (%d entries from %d bytes)\n", n_entries, tag->size);
                }
            }
            else if (tag->ext == 'm') {
                /* .mdx song data */
                extern unsigned char *SD_SngDataLoadInit(unsigned short id);
                unsigned char *buf = SD_SngDataLoadInit(tag->id);
                if (buf) {
                    memcpy(buf, data_ptr, tag->size);
                    printf("  [fs]   Loaded .mdx song data (%d bytes)\n", tag->size);
                }
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

/*---------------------------------------------------------------------------*/
/* Stream system — reads VOX.DAT/DEMO.DAT for cutscene timing data          */
/*---------------------------------------------------------------------------*/

static unsigned char *stream_buf = NULL;
static int stream_buf_len = 0;       /* bytes loaded */
static int stream_buf_cap = 0;       /* allocated capacity */
static int stream_read_pos = 0;      /* current scan position */
static int stream_active = 0;        /* 1 = stream loaded and active */
static int stream_ended = 0;
static int port_stream_tick = 0;

#define VOX_SECTOR_BASE  0x40000000  /* marker: sector is relative to VOX.DAT */
#define DEMO_SECTOR_BASE 0x20000000  /* marker: sector is relative to DEMO.DAT */

void FS_StreamTaskStart(int sector)
{
    /* Determine which file to read from based on sector marker */
    FILE *f;
    if (sector & VOX_SECTOR_BASE) {
        sector &= ~VOX_SECTOR_BASE;
        f = dat_files[4]; /* VOX.DAT */
    } else if (sector & DEMO_SECTOR_BASE) {
        sector &= ~DEMO_SECTOR_BASE;
        f = dat_files[5]; /* DEMO.DAT */
    } else {
        f = dat_files[5]; /* default: DEMO.DAT */
        if (!f) f = dat_files[4];
    }
    if (!f) {
        printf("[stream] no stream file available\n");
        stream_active = 0;
        return;
    }

    /* On PSX, data streams continuously from CD into a 96KB circular buffer.
       For the port, load the entire stream section from sector to end of file. */
    long byte_offset = (long)sector * 2048;
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    long stream_len = file_len - byte_offset;
    if (stream_len <= 0) {
        printf("[stream] no data at sector %d\n", sector);
        stream_active = 0;
        return;
    }

    if (stream_len > stream_buf_cap) {
        free(stream_buf);
        stream_buf = (unsigned char *)malloc(stream_len);
        stream_buf_cap = (int)stream_len;
    }

    fseek(f, byte_offset, SEEK_SET);
    stream_buf_len = (int)fread(stream_buf, 1, stream_len, f);
    stream_read_pos = 0;
    stream_active = 1;
    stream_ended = 0;
    port_stream_tick = 0;

    printf("[stream] loaded %d bytes from sector %d (offset 0x%lX)\n",
           stream_buf_len, sector, byte_offset);
}

int FS_StreamTaskState(void)
{
    /* 0 = ready/done, -1 = waiting, 1 = loading */
    return stream_active ? 0 : 0;
}

void FS_StreamTaskInit(void) {}

int FS_StreamSync(void)
{
    /* Process stream buffer — scan for end markers */
    if (!stream_active || !stream_buf) return 0;

    /* Check for 0xF0 end marker in the data we've scanned */
    return 0;
}

void FS_StreamCD(void) {}

int FS_StreamGetTop(int is_demo)
{
    /* Return a marker so FS_StreamTaskStart knows which file to read.
       The caller adds this to the stream offset: sector = offset + marker. */
    return is_demo ? DEMO_SECTOR_BASE : VOX_SECTOR_BASE;
}

int FS_StreamInit(void *pHeap, int heapSize) { (void)pHeap; (void)heapSize; return 0; }

void FS_StreamStop(void)
{
    stream_active = 0;
    stream_ended = 1;
}

void FS_StreamOpen(void) {}
void FS_StreamClose(void) {}

int FS_StreamIsEnd(void)
{
    /* In timer mode (no stream data), end after the tick counter
       reaches a high value. The cutscene GCL proc callback handles
       the actual stage transition. FS_StreamStop() can end it early
       (e.g., when user presses skip or the pad_demo finishes). */
    if (!stream_active) return 1;
    if (stream_ended) return 1;
    return 0;
}

void *FS_StreamGetData(int target_type)
{
    /* Scan stream buffer for next entry matching target_type.
       Entry format: {size:24, type:8} where size = total entry bytes.
       Returns pointer to data (after 4-byte header), matching PSX FS_StreamGetData. */
    if (!stream_active || !stream_buf || stream_buf_len == 0) return NULL;

    int pos = 0;
    int entries_scanned = 0;
    while (pos + 4 <= stream_buf_len)
    {
        int tag;
        memcpy(&tag, stream_buf + pos, 4);
        int type = tag & 0xFF;
        int size = (tag >> 8) & 0xFFFFFF;

        if (size <= 0 || pos + size > stream_buf_len) {
            static int fail_dbg = 0;
            if (fail_dbg++ < 3)
                printf("[GetData(%d)] FAIL at pos=%d: type=0x%02X size=%d buflen=%d scanned=%d\n",
                       target_type, pos, type, size, stream_buf_len, entries_scanned);
            return NULL;
        }

        if (type == target_type) {
            /* Mark as returned (bit 7) so next scan skips this entry.
               FS_StreamClear sets type to 0 for full consumption. */
            stream_buf[pos] = type | 0x80;
            return (void *)(stream_buf + pos + 4);
        }

        pos += size;
        entries_scanned++;
    }

    return NULL;
}

int FS_StreamGetSize(void *stream)
{
    if (!stream) return 0;
    int entry = *(int *)stream;
    return (entry >> 8) & 0xFFFF;
}

void FS_StreamUngetData(void *stream) { (void)stream; }

void FS_StreamClear(void *stream)
{
    /* Mark entry as consumed by clearing the header's type byte.
       stream points to data (header is at stream - 4). */
    if (!stream) return;
    unsigned char *hdr = (unsigned char *)stream - 4;
    *hdr = 0; /* clear type to 0 (consumed) */
}

void FS_StreamClearType(void *stream, int target_type)
{
    /* Clear type byte of the entry header (at stream - 4) if it matches */
    if (!stream) return;
    unsigned char *hdr = (unsigned char *)stream - 4;
    int type = *hdr & 0x7F; /* strip read flag */
    if (type == target_type) {
        *hdr = 0;
    }
}

int  FS_StreamGetEndFlag(void) { return stream_ended; }
int  FS_StreamIsForceStop(void) { return 0; }
void FS_StreamTickStart(void) { port_stream_tick = 0; }
void FS_StreamSoundMode(void) {}
int  FS_StreamGetTick(void) { return port_stream_tick++; }

/*---------------------------------------------------------------------------*/
/* PcmOpen/PcmRead/PcmClose — load sound files from STAGE.DIR               */
/*---------------------------------------------------------------------------*/

#define PCM_MAX_HANDLES 4
typedef struct {
    int    active;
    char  *data;     /* malloc'd file data */
    int    size;     /* total file size */
    int    pos;      /* read position */
} PcmHandle;
static PcmHandle pcm_handles[PCM_MAX_HANDLES];

/* Find a sound file ('s' tag with matching ext and id) in the current stage
   and return its data. On PSX, path_idx selects: 2=wave, 4=SE, etc.
   ext is 'w' for wave, 'm' for song, 'e' for SE. */
static char pcm_ext_for_path[] = { 0, 0, 'w', 0, 'e', 'm' };

int port_PcmOpen(int code, int path_idx)
{
    if (!port_stage_info.loaded || !port_stage_info.datacnf)
        return -1;

    /* Find free handle */
    int h = -1;
    for (int i = 0; i < PCM_MAX_HANDLES; i++) {
        if (!pcm_handles[i].active) { h = i; break; }
    }
    if (h < 0) return -1;

    /* Determine which ext to search for */
    char ext = 0;
    if (path_idx >= 0 && path_idx < (int)sizeof(pcm_ext_for_path))
        ext = pcm_ext_for_path[path_idx];
    if (!ext) ext = 'w'; /* default to wave */

    /* Scan DATACNF tags for matching 's' tag */
    DATACNF_TAG *tag = port_stage_info.datacnf->tags;
    char *data_ptr = (char *)port_stage_info.buffer + FS_SECTOR_SIZE;
    int file_id = code & 0xFFFF;

    while (tag->mode != 0) {
        if (tag->mode == 's' && tag->ext == ext && tag->id == file_id) {
            /* Found it — data_ptr points to this tag's data in the stage blob */
            pcm_handles[h].data = data_ptr;
            pcm_handles[h].size = tag->size;
            pcm_handles[h].pos = 0;
            pcm_handles[h].active = 1;
            return h;
        }
        /* Advance data pointer based on tag mode */
        if (tag->mode == 'c') {
            /* Cache tags: size is total size of the cache section */
            DATACNF_TAG *t = tag;
            while (t->mode == 'c') t++;
            /* The terminator tag has the total size */
            data_ptr += (t->size + (FS_SECTOR_SIZE - 1)) & ~(FS_SECTOR_SIZE - 1);
            tag = t + 1;
            continue;
        }
        data_ptr += (tag->size + (FS_SECTOR_SIZE - 1)) & ~(FS_SECTOR_SIZE - 1);
        tag++;
    }

    printf("[pcm] File not found: code=0x%x path=%d ext='%c'\n", code, path_idx, ext);
    return -1;
}

int port_PcmRead(int fd, unsigned char *buf, int size)
{
    if (fd < 0 || fd >= PCM_MAX_HANDLES || !pcm_handles[fd].active)
        return -1;
    PcmHandle *h = &pcm_handles[fd];
    int avail = h->size - h->pos;
    if (size > avail) size = avail;
    if (size <= 0) return 0;
    memcpy(buf, h->data + h->pos, size);
    h->pos += size;
    return size;
}

int port_PcmClose(int fd, int path_idx)
{
    (void)path_idx;
    if (fd < 0 || fd >= PCM_MAX_HANDLES) return -1;
    pcm_handles[fd].active = 0;
    return 0;
}
