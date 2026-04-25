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
    /* Columns: type \t instance \t x \t y \t z \t color \t proc \t rot */
    char *cols[8] = {0};
    int n = 0;
    char *p = line;
    while (n < 8) {
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
    printf("editor: loaded %d actor markers from %s\n", g_actor_count, path);
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
        cube_lines(a->pos[0], a->pos[1], a->pos[2], r, col);
    }
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
