/*
 * Host-filesystem-backed memory card backend for the macOS port.
 *
 * Replaces the PSX BIOS _card_xx event-driven async sectoring with synchronous
 * fopen/fread/fwrite. Each "memory card" is a directory; each save is a real
 * file containing the exact buffer datasave.c / jpegcam.c construct (header +
 * CLUT + icon + title + payload).
 *
 * Card root resolution:
 *   1. $PORT_MEMCARD_DIR  -> $PORT_MEMCARD_DIR/{0,1}/
 *   2. $HOME/Library/Application Support/MGS/memcard/{0,1}/   (macOS default)
 *   3. ./memcard/{0,1}/                                       (fallback)
 *
 * Status semantics required by source/menu/datasave.c:
 *   memcard_get_status() > 0  -> in flight
 *   memcard_get_status() == 0 -> success
 *   memcard_get_status() < 0  -> error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "memcard/memcard.h"
#include "mts/mts.h"

extern MEM_CARD gMemCards[2]; /* defined in source/data/bss.c */

static int  g_io_status = 0;          /* >0 busy, 0 ok, -1 err */
static char g_card_dirs[2][1024];
static int  g_card_root_resolved = 0;

static int port_mkdir_p(const char *path)
{
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void resolve_card_roots(void)
{
    if (g_card_root_resolved) return;
    g_card_root_resolved = 1;

    char base[1024];
    const char *override = getenv("PORT_MEMCARD_DIR");
    const char *home = getenv("HOME");

    if (override && *override)
    {
        snprintf(base, sizeof(base), "%s", override);
    }
    else if (home && *home)
    {
        snprintf(base, sizeof(base), "%s/Library/Application Support/MGS/memcard", home);
    }
    else
    {
        snprintf(base, sizeof(base), "./memcard");
    }

    for (int port = 0; port < 2; port++)
    {
        snprintf(g_card_dirs[port], sizeof(g_card_dirs[port]), "%s/%d", base, port);
        if (port_mkdir_p(g_card_dirs[port]) != 0)
        {
            fprintf(stderr, "memcard: cannot create %s: %s\n", g_card_dirs[port], strerror(errno));
        }
    }
    printf("memcard: card 0 -> %s\n", g_card_dirs[0]);
    printf("memcard: card 1 -> %s\n", g_card_dirs[1]);
}

static void build_path(int port, const char *filename, char *out, size_t outsz)
{
    /* PSX filenames are 20 raw bytes. They are normally pure ASCII
       ('BISLPM-99999XXXXXXXX') but defensively replace '/' with '_'
       to avoid host path traversal. */
    char safe[32];
    int n = 0;
    for (int i = 0; i < 20 && filename[i] != '\0' && n < (int)sizeof(safe) - 1; i++)
    {
        char c = filename[i];
        safe[n++] = (c == '/') ? '_' : c;
    }
    safe[n] = '\0';
    snprintf(out, outsz, "%s/%s", g_card_dirs[port], safe);
}

void memcard_init(void)
{
    resolve_card_roots();
    memset(gMemCards, 0, sizeof(MEM_CARD) * 2);
    gMemCards[0].card_idx = 0;
    gMemCards[1].card_idx = 1;
    gMemCards[0].last_op = 1;
    gMemCards[1].last_op = 1;
    g_io_status = 0;
}

void memcard_exit(void)
{
}

void memcard_reset_status(void)
{
    g_io_status = 0;
    gMemCards[0].last_op = 1;
    gMemCards[1].last_op = 1;
}

void memcard_retry(int port)
{
    (void)port;
}

int memcard_check(int port)
{
    if (port < 0 || port > 1) return 0x80000003;
    resolve_card_roots();

    /* PSX impl yields to the vsync task while the BIOS does its async
       sector probe. The save-menu state machine spin-polls memcard_check
       in init_file_mode_helper_helper_80049EDC waiting for dword_800ABB5C
       to be set by the UI thread, and counts on this yield to let the UI
       thread run. Without it the coroutine starves the main thread and
       the menu never advances. */
    mts_wait_vbl(1);

    /* 0 == "card OK, formatted, present" — matches the state-machine
       branch (temp_v1 != 3 path). The 0x1000000 "card changed" bit is
       never set, so the menu treats the card as stable. */
    if (access(g_card_dirs[port], R_OK | W_OK) != 0)
    {
        if (port_mkdir_p(g_card_dirs[port]) != 0)
        {
            return 0x80000003;
        }
    }
    return 0;
}

MEM_CARD *memcard_get_files(int port)
{
    if (port < 0 || port > 1) return NULL;
    resolve_card_roots();

    MEM_CARD *card = &gMemCards[port];
    card->card_idx = (unsigned char)port;
    card->last_op = 1;
    card->file_count = 0;
    int used_blocks = 0;

    DIR *dir = opendir(g_card_dirs[port]);
    if (!dir)
    {
        card->free_blocks = 15;
        return card;
    }

    struct dirent *ent;
    int slot = 0;
    while (slot < 15 && (ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;

        char full[1100];
        snprintf(full, sizeof(full), "%s/%s", g_card_dirs[port], ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        MEM_CARD_FILE *f = &card->files[slot];
        memset(f, 0, sizeof(*f));
        /* Name is 20 raw bytes, no null terminator. field_14 (next struct
           byte) is 0 thanks to the memset, which gives strcmp a sentinel. */
        size_t namelen = strlen(ent->d_name);
        if (namelen > 20) namelen = 20;
        memcpy(f->name, ent->d_name, namelen);
        for (size_t i = namelen; i < 20; i++) f->name[i] = ' ';
        f->field_14 = 0;
        f->field_18_size = (int)st.st_size;

        used_blocks += (st.st_size + MC_BLOCK_SIZE - 1) / MC_BLOCK_SIZE;
        slot++;
    }
    closedir(dir);

    card->file_count = (char)slot;
    int free = 15 - used_blocks;
    if (free < 0) free = 0;
    card->free_blocks = (char)free;
    return card;
}

int memcard_delete(int port, const char *filename)
{
    if (port < 0 || port > 1 || !filename) return 0;
    resolve_card_roots();

    char path[1100];
    build_path(port, filename, path, sizeof(path));
    if (unlink(path) == 0) return 1;
    fprintf(stderr, "memcard: delete %s failed: %s\n", path, strerror(errno));
    return 0;
}

int memcard_format(int port)
{
    if (port < 0 || port > 1) return 0;
    resolve_card_roots();

    DIR *dir = opendir(g_card_dirs[port]);
    if (!dir)
    {
        if (port_mkdir_p(g_card_dirs[port]) != 0) return 0;
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;
        char path[1100];
        snprintf(path, sizeof(path), "%s/%s", g_card_dirs[port], ent->d_name);
        unlink(path);
    }
    closedir(dir);
    return 1;
}

void memcard_write(int port, const char *filename, int offset, char *buffer, int size)
{
    if (port < 0 || port > 1 || !filename || !buffer || size <= 0)
    {
        g_io_status = -1;
        return;
    }
    resolve_card_roots();

    char path[1100];
    build_path(port, filename, path, sizeof(path));

    /* Open for read+write, create if missing. Don't truncate so a partial
       offset write into an existing file leaves the rest intact (matches
       PSX semantics where memcard_write only updates the requested range). */
    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f)
    {
        fprintf(stderr, "memcard: open %s for write failed: %s\n", path, strerror(errno));
        g_io_status = -1;
        return;
    }

    if (offset > 0) fseek(f, offset, SEEK_SET);
    size_t written = fwrite(buffer, 1, (size_t)size, f);
    fclose(f);

    if ((int)written != size)
    {
        fprintf(stderr, "memcard: short write %s (%zu of %d)\n", path, written, size);
        g_io_status = -1;
        return;
    }
    printf("memcard: wrote %d bytes to %s\n", size, path);
    g_io_status = 0;
}

void memcard_read(int port, const char *filename, int offset, char *buffer, int size)
{
    if (port < 0 || port > 1 || !filename || !buffer || size <= 0)
    {
        g_io_status = -1;
        return;
    }
    resolve_card_roots();

    char path[1100];
    build_path(port, filename, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "memcard: open %s for read failed: %s\n", path, strerror(errno));
        g_io_status = -1;
        return;
    }

    if (offset > 0) fseek(f, offset, SEEK_SET);
    size_t got = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    if (got == 0)
    {
        g_io_status = -1;
        return;
    }
    if ((int)got < size)
    {
        memset(buffer + got, 0, (size_t)size - got);
    }
    printf("memcard: read %zu bytes from %s\n", got, path);
    g_io_status = 0;
}

int memcard_get_status(void)
{
    return g_io_status;
}
