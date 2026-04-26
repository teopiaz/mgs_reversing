/* Loads actor placements from a TSV produced by tools/extract_gcl_actors.py
   and draws a small wireframe cube for each one in world space. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "libgte.h"
#include "libdg/gl_renderer.h"
#include "editor.h"

EdActor *g_actors        = NULL;
int      g_actor_count   = 0;
int      g_actor_capacity = 0;
int      g_actor_selected = -1;
int      g_actors_dirty  = 0;

extern void ed_world_to_eye(int wx, int wy, int wz, int eye[3]);
extern short ed_render_clip_dist(void);

static uint32_t parse_color(const char *s)
{
    /* Accepts "#RRGGBB" or "RRGGBB". Returns packed 0x00RRGGBB. */
    if (!s || !*s) return 0xC0C0C0;
    if (*s == '#') s++;
    uint32_t v = 0;
    for (int i = 0; i < 6 && s[i]; i++) {
        int c = s[i];
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return 0xC0C0C0;
        v = (v << 4) | (uint32_t)d;
    }
    return v;
}

static char *strip_nl(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
        *--e = 0;
    return s;
}

static void parse_tsv_line(char *line)
{
    /* Columns: type \t instance \t x \t y \t z \t color \t proc \t rot \t from_demo */
    char *cols[9] = {0};
    int n = 0;
    char *p = line;
    while (n < (int)(sizeof(cols)/sizeof(cols[0]))) {
        cols[n++] = p;
        char *t = strchr(p, '\t');
        if (!t) break;
        *t = 0;
        p = t + 1;
    }
    if (n < 5) return;   /* malformed */

    if (g_actor_count >= g_actor_capacity) {
        g_actor_capacity = g_actor_capacity ? g_actor_capacity * 2 : 64;
        g_actors = (EdActor *)realloc(g_actors, g_actor_capacity * sizeof(EdActor));
    }
    EdActor *a = &g_actors[g_actor_count];
    memset(a, 0, sizeof(*a));
    strncpy(a->type,     cols[0] ? cols[0] : "?", sizeof(a->type) - 1);
    strncpy(a->instance, cols[1] ? cols[1] : "?", sizeof(a->instance) - 1);
    a->pos[0]   = atoi(cols[2] ? cols[2] : "0");
    a->pos[1]   = atoi(cols[3] ? cols[3] : "0");
    a->pos[2]   = atoi(cols[4] ? cols[4] : "0");
    a->has_pos  = 1;
    a->color    = parse_color(cols[5]);
    if (cols[6]) strncpy(a->proc, cols[6], sizeof(a->proc) - 1);
    /* Rotation column: "b:N" (preferred), or "[a, b, c]" (camera/searchlight
       triplet — we just take the first), or empty. */
    a->rot_b = -1;
    if (cols[7] && cols[7][0]) {
        const char *r = cols[7];
        if (r[0] == 'b' && r[1] == ':') {
            a->rot_b = atoi(r + 2) & 0xFF;
        } else if (r[0] == '[') {
            a->rot_b = atoi(r + 1) & 0xFF;
        }
    }
    /* Eighth column (optional): "1" indicates the entry came from demo.gcl. */
    a->from_demo = (cols[8] && cols[8][0] == '1') ? 1 : 0;
    g_actor_count++;
}

void ed_actors_load(const char *path)
{
    free(g_actors);
    g_actors = NULL;
    g_actor_count = 0;
    g_actor_capacity = 0;

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("editor: could not open %s — actor markers disabled\n", path);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        strip_nl(line);
        if (!line[0]) continue;
        parse_tsv_line(line);
    }
    fclose(f);

    /* Resolve actor → KMD pointers once now (the GV cache is populated by
       FS_LoadStageRequest before this is called). Without caching here,
       the per-frame "Show Models" loop would call GV_GetCache for every
       unmatched actor every frame and spam [cache] MISS in stdout. */
    int with_model = 0;
    for (int i = 0; i < g_actor_count; i++) {
        ed_actor_kmd_for_type(g_actors[i].type, &g_actors[i].kmd_def);
        if (g_actors[i].kmd_def) with_model++;
    }
    g_actors_dirty = 0;
    printf("editor: loaded %d actor markers from %s (%d with model)\n",
           g_actor_count, path, with_model);
}

