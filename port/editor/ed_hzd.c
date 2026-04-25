/* HZD (Hazard) overlay rendering.
   Walks the loaded HZD_MAP and emits world-space line primitives via
   gl_submit_line3d. Distinguishes walls / floors / traps / cameras / zones /
   routes by color and toggles visibility per layer.

   Coord swap: HZD_VEC is { short x, z, y, h } — z and y are stored in the
   opposite order from PSX SVECTOR, so the world position of a HZD vertex is
   (v.x, v.y, v.z) with the v.y/v.z fields treated as Y/Z. Forgetting this
   produces tilted floor quads. */

#include <stdio.h>
#include <stdint.h>

#include "libgte.h"
#include "fmt_hzd.h"
#include "libdg/gl_renderer.h"
#include "editor.h"

extern void          ed_world_to_eye(int wx, int wy, int wz, int eye[3]);
extern short         ed_render_clip_dist(void);

static inline void hzd_world_pos(const HZD_VEC *v, int *x, int *y, int *z)
{
    *x = v->x;
    *y = v->y;
    *z = v->z;
}

static void line_world(int wx0, int wy0, int wz0,
                       int wx1, int wy1, int wz1,
                       const unsigned char col[3])
{
    int a[3], b[3];
    ed_world_to_eye(wx0, wy0, wz0, a);
    ed_world_to_eye(wx1, wy1, wz1, b);
    gl_submit_line3d(a, b, col, col, ed_render_clip_dist());
}

static void aabb_world(int x0, int y0, int z0,
                       int x1, int y1, int z1,
                       const unsigned char col[3])
{
    /* 12 edges of the axis-aligned bbox. */
    line_world(x0,y0,z0, x1,y0,z0, col);
    line_world(x1,y0,z0, x1,y0,z1, col);
    line_world(x1,y0,z1, x0,y0,z1, col);
    line_world(x0,y0,z1, x0,y0,z0, col);
    line_world(x0,y1,z0, x1,y1,z0, col);
    line_world(x1,y1,z0, x1,y1,z1, col);
    line_world(x1,y1,z1, x0,y1,z1, col);
    line_world(x0,y1,z1, x0,y1,z0, col);
    line_world(x0,y0,z0, x0,y1,z0, col);
    line_world(x1,y0,z0, x1,y1,z0, col);
    line_world(x1,y0,z1, x1,y1,z1, col);
    line_world(x0,y0,z1, x0,y1,z1, col);
}

static const unsigned char COL_WALL[3]    = {255,  90,  90};
static const unsigned char COL_WALL_SEL[3]= {255, 255,  60};
static const unsigned char COL_FLOOR[3]   = { 90, 220, 255};
static const unsigned char COL_FLOOR_SEL[3]={255, 255,  60};
static const unsigned char COL_TRAP[3]    = {180, 255,  80};
static const unsigned char COL_TRAP_SEL[3]={255, 255,  60};
static const unsigned char COL_CAM[3]     = {255, 180, 255};
static const unsigned char COL_CAM_SEL[3] ={255, 255,  60};
static const unsigned char COL_ZONE[3]    = {120, 120, 200};
static const unsigned char COL_ROUTE[3]   = {255, 200,  80};
static const unsigned char COL_ROUTE_SEL[3]={255, 255, 60};

