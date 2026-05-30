/**
 * Port filesystem — stdio replacement for PSX CD-ROM.
 * Can read game data from either:
 *   - an extracted disc directory (PORT_DATA_PATH / PORT_DATA_DIR env)
 *   - a disc image (.iso / .bin / .cue) passed via CLI or PORT_ISO env
 *
 * All internal access goes through the PortFile abstraction (see below).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "libgte.h"
#include "libgpu.h"
#include "libfs/libfs.h"
#include "libgv/libgv.h"
#include "iso_reader.h"

/*---------------------------------------------------------------------------*/
/* Configuration                                                             */
/*---------------------------------------------------------------------------*/

#ifndef PORT_DATA_PATH
#define PORT_DATA_PATH "data/disc1/MGS/"
#endif

/* Set by main.c before FS_StartDaemon runs if the user passed an image. */
const char *port_iso_override = NULL;

/*---------------------------------------------------------------------------*/
/* PortFile — uniform view over stdio FILE* or an ISO-embedded file          */
/*---------------------------------------------------------------------------*/

typedef struct PortFile {
    FILE    *fp;       /* directory mode: real file handle */
    IsoFile  iso_ent;  /* iso mode: (lba, size) inside image */
    long     size;     /* cached size in bytes (both modes) */
    int      from_iso; /* 0 = fp, 1 = iso_ent */
    int      valid;    /* 1 if opened successfully */
} PortFile;

static IsoImage port_iso = {0};
static int      port_iso_active = 0;

static int pf_valid(const PortFile *pf) { return pf && pf->valid; }

/* Read `size` bytes from the given byte offset into `buf`. Returns bytes
 * read (0 on EOF, -1 on error or unopened). */
static int pf_read_at(PortFile *pf, long byte_offset, int size, void *buf)
{
    if (!pf_valid(pf)) return -1;
    if (pf->from_iso) {
        return iso_read_file(&port_iso, &pf->iso_ent, byte_offset, size, buf);
    }
    if (fseek(pf->fp, byte_offset, SEEK_SET) != 0) return -1;
    return (int)fread(buf, 1, size, pf->fp);
}

static long pf_size(const PortFile *pf) { return pf_valid(pf) ? pf->size : 0; }

static PortFile stage_dir_pf;
static PortFile dat_pf[7];

/* Stage directory table */
#define FS_DIRNAME_MAX  8
#define MAX_STAGES      170

typedef struct {
    char name[FS_DIRNAME_MAX];
    int  offset;  /* in sectors */
} DirEntry;

static DirEntry stage_table[MAX_STAGES];
/* Parallel array: when non-NULL the entry came from extra_stages/ (a
   loose datacnf.bin on disk) and stage_table[i].offset is the sentinel
   EXTRA_STAGE_SENTINEL. Synthesised at FS_StartDaemon time. */
#define EXTRA_STAGE_SENTINEL 0x7FFFFFFF
static char *extra_paths[MAX_STAGES];
static long  extra_sizes[MAX_STAGES];
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
    long byte_offset = (long)sector_offset * FS_SECTOR_SIZE;
    return pf_read_at(&stage_dir_pf, byte_offset, size, buf);
}

/*---------------------------------------------------------------------------*/
/* FS_StartDaemon — open files and parse STAGE.DIR directory                 */
/*---------------------------------------------------------------------------*/

static const char *dat_names[] = {
    "STAGE.DIR", "RADIO.DAT", "FACE.DAT", "ZMOVIE.STR",
    "VOX.DAT", "DEMO.DAT", "BRF.DAT"
};

/* Try to open all 7 DAT files from a plain directory. */
static int port_fs_open_dir(const char *data_path)
{
    int any_opened = 0;
    for (int i = 0; i < 7; i++)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", data_path, dat_names[i]);
        FILE *fp = fopen(path, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            dat_pf[i].fp = fp;
            dat_pf[i].size = sz;
            dat_pf[i].from_iso = 0;
            dat_pf[i].valid = 1;
            printf(" %s", dat_names[i]);
            any_opened = 1;
        } else {
            printf(" [%s:MISSING]", dat_names[i]);
        }
    }
    stage_dir_pf = dat_pf[0];
    return any_opened;
}