int ed_actors_save(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("editor: could not open %s for writing\n", path);
        return -1;
    }
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        char rot[16] = "";
        if (a->rot_b >= 0) snprintf(rot, sizeof(rot), "b:%d", a->rot_b);
        fprintf(f, "%s\t%s\t%d\t%d\t%d\t#%06X\t%s\t%s\t%d\n",
                a->type, a->instance,
                a->pos[0], a->pos[1], a->pos[2],
                a->color & 0xFFFFFF, a->proc, rot,
                a->from_demo ? 1 : 0);
    }
    fclose(f);
    g_actors_dirty = 0;
    printf("editor: wrote %d actors to %s\n", g_actor_count, path);
    return 0;
}

#define MARKER_R 250    /* PSX world units (~25cm at the game's scale) */

static void cube_lines(int cx, int cy, int cz, int r,
                       const unsigned char col[3])
{
    int x0 = cx - r, x1 = cx + r;
    int y0 = cy - r, y1 = cy + r;
    int z0 = cz - r, z1 = cz + r;
    int a[3], b[3];
    short cd = ed_render_clip_dist();

    #define LINE(ax,ay,az, bx,by,bz) \
        do { ed_world_to_eye(ax,ay,az,a); ed_world_to_eye(bx,by,bz,b); \
             gl_submit_line3d(a, b, col, col, cd); } while(0)
    LINE(x0,y0,z0, x1,y0,z0); LINE(x1,y0,z0, x1,y0,z1);
    LINE(x1,y0,z1, x0,y0,z1); LINE(x0,y0,z1, x0,y0,z0);
    LINE(x0,y1,z0, x1,y1,z0); LINE(x1,y1,z0, x1,y1,z1);
    LINE(x1,y1,z1, x0,y1,z1); LINE(x0,y1,z1, x0,y1,z0);
    LINE(x0,y0,z0, x0,y1,z0); LINE(x1,y0,z0, x1,y1,z0);
    LINE(x1,y0,z1, x1,y1,z1); LINE(x0,y0,z1, x0,y1,z1);
    /* Cross at center for visibility */
    LINE(x0,cy,cz, x1,cy,cz); LINE(cx,y0,cz, cx,y1,cz); LINE(cx,cy,z0, cx,cy,z1);
    #undef LINE
}

extern int g_show_actor_rotations;

static void rotation_arrow(int cx, int cy, int cz, int rot_b,
                           const unsigned char col[3])
{
    /* PSX byte rotation: 0..255 maps to 0..2π around the Y axis (yaw on
       horizontal plane). 0 = facing +Z. */
    float angle = (float)rot_b * (6.2831853f / 256.0f);
    float fx = sinf(angle), fz = cosf(angle);
    int len = MARKER_R * 4;
    int tip[3] = { cx + (int)(fx * len), cy, cz + (int)(fz * len) };
    int o[3], t[3];
    short cd = ed_render_clip_dist();
    ed_world_to_eye(cx, cy, cz, o);
    ed_world_to_eye(tip[0], tip[1], tip[2], t);
    gl_submit_line3d(o, t, col, col, cd);
    /* Two short tail strokes for an arrowhead, ~30° from the shaft. */
    float ang_l = angle + 2.6f, ang_r = angle - 2.6f;
    int hl[3] = { tip[0] + (int)(sinf(ang_l) * MARKER_R * 1.2f), cy,
                  tip[2] + (int)(cosf(ang_l) * MARKER_R * 1.2f) };
    int hr[3] = { tip[0] + (int)(sinf(ang_r) * MARKER_R * 1.2f), cy,
                  tip[2] + (int)(cosf(ang_r) * MARKER_R * 1.2f) };
    int eL[3], eR[3];
    ed_world_to_eye(hl[0], hl[1], hl[2], eL);
    ed_world_to_eye(hr[0], hr[1], hr[2], eR);
    gl_submit_line3d(t, eL, col, col, cd);
    gl_submit_line3d(t, eR, col, col, cd);
}

