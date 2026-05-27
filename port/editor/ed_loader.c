/* Stage loader.
   Calls FS_LoadStageRequest which walks the stage's DATACNF, calls the
   already-registered libdg/libhzd loaders for each entry, and uploads PCX
   textures into VRAM. After the call returns, we look for the map's KMD and
   HZD by scanning the GV cache for the first 'k' and 'h' tags found. This
   matches what the engine itself does for non-overlay stages — there is
   typically exactly one KMD and one HZD per stage. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libhzd/libhzd.h"
#include "editor.h"

EditorStage g_stage;

extern void *FS_LoadStageRequest(const char *dirname);
extern CACHE Caches[MAX_CACHES];

/* libfs.c — editor accessors */
extern int         port_fs_stage_count(void);
extern const char *port_fs_stage_name(int idx);
extern void        port_fs_unload_stage(void);

/* gl_renderer.c — editor calls this to drop the previous stage's textures */
extern uint16_t vram[512][1024];
extern void gl_renderer_mark_vram_dirty(int y0, int y1);

/* libgv cache — used to clear all entries before reloading a stage. */
extern void GV_InitCacheSystem(void);

/* libdg texture cache — needs the same wipe; stale entries from the old
   stage would otherwise be returned by DG_GetTexture for the new stage's
   KMDs (their material IDs are 16-bit hashes that can collide). */
extern void DG_InitTextureSystem(void);

/* GV memory heap reset. We use this instead of GV_FreeMemory so accumulated
   per-stage allocations (HZD_DEF allocations from hzd_loader, etc.) don't
   fragment the 2 MiB pool — a stage slightly larger than the previous one
   wouldn't fit in the freed hole otherwise. */
extern void  GV_InitMemorySystem(int which, int dynamic, void *memory, int size);
extern void *port_normal_memory;
#define GV_NORMAL_MEMORY_RESET_SIZE 0x200000   /* matches port_memory.c */

int ed_stage_count(void) { return port_fs_stage_count(); }

const char *ed_stage_name(int idx)
{
    static char buf[16];
    const char *name = port_fs_stage_name(idx);
    if (!name) return NULL;
    /* Stage names are 8 chars NUL-padded; copy into a NUL-terminated buf. */
    int n = 0;
    while (n < 8 && name[n]) { buf[n] = name[n]; n++; }
    buf[n] = 0;
    return buf;
}

static void *find_first_with_ext(int ext_char, int *out_id)
{
    int target_ext = ext_char - 'a';
    for (int i = 0; i < MAX_CACHES; i++) {
        CACHE *t = &Caches[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->buf) continue;
        int ext = (id >> 16) & 0xFF;
        if (ext == target_ext) {
            if (out_id) *out_id = id;
            return t->buf;
        }
    }
    return NULL;
}

/* Pick every "map-sized" KMD. Stages either ship as one big mesh (s01a)
   or as several room KMDs (s02a's tank hangar has 7). The actor / item /
   door KMDs that also live in the cache have tiny asymmetric bboxes
   (typically < 2000 units half-extent), while stage rooms have large
   roughly-cube bboxes — that's how we tell them apart. We render every
   match at world origin; actor instantiation isn't part of the editor. */
static void collect_map_kmds(EditorStage *s)
{
    s->n_map_defs = 0;
    for (int i = 0; i < MAX_CACHES; i++) {
        CACHE *t = &Caches[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->buf) continue;
        if (((id >> 16) & 0xFF) != ('k' - 'a')) continue;
        DG_DEF *d = (DG_DEF *)t->buf;
        if (d->n_models <= 0 || d->n_models > 256) continue;
        long sx = (long)d->max.vx - d->min.vx;
        long sy = (long)d->max.vy - d->min.vy;
        long sz = (long)d->max.vz - d->min.vz;
        if (sx <= 0 || sy <= 0 || sz <= 0) continue;
        /* Reject anything an actor model could plausibly be — both small
           half-extent (< 5000) and asymmetric bbox (off-center origin) are
           strong signals of an actor / pickup / door. */
        long max_dim = sx; if (sy > max_dim) max_dim = sy; if (sz > max_dim) max_dim = sz;
        if (max_dim < 10000) continue;
        if (s->n_map_defs >= EDITOR_MAX_MAP_KMDS) break;
        s->map_defs[s->n_map_defs] = t->buf;
        s->map_ids [s->n_map_defs] = id;
        s->n_map_defs++;
    }
}

