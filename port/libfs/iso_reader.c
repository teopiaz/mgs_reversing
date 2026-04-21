#include "iso_reader.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ISO_SECTOR_USER_SIZE 2048

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int iso_path_looks_like_image(const char *path)
{
    if (!path) return 0;
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    return ieq(dot, ".iso") || ieq(dot, ".bin") ||
           ieq(dot, ".cue") || ieq(dot, ".img");
}

/* Read one sector's 2048 bytes of user data from the image. */
static int iso_read_sector(IsoImage *img, int lba, uint8_t *out2048)
{
    if (!img || !img->fp) return -1;
    long off = (long)lba * img->sector_size + img->data_offset;
    if (fseek(img->fp, off, SEEK_SET) != 0) return -1;
    size_t n = fread(out2048, 1, ISO_SECTOR_USER_SIZE, img->fp);
    return (n == ISO_SECTOR_USER_SIZE) ? 0 : -1;
}

static int iso_detect_format(IsoImage *img)
{
    uint8_t buf[ISO_SECTOR_USER_SIZE];

    /* Mode 2 Form 1: user data at +24 */
    img->sector_size = 2352; img->data_offset = 24;
    if (iso_read_sector(img, 16, buf) == 0 && memcmp(buf + 1, "CD001", 5) == 0) return 0;

    /* Mode 1 raw: user data at +16 */
    img->data_offset = 16;
    if (iso_read_sector(img, 16, buf) == 0 && memcmp(buf + 1, "CD001", 5) == 0) return 0;

    /* Plain ISO: 2048-byte sectors */
    img->sector_size = 2048; img->data_offset = 0;
    if (iso_read_sector(img, 16, buf) == 0 && memcmp(buf + 1, "CD001", 5) == 0) return 0;

    return -1;
}

/* Parse a CUE to find the first BINARY file reference. Returns a
 * heap-allocated path (caller frees) or NULL. */