extern int g_show_actor_models;

void ed_actors_render(void)
{
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        if (!a->has_pos) continue;
        unsigned char col[3] = {
            (unsigned char)((a->color >> 16) & 0xFF),
            (unsigned char)((a->color >> 8)  & 0xFF),
            (unsigned char)( a->color        & 0xFF),
        };
        int r = MARKER_R;
        if (i == g_actor_selected) {
            col[0] = 255; col[1] = 255; col[2] = 60;
            r = (int)(MARKER_R * 1.6f);
        }
        /* Models mode: skip the cube if ed_render_frame already drew the
           KMD for this actor. Actors without a resolved KMD still show as
           cubes so nothing disappears. */
        int has_model_drawn = g_show_actor_models && a->kmd_def != NULL;
        if (!has_model_drawn) {
            cube_lines(a->pos[0], a->pos[1], a->pos[2], r, col);
        }
        if (g_show_actor_rotations && a->rot_b >= 0) {
            rotation_arrow(a->pos[0], a->pos[1], a->pos[2], a->rot_b, col);
        }
    }
}

/* Map actor type → KMD low-16-bit hash. Hand-curated for the obvious cases
   (CAMERA, DOOR, ITEM, …); others fall back to the cube marker. We look the
   id up directly in GV_CacheSystem so misses don't spam stdout. */
#include "libgv/libgv.h"
extern GV_CACHE_PAGE GV_CacheSystem;
extern int           GV_CacheID2(const char *name, int ext);

static void *quiet_get_cache(int id)
{
    int target = id & 0xFFFFFF;
    if (!target) return NULL;
    /* Same hashing scheme as GetCacheTag (linear-probe from id % MAX). */
    int start = target % MAX_CACHE_TAGS;
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        int slot = (start + i) % MAX_CACHE_TAGS;
        GV_CACHE_TAG *tag = &GV_CacheSystem.tags[slot];
        int cur = tag->id & 0xFFFFFF;
        if (cur == 0) return NULL;
        if (cur == target) return tag->ptr;
    }
    return NULL;
}

static int strcode_hash(const char *s)
{
    /* Mirrors port/gcl_tools/constants.py gv_strcode (5-bit rol + add). */
    unsigned int h = 0;
    while (*s) {
        h = ((h << 5) | (h >> 11)) & 0xFFFF;
        h = (h + (unsigned char)*s) & 0xFFFF;
        s++;
    }
    return (int)h;
}

/* True if `type` is a humanoid-style chara (skeletal animation, ~16 KMD
   models per asset). Used by the heuristic fallback below. */
static int is_humanoid_type(const char *type)
{
    static const char *names[] = {
        "WATCHER", "COMMANDER", "SNAKE", "MERYL", "MERYL7",
        "ZAKO", "ZAKO10", "ZAKO11A", "ZAKO11E", "ZAKO11F", "ZAKO14", "ZAKO19",
        "BOX", "ASIOTOKUN",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        if (strcmp(type, names[i]) == 0) return 1;
    }
    return 0;
}

#include "libdg/libdg.h"

/* Pick the best-matching humanoid-shaped KMD in the cache.
   Heuristic: many models (≥5), modest bbox (half-extent ≤ 2000) — that's
   how skeletal characters are authored. We pick the one with the most
   models so different humanoid actor types share the same body. */
