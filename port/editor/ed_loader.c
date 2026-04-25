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
extern GV_CACHE_PAGE GV_CacheSystem;

static void *find_first_with_ext(int ext_char, int *out_id)
{
    int target_ext = ext_char - 'a';
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->ptr) continue;
        int ext = (id >> 16) & 0xFF;
        if (ext == target_ext) {
            if (out_id) *out_id = id;
            return t->ptr;
        }
    }
    return NULL;
}

/* Heuristic for picking the map KMD: among all loaded 'k' entries pick the
   one with the largest bbox volume. Per-actor KMDs (door, item, watcher) are
   tiny — typically < 1000 units cubed — while the stage itself spans tens of
   thousands. */
static void *find_largest_kmd(int *out_id)
{
    void *best = NULL;
    long  best_volume = -1;
    int   best_id = 0;
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->ptr) continue;
        if (((id >> 16) & 0xFF) != ('k' - 'a')) continue;
        DG_DEF *d = (DG_DEF *)t->ptr;
        if (d->n_models <= 0 || d->n_models > 256) continue;
        long sx = (long)d->max.vx - d->min.vx;
        long sy = (long)d->max.vy - d->min.vy;
        long sz = (long)d->max.vz - d->min.vz;
        if (sx < 0 || sy < 0 || sz < 0) continue;
        long vol = sx * sy + sy * sz + sx * sz;  /* surface-area proxy */
        if (vol > best_volume) {
            best_volume = vol;
            best        = t->ptr;
            best_id     = id;
        }
    }
    if (out_id) *out_id = best_id;
    return best;
}

int ed_load_stage(const char *stage_name)
{
    memset(&g_stage, 0, sizeof(g_stage));
    strncpy(g_stage.stage_name, stage_name, sizeof(g_stage.stage_name) - 1);

    void *info = FS_LoadStageRequest(stage_name);
    (void)info;

    g_stage.map_def = find_largest_kmd(&g_stage.map_def_id);
    g_stage.hzd_map = find_first_with_ext('h', &g_stage.hzd_id);

    int kmd_count = 0, hzd_count = 0, pcx_count = 0;
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->ptr) continue;
        int ext = (id >> 16) & 0xFF;
        if      (ext == 'k' - 'a') kmd_count++;
        else if (ext == 'h' - 'a') hzd_count++;
        else if (ext == 'p' - 'a') pcx_count++;
    }

    printf("editor: stage '%s' loaded — %d KMDs, %d HZDs, %d PCXes in cache\n",
           stage_name, kmd_count, hzd_count, pcx_count);

    /* Dump every KMD so we can sanity-check which one we picked. */
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->ptr) continue;
        if (((id >> 16) & 0xFF) != ('k' - 'a')) continue;
        DG_DEF *d = (DG_DEF *)t->ptr;
        const char *mark = (t->ptr == g_stage.map_def) ? " *MAP*" : "";
        printf("editor:   KMD id=0x%X models=%d bbox=(%d..%d, %d..%d, %d..%d)%s\n",
               id, d->n_models,
               d->min.vx, d->max.vx, d->min.vy, d->max.vy, d->min.vz, d->max.vz,
               mark);
    }
    if (g_stage.hzd_map) {
        printf("editor:   HZD map: id=0x%X\n", g_stage.hzd_id);
    }

    g_stage.loaded = (g_stage.map_def != NULL);
    return g_stage.loaded ? 0 : -1;
}