void ed_hzd_render(void)
{
    HZD_MAP *m = (HZD_MAP *)g_stage.hzd_map;
    if (!m) return;

    int wall_idx = 0, floor_idx = 0, trap_idx = 0, cam_idx = 0;

    /* Walls / floors / traps / cameras live inside per-group arrays. */
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];

        if (g_show_walls && g->walls) {
            for (int i = 0; i < g->n_walls; i++, wall_idx++) {
                HZD_SEG *s = &g->walls[i];
                int x0, y0, z0, x1, y1, z1;
                hzd_world_pos(&s->p1, &x0, &y0, &z0);
                hzd_world_pos(&s->p2, &x1, &y1, &z1);
                const unsigned char *c = (g_sel_wall == wall_idx) ? COL_WALL_SEL : COL_WALL;
                /* Walls are conceptually full-height; visualize as a vertical
                   strip from y to y-1500 plus the base segment. */
                line_world(x0, y0, z0, x1, y1, z1, c);
                line_world(x0, y0 - 1500, z0, x1, y1 - 1500, z1, c);
                line_world(x0, y0, z0, x0, y0 - 1500, z0, c);
                line_world(x1, y1, z1, x1, y1 - 1500, z1, c);
            }
        }

        if (g_show_floors && g->floors) {
            for (int i = 0; i < g->n_floors; i++, floor_idx++) {
                HZD_FLR *f = &g->floors[i];
                const HZD_VEC *p[4] = {&f->p1, &f->p2, &f->p3, &f->p4};
                const unsigned char *c = (g_sel_floor == floor_idx) ? COL_FLOOR_SEL : COL_FLOOR;
                for (int e = 0; e < 4; e++) {
                    int n = (e + 1) & 3;
                    line_world(p[e]->x, p[e]->y, p[e]->z,
                               p[n]->x, p[n]->y, p[n]->z, c);
                }
            }
        }

        /* triggers: union of HZD_TRP and HZD_CAM. n_triggers is the total. */
        if (g->triggers) {
            for (int i = 0; i < g->n_triggers; i++) {
                HZD_TRG *t = &g->triggers[i];
                /* Heuristic: HZD_CAM is wider than HZD_TRP (it has cam+orient
                   after the bbox). Check orient differs from b1/b2. */
                int dx = t->cam.cam.x - t->cam.b1.x;
                int dy = t->cam.cam.y - t->cam.b1.y;
                int dz = t->cam.cam.z - t->cam.b1.z;
                int looks_like_camera = (dx != 0 || dy != 0 || dz != 0) &&
                                        (t->cam.orient.x | t->cam.orient.y | t->cam.orient.z) != 0;

                if (looks_like_camera && g_show_cameras) {
                    const unsigned char *c = (g_sel_cam == cam_idx) ? COL_CAM_SEL : COL_CAM;
                    int x0=t->cam.b1.x, y0=t->cam.b1.y, z0=t->cam.b1.z;
                    int x1=t->cam.b2.x, y1=t->cam.b2.y, z1=t->cam.b2.z;
                    aabb_world(x0,y0,z0, x1,y1,z1, c);
                    line_world(t->cam.cam.x, t->cam.cam.y, t->cam.cam.z,
                               t->cam.orient.x, t->cam.orient.y, t->cam.orient.z, c);
                    cam_idx++;
                } else if (g_show_traps) {
                    const unsigned char *c = (g_sel_trap == trap_idx) ? COL_TRAP_SEL : COL_TRAP;
                    int x0=t->trap.b1.x, y0=t->trap.b1.y, z0=t->trap.b1.z;
                    int x1=t->trap.b2.x, y1=t->trap.b2.y, z1=t->trap.b2.z;
                    aabb_world(x0,y0,z0, x1,y1,z1, c);
                    trap_idx++;
                }
            }
        }
    }

    /* Zones (navmesh sectors). One per HZD_ZON. */
    if (g_show_zones && m->zones) {
        for (int i = 0; i < m->n_zones; i++) {
            HZD_ZON *z = &m->zones[i];
            int hw = z->w / 2, hh = z->h / 2;
            int x0 = z->x - hw, x1 = z->x + hw;
            int z0 = z->z - hh, z1 = z->z + hh;
            int y  = z->y;
            line_world(x0,y,z0, x1,y,z0, COL_ZONE);
            line_world(x1,y,z0, x1,y,z1, COL_ZONE);
            line_world(x1,y,z1, x0,y,z1, COL_ZONE);
            line_world(x0,y,z1, x0,y,z0, COL_ZONE);
        }
    }

    /* Routes: polylines through patrol points. */
    if (g_show_routes && m->routes) {
        for (int ri = 0; ri < m->n_routes; ri++) {
            HZD_PAT *r = &m->routes[ri];
            if (!r->points || r->n_points <= 1) continue;
            const unsigned char *c = (g_sel_route == ri) ? COL_ROUTE_SEL : COL_ROUTE;
            for (int p = 0; p < r->n_points - 1; p++) {
                HZD_PTP *a = &r->points[p];
                HZD_PTP *b = &r->points[p + 1];
                line_world(a->x, a->y, a->z, b->x, b->y, b->z, c);
            }
        }
    }
}