static void *pick_humanoid_kmd(void)
{
    void *best = NULL;
    int   best_models = 0;
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[i];
        int id = t->id & 0xFFFFFF;
        if (id == 0 || !t->ptr) continue;
        if (((id >> 16) & 0xFF) != ('k' - 'a')) continue;
        DG_DEF *d = (DG_DEF *)t->ptr;
        if (d->n_models < 5 || d->n_models > 64) continue;
        int hx = d->max.vx - d->min.vx;
        int hy = d->max.vy - d->min.vy;
        int hz = d->max.vz - d->min.vz;
        if (hx <= 0 || hy <= 0 || hz <= 0) continue;
        if (hx > 4000 || hy > 4000 || hz > 4000) continue;
        if (d->n_models > best_models) {
            best = t->ptr;
            best_models = d->n_models;
        }
    }
    return best;
}

int ed_actor_kmd_for_type(const char *type, void **out_def)
{
    if (!type || !*type || !out_def) return 0;
    static const struct { const char *type; const char *kmd_name; } table[] = {
        /* The obvious ones — actor type maps directly to a KMD with the
           same string-code hash. Add more here as they're discovered. */
        { "CAMERA",       "camera"   },
        { "CAMERA2",      "camera"   },
        { "DOOR",         "door"     },
        { "DOOR2",        "door2"    },
        { "M_DOOR",       "door"     },
        { "ITEM",         "item"     },
        { "SEARCHLIGHT",  "slight"   },
        { NULL, NULL }
    };
    for (int i = 0; table[i].type; i++) {
        if (strcmp(type, table[i].type) != 0) continue;
        int id = GV_CacheID2(table[i].kmd_name, 'k');
        void *p = quiet_get_cache(id);
        if (p) { *out_def = p; return 1; }
    }
    /* Lower-cased type-as-name probe — covers cases where the KMD asset has
       the same string-code as the actor symbol. */
    {
        char lower[32];
        int n = 0;
        while (type[n] && n < (int)sizeof(lower) - 1) {
            char c = type[n];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            lower[n++] = c;
        }
        lower[n] = 0;
        int id = GV_CacheID2(lower, 'k');
        void *p = quiet_get_cache(id);
        if (p) { *out_def = p; return 1; }
    }
    /* Humanoid fallback — pick whatever skeletal-shaped KMD the stage
       loaded. Better than a cube even if it's not the *exact* model. */
    if (is_humanoid_type(type)) {
        void *p = pick_humanoid_kmd();
        if (p) { *out_def = p; return 1; }
    }
    return 0;
}

/* CPU ray vs. marker AABB picking. ray_origin/ray_dir are in world space.
   Returns the index of the closest hit, or -1. */
int ed_actors_pick_ray(const float ray_origin[3], const float ray_dir[3])
{
    int best = -1;
    float best_t = 1e30f;
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        if (!a->has_pos) continue;
        float bmin[3] = { (float)(a->pos[0] - MARKER_R),
                          (float)(a->pos[1] - MARKER_R),
                          (float)(a->pos[2] - MARKER_R) };
        float bmax[3] = { (float)(a->pos[0] + MARKER_R),
                          (float)(a->pos[1] + MARKER_R),
                          (float)(a->pos[2] + MARKER_R) };
        float tmin = -1e30f, tmax = 1e30f;
        for (int k = 0; k < 3; k++) {
            float d = ray_dir[k];
            if (fabsf(d) < 1e-6f) {
                if (ray_origin[k] < bmin[k] || ray_origin[k] > bmax[k]) {
                    tmin = 1e30f; break;
                }
            } else {
                float t1 = (bmin[k] - ray_origin[k]) / d;
                float t2 = (bmax[k] - ray_origin[k]) / d;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) break;
            }
        }
        if (tmin <= tmax && tmin >= 0 && tmin < best_t) {
            best_t = tmin;
            best = i;
        }
    }
    return best;
}