/* Open a disc image (.iso/.bin/.cue) and locate /MGS/<dat> for each. */
static int port_fs_open_iso(const char *image_path)
{
    if (iso_open(&port_iso, image_path) != 0) return 0;
    port_iso_active = 1;

    int any_found = 0;
    for (int i = 0; i < 7; i++) {
        char path[64];
        snprintf(path, sizeof(path), "MGS/%s", dat_names[i]);
        if (iso_find_file(&port_iso, path, &dat_pf[i].iso_ent) == 0) {
            dat_pf[i].size = dat_pf[i].iso_ent.size;
            dat_pf[i].from_iso = 1;
            dat_pf[i].valid = 1;
            printf(" %s(lba=%d size=%ld)", dat_names[i],
                   dat_pf[i].iso_ent.lba, dat_pf[i].iso_ent.size);
            any_found = 1;
        } else {
            printf(" [%s:MISSING]", dat_names[i]);
        }
    }
    stage_dir_pf = dat_pf[0];
    return any_found;
}

void FS_StartDaemon(void)
{
    printf("fs:");

    /* Selection order: (1) port_iso_override from CLI / main.c,
       (2) PORT_ISO env var, (3) PORT_DATA_DIR env, (4) default dir. */
    const char *iso_path = port_iso_override;
    if (!iso_path || !*iso_path) iso_path = getenv("PORT_ISO");

    int opened = 0;
    if (iso_path && *iso_path) {
        opened = port_fs_open_iso(iso_path);
        if (!opened) printf(" (iso '%s' unusable, falling back to dir)", iso_path);
    }
    if (!opened) {
        const char *dir = getenv("PORT_DATA_DIR");
        if (!dir || !*dir) dir = PORT_DATA_PATH;
        opened = port_fs_open_dir(dir);
    }
    printf("\n");

    /* Parse STAGE.DIR directory table */
    if (pf_valid(&stage_dir_pf))
    {
        unsigned char header[2048];
        pf_read_at(&stage_dir_pf, 0, 2048, header);

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

    /* Auto-discover loose datacnf.bin under extra_stages/ and append
       synthetic stage entries. The editor's stage picker iterates the
       same table so they appear in the dropdown. */
    {
        /* Search relative paths so both ./mgs (CWD=port) and ./editor/editor
           (CWD=port/editor) find the same extra_stages directory. */
        const char *roots[] = { "extra_stages", "../extra_stages",
                                "../../extra_stages",
                                "editor/extra_stages",
                                "../editor/extra_stages", NULL };
        for (const char **rp = roots; *rp; rp++) {
            DIR *d = opendir(*rp);
            if (!d) continue;
            struct dirent *de;
            while ((de = readdir(d)) && stage_count < MAX_STAGES) {
                if (de->d_name[0] == '.') continue;
                char path[512];
                snprintf(path, sizeof(path), "%s/%s/datacnf.bin",
                         *rp, de->d_name);
                FILE *fp = fopen(path, "rb");
                if (!fp) continue;
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fclose(fp);
                if (sz <= 0) continue;
                int idx = stage_count++;
                memset(stage_table[idx].name, 0, FS_DIRNAME_MAX);
                int n = (int)strlen(de->d_name);
                if (n > FS_DIRNAME_MAX) n = FS_DIRNAME_MAX;
                memcpy(stage_table[idx].name, de->d_name, n);
                stage_table[idx].offset = EXTRA_STAGE_SENTINEL;
                extra_paths[idx] = strdup(path);
                extra_sizes[idx] = sz;
                printf("  [fs] +extra stage '%s' (%ld bytes)\n",
                       de->d_name, sz);
            }
            closedir(d);
            break;   /* first found root wins */
        }
    }

    FS_DiskNum = 0;
    printf("  [fs] FS_StartDaemon complete\n");
    fflush(stdout);
}

/* Read raw bytes from a stage source. For built-in stages the byte
   offset is `sector * FS_SECTOR_SIZE` inside STAGE.DIR; for extra
   stages we use the parallel `extra_paths[]` and read the loose file
   directly. `idx` is the index into stage_table; `byte_offset` is the
   read position within that stage's blob. */
static int extra_stage_read(int idx, long byte_offset, int size, void *buf)
{
    if (idx < 0 || idx >= stage_count || !extra_paths[idx]) return -1;
    FILE *fp = fopen(extra_paths[idx], "rb");
    if (!fp) return -1;
    if (fseek(fp, byte_offset, SEEK_SET) != 0) { fclose(fp); return -1; }
    size_t got = fread(buf, 1, size, fp);
    fclose(fp);
    return (int)got;
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

/* Editor accessors — expose the parsed STAGE.DIR table without making the
   internal struct public. Names are 8 chars, NUL-padded; the buffer returned
   here points into stage_table and is valid for the program's lifetime. */
int port_fs_stage_count(void) { return stage_count; }

const char *port_fs_stage_name(int idx)
{
    if (idx < 0 || idx >= stage_count) return NULL;
    return stage_table[idx].name;   /* not NUL-terminated past 8 chars */
}

/* True iff the stage at `idx` came from extra_stages/<name>/datacnf.bin
   rather than the disc's STAGE.DIR. Used by the imgui debug menu so the
   user can launch a custom stage with one click. */
int port_fs_stage_is_extra(int idx)
{
    if (idx < 0 || idx >= stage_count) return 0;
    return stage_table[idx].offset == EXTRA_STAGE_SENTINEL;
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

/* Last stage name requested via FS_LoadStageRequest. Used by the imgui debug
   overlay (Other tab) to show the current stage without hooking the overlay
   loader. Not cleared on unload -- it keeps showing the most recent name. */
char port_current_stage[16] = {0};

/* Editor stage-switch helper. Frees the GV_NORMAL_MEMORY allocation made by
   FS_LoadStageRequest (~1 MiB per stage, would otherwise exhaust the 2 MiB
   pool after two loads) and clears the cached info so a follow-up
   FS_LoadStageRequest starts fresh. Cache entries that pointed into the
   freed buffer must be zeroed by the caller (see ed_loader). */
void port_fs_unload_stage(void)
{
    if (port_stage_info.loaded && port_stage_info.buffer) {
        GV_FreeMemory(GV_NORMAL_MEMORY, port_stage_info.buffer);
    }
    port_stage_info.loaded = 0;
    port_stage_info.buffer = NULL;
    port_stage_info.datacnf = NULL;
    port_stage_info.size = 0;
    port_current_stage[0] = 0;
}

void *FS_LoadStageRequest(const char *dirname)
{
    if (dirname) {
        strncpy(port_current_stage, dirname, sizeof(port_current_stage) - 1);
        port_current_stage[sizeof(port_current_stage) - 1] = 0;
    }
    int sector = FS_CdGetStageFileTop((char *)dirname);
    if (sector < 0)
    {
        port_stage_info.loaded = 0;
        return &port_stage_info;
    }

    /* Resolve which transport to read from. Built-in stages live inside
       STAGE.DIR at `sector * 2048`; extra stages live in a loose
       `extra_stages/<name>/datacnf.bin` and use the sentinel offset. */
    int extra_idx = -1;
    if (sector == EXTRA_STAGE_SENTINEL) {
        for (int i = 0; i < stage_count; i++) {
            if (stage_table[i].offset == EXTRA_STAGE_SENTINEL &&
                strncmp(stage_table[i].name, dirname, FS_DIRNAME_MAX) == 0) {
                extra_idx = i; break;
            }
        }
        if (extra_idx < 0) {
            port_stage_info.loaded = 0;
            return &port_stage_info;
        }
        printf("  [fs] Loading extra stage '%s' from %s...\n",
               dirname, extra_paths[extra_idx]);
    } else {
        printf("  [fs] Loading stage '%s' at sector %d...\n", dirname, sector);
    }

    /* Read the DATACNF header (first sector) */
    unsigned char header[FS_SECTOR_SIZE];
    if (extra_idx >= 0)
        extra_stage_read(extra_idx, 0, FS_SECTOR_SIZE, header);
    else
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

    if (extra_idx >= 0)
        extra_stage_read(extra_idx, 0, total_size, buffer);
    else
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
            int region = (tag->mode == 'r') ? GV_INIT_RESIDENT : GV_INIT_NOCACHE;
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

                if (region == GV_INIT_RESIDENT)
                {
                    /* Allocate from port_malloc (in the mmap'd pool) so pointers
                       survive GCL int truncation. Never freed — lifetime = session. */
                    extern void *port_malloc(size_t size);
                    void *perm = port_malloc(dar->size);
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

                    GV_LoadInit(entry_data, cache_id, GV_INIT_CACHE);
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

                    GV_LoadInit(file_data, cache_id, GV_INIT_NOCACHE);
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
                GV_LoadInit(data_ptr, cache_id, GV_INIT_NOCACHE);
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
                extern volatile int sng_status;
                unsigned char *buf = SD_SngDataLoadInit(tag->id);
                if (buf) {
                    memcpy(buf, data_ptr, tag->size);
                    /* Mark song data as ready so IntSdMain can activate it.
                       On PSX, SdMain task sets sng_status=2 after LoadSngData.
                       The port loads .mdx directly during stage loading. */
                    sng_status = 2;
                    printf("  [fs]   Loaded .mdx song data (%d bytes) → sng_status=2\n", tag->size);
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
    if (fileno < 0 || fileno >= 7 || !pf_valid(&dat_pf[fileno]))
        return;
    long byte_offset = (long)offset * FS_SECTOR_SIZE;
    int r = pf_read_at(&dat_pf[fileno], byte_offset, size, buffer);
    if (r < 0) r = 0;
    if (r < size)
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
/* Stream system — reads VOX.DAT/DEMO.DAT for codec voice + cutscene timing
 *
 * PSX layout: continuously-refilled 96 KB circular heap; CD reads sector at
 * a time into the write cursor; game thread parses {type:8, size:24} blocks
 * starting at the top cursor, marking consumed entries with type=0 so the
 * top cursor advances. Special block types: 0xFF=wrap-to-heap-start,
 * 0xF0=end-of-bank. Reference: source/libfs/stream.c.
 *
 * Port: same on-disk format but reads synchronously from the open VOX/DEMO
 * file. FS_StreamSync() is called every frame from strctrl.c:Act() — that's
 * where we pump sectors as the heap drains below 1/3 free.
 */

#define VOX_SECTOR_BASE  0x40000000  /* marker: sector is relative to VOX.DAT */
#define DEMO_SECTOR_BASE 0x20000000  /* marker: sector is relative to DEMO.DAT */

#define STREAM_HEAP_SIZE (96 * 1024)
#define STREAM_SECTOR    2048

static unsigned char stream_heap[STREAM_HEAP_SIZE];
static int   stream_top         = 0;   /* read cursor: oldest unconsumed entry */
static int   stream_write_ptr   = 0;   /* write cursor: where next sector lands */
static int   stream_sector      = 0;   /* next sector to fetch */
static int   stream_active      = 0;
static int   stream_end         = 0;   /* hit 0xF0 / EOF */
static int   stream_stop        = 0;   /* user FS_StreamStop */
static int   stream_task_state  = 0;   /* -1=priming, 0=ready, matches PSX */
static PortFile *stream_pf      = NULL;
static int   stream_ended       = 0;   /* derived end-of-playback (see FS_StreamIsEnd) */
static int   port_stream_tick   = 0;   /* monotonic counter for FS_StreamGetTick (port-only stand-in for PSX VSync timer) */

/* (mod heap size) bytes between top and write_ptr — i.e., parseable data. */
static int stream_remaining(void)
{
    int r = stream_write_ptr - stream_top;
    if (r < 0) r += STREAM_HEAP_SIZE;
    return r;
}

/* Advance top past consumed (type=0) and wrap (0xFF) entries so GetData
 * starts scanning at the next live block. */
static void stream_advance_top(void)
{
    while (stream_top != stream_write_ptr) {
        unsigned int tag;
        memcpy(&tag, &stream_heap[stream_top], 4);
        int type = tag & 0xFF;
        int size = (tag >> 8) & 0xFFFFFF;
        if (type == 0xFF) {
            stream_top = 0;
            continue;
        }
        if (type == 0 && size > 0) {
            stream_top += size;
            if (stream_top >= STREAM_HEAP_SIZE) stream_top -= STREAM_HEAP_SIZE;
            continue;
        }
        break;
    }
}

/* Read one sector into the heap at write_ptr. Returns 1 if a sector was
 * consumed, 0 if we stalled (heap full, EOF, or end-of-bank already seen). */
static int stream_pump_one(void)
{
    if (stream_end || stream_stop || !stream_pf) return 0;

    /* Need 2048 contiguous bytes from write_ptr. If we'd cross the end of
     * the heap, drop a 0xFF wrap marker and reset write_ptr to 0. */
    if (stream_write_ptr + STREAM_SECTOR > STREAM_HEAP_SIZE) {
        /* Only safe to wrap if top is not in the next region we'd overwrite. */
        if (stream_top > stream_write_ptr) {
            /* unread data sits between us and the heap end — no room. */
            return 0;
        }
        unsigned int wrap = 0xFFu;
        memcpy(&stream_heap[stream_write_ptr], &wrap, 4);
        stream_write_ptr = 0;
    }

    /* Collision check: would the new sector overrun unread data at `top`? */
    if (stream_top > stream_write_ptr &&
        stream_top - stream_write_ptr < STREAM_SECTOR) {
        return 0;
    }

    long byte_off = (long)stream_sector * STREAM_SECTOR;
    long file_len = pf_size(stream_pf);
    if (byte_off >= file_len) {
        stream_end = 1;
        return 0;
    }
    int want = STREAM_SECTOR;
    if (byte_off + want > file_len) want = (int)(file_len - byte_off);
    int n = pf_read_at(stream_pf, byte_off, want, &stream_heap[stream_write_ptr]);
    if (n <= 0) {
        stream_end = 1;
        return 0;
    }

    /* Scan the newly-read sector for the 0xF0 end-of-bank marker so we
     * can stop reading without overrunning into the next cutscene. */
    {
        int p = stream_write_ptr;
        int sector_end = stream_write_ptr + n;
        while (p + 4 <= sector_end) {
            unsigned int tag;
            memcpy(&tag, &stream_heap[p], 4);
            int type = tag & 0xFF;
            int size = (tag >> 8) & 0xFFFFFF;
            if (type == 0xF0) { stream_end = 1; break; }
            if (size <= 0 || size > 0x100000) break;
            p += size;
            if (p > sector_end) break;
        }
    }

    stream_write_ptr += n;
    if (stream_write_ptr == STREAM_HEAP_SIZE) stream_write_ptr = 0;
    stream_sector++;
    stream_task_state = 0;
    return 1;
}

void FS_StreamTaskStart(int sector)
{
    /* Determine which file based on sector marker */
    PortFile *pf;
    if (sector & VOX_SECTOR_BASE) {
        sector &= ~VOX_SECTOR_BASE;
        pf = &dat_pf[4]; /* VOX.DAT */
    } else if (sector & DEMO_SECTOR_BASE) {
        sector &= ~DEMO_SECTOR_BASE;
        pf = &dat_pf[5]; /* DEMO.DAT */
    } else {
        pf = &dat_pf[5];
        if (!pf_valid(pf)) pf = &dat_pf[4];
    }
    if (!pf_valid(pf)) {
        printf("[stream] no stream file available\n");
        stream_active = 0;
        stream_task_state = 0;
        return;
    }

    stream_pf = pf;
    stream_sector = sector;
    stream_top = 0;
    stream_write_ptr = 0;
    stream_end = 0;
    stream_stop = 0;
    stream_ended = 0;
    stream_active = 1;
    stream_task_state = -1;

    /* Prime the buffer so StartStream() sees the header block immediately. */
    int primed = 0;
    while (primed < 4 && stream_pump_one()) primed++;
    stream_task_state = (primed > 0) ? 0 : -1;
    printf("[stream] start sector=%d primed=%d sectors\n", sector, primed);
}

int FS_StreamTaskState(void)
{
    /* -1=waiting for first sector, 0=ready/idle, matches source/libfs/stream.c */
    if (!stream_active) return 0;
    return stream_task_state;
}

void FS_StreamTaskInit(void) {}

int FS_StreamSync(void)
{
    if (!stream_active || stream_stop) {
        stream_task_state = 0;
        return 0;
    }
    if (stream_end) {
        /* No more data to fetch; let consumer drain the heap. */
        stream_advance_top();
        return (stream_top != stream_write_ptr) ? 1 : 0;
    }

    stream_advance_top();

    /* Refill if more than 1/3 of the heap is empty (PSX threshold). Cap
     * sectors-per-Sync so we don't stall the frame on a slow disk. */
    if (stream_remaining() < (STREAM_HEAP_SIZE * 2) / 3) {
        for (int i = 0; i < 8; i++) {
            if (!stream_pump_one()) break;
        }
    }
    return 1;
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
    stream_stop = 1;
    stream_end = 1;
    stream_ended = 1;
    stream_active = 0;
    stream_task_state = 0;
}

void FS_StreamOpen(void) {}
void FS_StreamClose(void) {}

/* Editor-only accessor: read raw bytes from one of the opened DAT files
 * (file_id matches the FS_FILEID_* enum). Used by the DMO inspector to
 * walk DEMO.DAT block streams without going through the FS streamer
 * abstraction. Returns bytes read, 0 on EOF, -1 on error / closed file.
 *
 * Not meant for game code — the streamer is the right path for live
 * cutscene playback. This is a back door for offline tooling that wants
 * the *file* shape rather than the streamed-block shape. */
int port_fs_read_dat(int file_id, long byte_off, int len, void *buf)
{
    if (file_id < 0 || file_id >= 7) return -1;
    return pf_read_at(&dat_pf[file_id], byte_off, len, buf);
}

int FS_StreamIsEnd(void)
{
    /* End conditions: explicit stop, EOF/0xF0 with heap drained, or audio
       playback caught up to the producer (codec line finished). The cutscene
       GCL proc callback handles the actual stage transition.
       FS_StreamStop() can end it early (skip / pad_demo finish). */
    if (!stream_active) return 1;
    if (stream_ended) return 1;
    if (stream_end && stream_top == stream_write_ptr) {
        stream_ended = 1;
        return 1;
    }

    /* Natural-end detection: the codec script does
         while (GM_StreamStatus() != -1) mts_wait_vbl(2);
       after starting a VOX line and expects this to terminate when the
       audio finishes playing. On PSX the SPU signals end-of-stream via
       hardware; here we have to derive it from the SPU emulator's read
       cursor catching up to the game's write cursor.

       A stream is considered finished when:
         (1) the SPU stream voice was keyed on at least once (wr_r > 0),
         (2) the audio thread's read cursor has reached or passed both
             the L and R write cursors (no more PCM left), AND
         (3) we observe this condition stably across at least 2 polls
             (~33 ms between codec wait_vbl(2) cycles) — guards against
             an off-by-one where the game just hasn't produced the next
             chunk yet but is about to.

       Without this the codec call hangs forever in stages whose Mei
       Ling voice line isn't long enough for the user to press CROSS
       during the brief codec_state==5 window. */
    {
        extern int port_spu_stream_info(int *pcm_rd, int *pcm_wr_r, int *pcm_wr_l,
                                        int *active, unsigned long *base_r,
                                        unsigned long *base_l);
        static int prev_seen_done = 0;
        int rd = 0, wr_r = 0, wr_l = 0, sa = 0;
        port_spu_stream_info(&rd, &wr_r, &wr_l, &sa, NULL, NULL);
        if (wr_r > 0 && rd >= wr_r && rd >= wr_l) {
            if (prev_seen_done) {
                stream_ended = 1;
                prev_seen_done = 0;
                /* Also force the sd_main str_status state machine back to
                   idle. On PSX it transitions 5→6→7→0 when sd_str.c
                   exhausts FS_StreamGetData; in the port it sometimes
                   gets stuck at 5/6 because the order of mts task wakes
                   diverges. If we leave it stuck, sd_str_play() returns
                   true forever (status > 4) and the next stream playback
                   attempt floods "Double Pcm" instead of keying on. */
                {
                    extern unsigned int str_status;
                    str_status = 0;
                }
                return 1;
            }
            prev_seen_done = 1;
        } else {
            prev_seen_done = 0;
        }
    }
    return 0;
}

void *FS_StreamGetData(int target_type)
{
    /* Walk the ring buffer from `top` toward `write_ptr` for next live block
     * of target_type. Mark match with bit 0x80 so PSX semantics are preserved
     * (FS_StreamUngetData strips the bit; FS_StreamClear writes 0). */
    if (!stream_active || stream_stop) return NULL;

    int ptr = stream_top;
    while (ptr != stream_write_ptr) {
        if (ptr + 4 > STREAM_HEAP_SIZE) {
            /* Heap-end-straddling tag would be malformed — skip to start. */
            ptr = 0;
            continue;
        }
        unsigned int tag;
        memcpy(&tag, &stream_heap[ptr], 4);
        int type = tag & 0xFF;
        int size = (tag >> 8) & 0xFFFFFF;

        if (type == 0xFF) {
            ptr = 0;
            continue;
        }
        if (type == 0xF0) {
            return NULL;
        }
        if (type == target_type) {
            stream_heap[ptr] = (unsigned char)(type | 0x80);
            return &stream_heap[ptr + 4];
        }
        if (size <= 0 || size > 0x100000) return NULL;
        ptr += size;
        if (ptr >= STREAM_HEAP_SIZE) ptr -= STREAM_HEAP_SIZE;
    }
    return NULL;
}

int FS_StreamGetSize(void *stream)
{
    if (!stream) return 0;
    /* stream points to payload; header tag is the 4 bytes before. */
    int tag;
    memcpy(&tag, (unsigned char *)stream - 4, 4);
    return (tag >> 8) & 0xFFFFFF;
}

void FS_StreamUngetData(void *stream)
{
    /* Reverse FS_StreamGetData's 0x80 mark so the entry is rediscoverable. */
    if (!stream) return;
    unsigned char *hdr = (unsigned char *)stream - 4;
    *hdr &= ~0x80u;
}

void FS_StreamClear(void *stream)
{
    /* Mark entry consumed (type byte = 0). Top cursor advances next Sync. */
    if (!stream) return;
    unsigned char *hdr = (unsigned char *)stream - 4;
    *hdr = 0;
    stream_advance_top();
}

void FS_StreamClearType(void *stream, int target_type)
{
    /* Walk top→stream-end-of-target, clear any block of target_type. PSX
     * uses this to drop the type-1 ADPCM entry after sd_str_play consumed it. */
    if (!stream || stream_stop) return;
    unsigned char *end = (unsigned char *)stream - 4;
    int ptr = stream_top;
    while (ptr != stream_write_ptr && &stream_heap[ptr] != end) {
        if (ptr + 4 > STREAM_HEAP_SIZE) { ptr = 0; continue; }
        unsigned int tag;
        memcpy(&tag, &stream_heap[ptr], 4);
        int type = tag & 0xFF;
        int size = (tag >> 8) & 0xFFFFFF;
        if (type == 0xFF) { ptr = 0; continue; }
        if ((type & 0x7F) == target_type) {
            stream_heap[ptr] = 0;
        }
        if (size <= 0 || size > 0x100000) break;
        ptr += size;
        if (ptr >= STREAM_HEAP_SIZE) ptr -= STREAM_HEAP_SIZE;
    }
}

int  FS_StreamGetEndFlag(void) { return stream_end; }
int  FS_StreamIsForceStop(void) { return 0; }
void FS_StreamTickStart(void) {
    port_stream_tick = 0;
    /* Sync str_tick_count so jimctrl (subtitle actor) doesn't return early.
       On PSX, str_tick_count is set by SPU IRQ which runs fast. On the port,
       it stays -1 until StrSpuTransWithNoLoop reaches state 4 (~12 frames).
       Setting it to 0 here lets jimctrl process subtitles immediately. */
    extern int str_tick_count;
    str_tick_count = 0;
}
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
/* path_idx: 2=wave('w'), 3=song('m'), 4=SE('e'), 5=song('m') */
static char pcm_ext_for_path[] = { 0, 0, 'w', 'm', 'e', 'm' };

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