static char *cue_find_bin(const char *cue_path)
{
    FILE *f = fopen(cue_path, "r");
    if (!f) return NULL;

    char line[1024];
    char *bin_rel = NULL;
    while (fgets(line, sizeof(line), f)) {
        const char *p = strstr(line, "FILE");
        if (!p) continue;
        const char *q1 = strchr(p, '"');
        if (!q1) continue;
        const char *q2 = strchr(q1 + 1, '"');
        if (!q2) continue;
        size_t len = q2 - q1 - 1;
        bin_rel = (char *)malloc(len + 1);
        memcpy(bin_rel, q1 + 1, len);
        bin_rel[len] = '\0';
        break;
    }
    fclose(f);
    if (!bin_rel) return NULL;

    /* If bin_rel is already absolute, use it. Otherwise make it relative
     * to the cue file's directory. */
    if (bin_rel[0] == '/') return bin_rel;

    const char *slash = strrchr(cue_path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(cue_path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (!slash) return bin_rel;

    size_t dir_len = slash - cue_path + 1;
    size_t bin_len = strlen(bin_rel);
    char *full = (char *)malloc(dir_len + bin_len + 1);
    memcpy(full, cue_path, dir_len);
    memcpy(full + dir_len, bin_rel, bin_len + 1);
    free(bin_rel);
    return full;
}

int iso_open(IsoImage *img, const char *path)
{
    if (!img || !path) return -1;
    memset(img, 0, sizeof(*img));

    /* If it's a cue, resolve to the real BIN first. */
    const char *dot = strrchr(path, '.');
    char *resolved = NULL;
    if (dot && ieq(dot, ".cue")) {
        resolved = cue_find_bin(path);
        if (!resolved) {
            fprintf(stderr, "[iso] cue '%s' did not reference any BIN\n", path);
            return -1;
        }
        path = resolved;
    }

    img->fp = fopen(path, "rb");
    if (!img->fp) {
        fprintf(stderr, "[iso] failed to open '%s'\n", path);
        free(resolved);
        return -1;
    }

    if (iso_detect_format(img) != 0) {
        fprintf(stderr, "[iso] no ISO-9660 PVD found in '%s'\n", path);
        fclose(img->fp);
        img->fp = NULL;
        free(resolved);
        return -1;
    }

    printf("[iso] opened '%s' (sector=%d, data_off=%d)\n",
           path, img->sector_size, img->data_offset);
    free(resolved);
    return 0;
}

void iso_close(IsoImage *img)
{
    if (img && img->fp) {
        fclose(img->fp);
        img->fp = NULL;
    }
}

/* Read an entire directory extent (may span several sectors). */
static uint8_t *read_dir_extent(IsoImage *img, int lba, long size, long *out_size)
{
    int n_sectors = (int)((size + ISO_SECTOR_USER_SIZE - 1) / ISO_SECTOR_USER_SIZE);
    long cap = (long)n_sectors * ISO_SECTOR_USER_SIZE;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return NULL;
    for (int i = 0; i < n_sectors; i++) {
        if (iso_read_sector(img, lba + i, buf + (long)i * ISO_SECTOR_USER_SIZE) != 0) {
            free(buf);
            return NULL;
        }
    }
    *out_size = size;
    return buf;
}

/* Compare a directory-record name against a target. ISO filenames may have
 * a ";1" version suffix and are usually uppercase. Match case-insensitively
 * and ignore the version suffix. */
static int name_matches(const char *rec_name, int rec_name_len, const char *target)
{
    int len = rec_name_len;
    /* Trim ";<ver>" */
    for (int i = 0; i < rec_name_len; i++) {
        if (rec_name[i] == ';') { len = i; break; }
    }
    /* Also trim trailing '.' that ISO adds for extensionless files */
    while (len > 0 && rec_name[len - 1] == '.') len--;
    int tlen = (int)strlen(target);
    if (len != tlen) return 0;
    for (int i = 0; i < len; i++) {
        if (toupper((unsigned char)rec_name[i]) != toupper((unsigned char)target[i])) return 0;
    }
    return 1;
}

int iso_find_file(IsoImage *img, const char *iso_path, IsoFile *out)
{
    if (!img || !img->fp || !iso_path || !out) return -1;

    uint8_t pvd[ISO_SECTOR_USER_SIZE];
    if (iso_read_sector(img, 16, pvd) != 0) return -1;

    /* Root directory record is at offset 156 in the PVD. Inside that
     * record, extent LBA is at offset 2 (little-endian 4 bytes) and
     * size at offset 10 (little-endian 4 bytes). */
    uint8_t *root = pvd + 156;
    int  dir_lba  = (int)((uint32_t)root[2]  | ((uint32_t)root[3]  << 8) |
                          ((uint32_t)root[4]  << 16) | ((uint32_t)root[5]  << 24));
    long dir_size = (long)((uint32_t)root[10] | ((uint32_t)root[11] << 8) |
                           ((uint32_t)root[12] << 16) | ((uint32_t)root[13] << 24));

    /* Walk the path component-by-component. */
    const char *p = iso_path;
    while (*p == '/' || *p == '\\') p++;

    for (;;) {
        /* Extract one component into `comp`. */
        char comp[64];
        int cn = 0;
        while (*p && *p != '/' && *p != '\\') {
            if (cn < (int)sizeof(comp) - 1) comp[cn++] = *p;
            p++;
        }
        comp[cn] = '\0';
        if (cn == 0) return -1;

        int is_last = (*p == '\0');
        while (*p == '/' || *p == '\\') p++;

        long dir_actual = 0;
        uint8_t *dir_data = read_dir_extent(img, dir_lba, dir_size, &dir_actual);
        if (!dir_data) return -1;

        int found = 0;
        long i = 0;
        while (i < dir_size) {
            uint8_t rec_len = dir_data[i];
            if (rec_len == 0) {
                /* Padding to end of sector. */
                long next = ((i / ISO_SECTOR_USER_SIZE) + 1) * ISO_SECTOR_USER_SIZE;
                if (next <= i) break;
                i = next;
                continue;
            }

            int  f_lba  = (int)((uint32_t)dir_data[i+2]  | ((uint32_t)dir_data[i+3]  << 8) |
                                ((uint32_t)dir_data[i+4]  << 16) | ((uint32_t)dir_data[i+5]  << 24));
            long f_size = (long)((uint32_t)dir_data[i+10] | ((uint32_t)dir_data[i+11] << 8) |
                                 ((uint32_t)dir_data[i+12] << 16) | ((uint32_t)dir_data[i+13] << 24));
            uint8_t flags    = dir_data[i + 25];
            uint8_t name_len = dir_data[i + 32];
            const char *name = (const char *)&dir_data[i + 33];
            int is_dir = (flags & 2) != 0;

            if (name_matches(name, name_len, comp)) {
                if (is_last && !is_dir) {
                    out->lba = f_lba;
                    out->size = f_size;
                    free(dir_data);
                    return 0;
                }
                if (!is_last && is_dir) {
                    dir_lba  = f_lba;
                    dir_size = f_size;
                    found = 1;
                    break;
                }
            }
            i += rec_len;
        }

        free(dir_data);
        if (!found) return -1;
        if (is_last) return -1;   /* matched a directory as the last component */
    }
}

int iso_read_file(IsoImage *img, const IsoFile *file,
                  long byte_offset, int size, void *buf)
{
    if (!img || !file || !buf || size <= 0) return 0;
    if (byte_offset < 0) return 0;
    if (byte_offset >= file->size) return 0;

    long remain_in_file = file->size - byte_offset;
    if (size > remain_in_file) size = (int)remain_in_file;

    uint8_t *dst = (uint8_t *)buf;
    uint8_t  sec[ISO_SECTOR_USER_SIZE];
    int      got = 0;

    long  pos   = byte_offset;
    while (got < size) {
        int  sec_idx = (int)(pos / ISO_SECTOR_USER_SIZE);
        int  sec_off = (int)(pos % ISO_SECTOR_USER_SIZE);
        int  chunk   = ISO_SECTOR_USER_SIZE - sec_off;
        if (chunk > size - got) chunk = size - got;

        if (iso_read_sector(img, file->lba + sec_idx, sec) != 0) break;
        memcpy(dst + got, sec + sec_off, chunk);
        got += chunk;
        pos += chunk;
    }
    return got;
}