/* Drop the previous stage: nuke the entire GV normal-memory heap (frees the
   DATACNF buffer + every per-stage HZD allocation), wipe the GV cache and
   DG texture cache (entries point into the now-freed memory), and clear
   VRAM so stale textures from the old stage don't bleed into the new one. */
static void ed_unload_stage(void)
{
    GV_InitMemorySystem(GV_NORMAL_MEMORY, 0,
                        port_normal_memory, GV_NORMAL_MEMORY_RESET_SIZE);
    port_fs_unload_stage();   /* clears its own static state; the buffer it
                                 would have freed is already gone above. */
    GV_InitCacheSystem();
    DG_InitTextureSystem();
    memset(vram, 0, sizeof(vram));
    gl_renderer_mark_vram_dirty(0, 512);
}

int ed_load_stage(const char *stage_name)
{
    if (!stage_name || !stage_name[0]) {
        fprintf(stderr, "editor: ignoring empty stage name\n");
        return -1;
    }

    /* Snapshot the name first — callers (e.g. the Reload button) may pass
       g_stage.stage_name itself, which we're about to memset+overwrite.
       __strncpy_chk aborts when src and dst overlap. */
    char name_buf[16] = {0};
    {
        size_t n = strnlen(stage_name, sizeof(name_buf) - 1);
        memcpy(name_buf, stage_name, n);
    }

    /* Always unload — even if the previous load failed (g_stage.loaded == 0)
       the GV heap may still hold HZD allocations from a partial load that
       would fragment a fresh allocation. */
    ed_unload_stage();
    memset(&g_stage, 0, sizeof(g_stage));
    memcpy(g_stage.stage_name, name_buf, sizeof(name_buf));

    void *info = FS_LoadStageRequest(name_buf);
    (void)info;

    collect_map_kmds(&g_stage);
    g_stage.hzd_map = find_first_with_ext('h', &g_stage.hzd_id);

    int kmd_count = 0, hzd_count = 0, pcx_count = 0;
    for (int i = 0; i < MAX_CACHES; i++) {
        CACHE *t = &Caches[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->buf) continue;
        int ext = (id >> 16) & 0xFF;
        if      (ext == 'k' - 'a') kmd_count++;
        else if (ext == 'h' - 'a') hzd_count++;
        else if (ext == 'p' - 'a') pcx_count++;
    }

    printf("editor: stage '%s' loaded — %d KMDs (%d picked as map), %d HZDs, %d PCXes\n",
           name_buf, kmd_count, g_stage.n_map_defs, hzd_count, pcx_count);

    /* Dump every KMD so we can sanity-check which ones the heuristic took. */
    for (int i = 0; i < MAX_CACHES; i++) {
        CACHE *t = &Caches[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->buf) continue;
        if (((id >> 16) & 0xFF) != ('k' - 'a')) continue;
        DG_DEF *d = (DG_DEF *)t->buf;
        int picked = 0;
        for (int k = 0; k < g_stage.n_map_defs; k++)
            if (g_stage.map_defs[k] == t->buf) { picked = 1; break; }
        const char *mark = picked ? " *MAP*" : "";
        printf("editor:   KMD id=0x%X models=%d bbox=(%d..%d, %d..%d, %d..%d)%s\n",
               id, d->n_models,
               d->min.vx, d->max.vx, d->min.vy, d->max.vy, d->min.vz, d->max.vz,
               mark);
    }
    if (g_stage.hzd_map) {
        printf("editor:   HZD map: id=0x%X\n", g_stage.hzd_id);
    }

    g_stage.loaded = (g_stage.n_map_defs > 0);

    /* Per-stage actor markers. File is generated by tools/extract_actors --batch
       (which fans out tools/extract_actors over every decompiled
       scenerio.gcl). Missing file is fine — most stages don't have a
       decompile yet. */
    char tsv_path[64];
    /* Pass the JSON path; ed_actors_load prefers JSON and falls back to
     * the sibling .tsv if the JSON isn't present yet. */
    snprintf(tsv_path, sizeof(tsv_path), "data/%s_actors.json", name_buf);
    ed_actors_load(tsv_path);

    return g_stage.loaded ? 0 : -1;
}

int ed_stage_collect_entries(EdStageEntry *out, int max)
{
    int n = 0;
    for (int i = 0; i < MAX_CACHES && n < max; i++) {
        CACHE *t = &Caches[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->buf) continue;
        out[n].id = id;
        out[n].ext = (char)('a' + ((id >> 16) & 0xFF));
        out[n].approx_size = -1;
        n++;
    }
    return n;
}
