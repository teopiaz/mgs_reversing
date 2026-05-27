/* HZD (Hazard) overlay rendering.
   Walks the loaded HZD_DEF and emits world-space line primitives via
   gl_submit_line3d. Distinguishes walls / floors / traps / cameras / zones /
   routes by color and toggles visibility per layer.

   Coord swap: HZD_VEC is { short x, z, y, h } — z and y are stored in the
   opposite order from PSX SVECTOR, so the world position of a HZD vertex is
   (v.x, v.y, v.z) with the v.y/v.z fields treated as Y/Z. Forgetting this
   produces tilted floor quads. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

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
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
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
                    /* Frustum: shaft cam→orient, plus 4 lines fanning out
                       to a square at the orient point so the viewing
                       direction is obvious from any angle. */
                    int cx = t->cam.cam.x,    cy = t->cam.cam.y,    cz = t->cam.cam.z;
                    int ox = t->cam.orient.x, oy = t->cam.orient.y, oz = t->cam.orient.z;
                    line_world(cx, cy, cz, ox, oy, oz, c);
                    /* Pick a perpendicular by zeroing the smallest component. */
                    int dx = ox-cx, dy = oy-cy, dz = oz-cz;
                    int half = 600;   /* corner offset at the orient end */
                    int u[3], v[3];
                    if (abs(dx) <= abs(dy) && abs(dx) <= abs(dz)) {
                        u[0]=0; u[1]=-dz; u[2]=dy;
                    } else if (abs(dy) <= abs(dz)) {
                        u[0]=-dz; u[1]=0; u[2]=dx;
                    } else {
                        u[0]=-dy; u[1]=dx; u[2]=0;
                    }
                    /* Normalise u/v roughly to ±half. */
                    int ulen = (int)sqrt((double)u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
                    if (ulen < 1) ulen = 1;
                    u[0] = u[0]*half/ulen; u[1] = u[1]*half/ulen; u[2] = u[2]*half/ulen;
                    v[0] = dy*u[2] - dz*u[1];
                    v[1] = dz*u[0] - dx*u[2];
                    v[2] = dx*u[1] - dy*u[0];
                    int vlen = (int)sqrt((double)v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
                    if (vlen < 1) vlen = 1;
                    v[0] = v[0]*half/vlen; v[1] = v[1]*half/vlen; v[2] = v[2]*half/vlen;
                    int corners[4][3] = {
                        { ox + u[0] + v[0], oy + u[1] + v[1], oz + u[2] + v[2] },
                        { ox - u[0] + v[0], oy - u[1] + v[1], oz - u[2] + v[2] },
                        { ox - u[0] - v[0], oy - u[1] - v[1], oz - u[2] - v[2] },
                        { ox + u[0] - v[0], oy + u[1] - v[1], oz + u[2] - v[2] },
                    };
                    for (int k = 0; k < 4; k++) {
                        line_world(cx, cy, cz,
                                   corners[k][0], corners[k][1], corners[k][2], c);
                        line_world(corners[k][0], corners[k][1], corners[k][2],
                                   corners[(k+1)&3][0], corners[(k+1)&3][1], corners[(k+1)&3][2], c);
                    }
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

    /* Routes: polylines through patrol points + tiny X markers at each
     * point so node positions are visible at a glance. The init point
     * (route start) gets a slightly bigger marker so the playback
     * direction can be inferred. */
    if (g_show_routes && m->routes) {
        for (int ri = 0; ri < m->n_routes; ri++) {
            HZD_PAT *r = &m->routes[ri];
            if (!r->points || r->n_points <= 0) continue;
            const unsigned char *c = (g_sel_route == ri) ? COL_ROUTE_SEL : COL_ROUTE;
            for (int p = 0; p + 1 < r->n_points; p++) {
                HZD_PTP *a = &r->points[p];
                HZD_PTP *b = &r->points[p + 1];
                line_world(a->x, a->y, a->z, b->x, b->y, b->z, c);
            }
            /* Per-point markers — small + cross in the XZ plane. The first
             * point uses 1.6× size to mark the route start. */
            for (int p = 0; p < r->n_points; p++) {
                HZD_PTP *pt = &r->points[p];
                int rad = (p == r->init_point) ? 240 : 150;
                line_world(pt->x - rad, pt->y, pt->z,
                           pt->x + rad, pt->y, pt->z, c);
                line_world(pt->x, pt->y, pt->z - rad,
                           pt->x, pt->y, pt->z + rad, c);
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Inspector helpers — flat-index walks over walls/floors/traps/cameras.     */
/*---------------------------------------------------------------------------*/

extern int ed_world_to_screen(int wx, int wy, int wz, float *sx, float *sy);

static int trap_looks_like_camera(const HZD_TRG *t)
{
    int dx = t->cam.cam.x - t->cam.b1.x;
    int dy = t->cam.cam.y - t->cam.b1.y;
    int dz = t->cam.cam.z - t->cam.b1.z;
    int orient_nz = (t->cam.orient.x | t->cam.orient.y | t->cam.orient.z) != 0;
    return (dx | dy | dz) != 0 && orient_nz;
}

static void seg_center(const HZD_SEG *s, int *cx, int *cy, int *cz)
{
    *cx = (s->p1.x + s->p2.x) / 2;
    *cy = (s->p1.y + s->p2.y) / 2;
    *cz = (s->p1.z + s->p2.z) / 2;
}

static void aabb_center(const HZD_VEC *b1, const HZD_VEC *b2,
                        int *cx, int *cy, int *cz)
{
    *cx = (b1->x + b2->x) / 2;
    *cy = (b1->y + b2->y) / 2;
    *cz = (b1->z + b2->z) / 2;
}

int ed_hzd_count_walls(void)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int n = 0;
    for (int gi = 0; gi < m->n_groups; gi++) n += m->groups[gi].n_walls;
    return n;
}

int ed_hzd_count_floors(void)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int n = 0;
    for (int gi = 0; gi < m->n_groups; gi++) n += m->groups[gi].n_floors;
    return n;
}

int ed_hzd_count_traps(void)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int n = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        for (int i = 0; i < m->groups[gi].n_triggers; i++)
            if (!trap_looks_like_camera(&m->groups[gi].triggers[i])) n++;
    }
    return n;
}

int ed_hzd_count_cameras(void)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int n = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        for (int i = 0; i < m->groups[gi].n_triggers; i++)
            if (trap_looks_like_camera(&m->groups[gi].triggers[i])) n++;
    }
    return n;
}

int ed_hzd_count_routes(void)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    return m ? m->n_routes : 0;
}

int ed_hzd_get_wall(int idx, EdHzdItem *out)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int seen = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];
        if (idx < seen + g->n_walls) {
            HZD_SEG *s = &g->walls[idx - seen];
            seg_center(s, &out->cx, &out->cy, &out->cz);
            snprintf(out->tag, sizeof(out->tag), "wall %d (g%d)", idx, gi);
            return 1;
        }
        seen += g->n_walls;
    }
    return 0;
}

int ed_hzd_get_floor(int idx, EdHzdItem *out)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int seen = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];
        if (idx < seen + g->n_floors) {
            HZD_FLR *f = &g->floors[idx - seen];
            out->cx = (f->p1.x + f->p3.x) / 2;
            out->cy = (f->p1.y + f->p3.y) / 2;
            out->cz = (f->p1.z + f->p3.z) / 2;
            snprintf(out->tag, sizeof(out->tag), "floor %d (g%d)", idx, gi);
            return 1;
        }
        seen += g->n_floors;
    }
    return 0;
}

int ed_hzd_get_trap(int idx, EdHzdItem *out)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int seen = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];
        for (int i = 0; i < g->n_triggers; i++) {
            HZD_TRG *t = &g->triggers[i];
            if (trap_looks_like_camera(t)) continue;
            if (seen == idx) {
                aabb_center(&t->trap.b1, &t->trap.b2,
                            &out->cx, &out->cy, &out->cz);
                int n = 0;
                while (n < 12 && t->trap.name[n] && n < (int)sizeof(out->tag) - 1) {
                    out->tag[n] = t->trap.name[n];
                    n++;
                }
                out->tag[n] = 0;
                if (!n) snprintf(out->tag, sizeof(out->tag), "trap %d", idx);
                return 1;
            }
            seen++;
        }
    }
    return 0;
}

int ed_hzd_get_camera(int idx, EdHzdItem *out)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int seen = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];
        for (int i = 0; i < g->n_triggers; i++) {
            HZD_TRG *t = &g->triggers[i];
            if (!trap_looks_like_camera(t)) continue;
            if (seen == idx) {
                out->cx = t->cam.cam.x;
                out->cy = t->cam.cam.y;
                out->cz = t->cam.cam.z;
                snprintf(out->tag, sizeof(out->tag), "cam %d (g%d)", idx, gi);
                return 1;
            }
            seen++;
        }
    }
    return 0;
}

int ed_hzd_get_route(int idx, EdHzdItem *out)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m || idx < 0 || idx >= m->n_routes) return 0;
    HZD_PAT *r = &m->routes[idx];
    if (!r->points || r->n_points < 1) return 0;
    out->cx = r->points[0].x;
    out->cy = r->points[0].y;
    out->cz = r->points[0].z;
    snprintf(out->tag, sizeof(out->tag), "route %d (%dpts)", idx, r->n_points);
    return 1;
}

/* CPU ray vs trap/camera AABB picking. Returns 1 if a trap hit, 2 for a
   camera hit, 0 otherwise. Sets the matching g_sel_* index so the
   wireframe highlights the picked one. */
static int ray_aabb(const float ro[3], const float rd[3],
                    int x0, int y0, int z0, int x1, int y1, int z1,
                    float *out_t)
{
    float tmin = -1e30f, tmax = 1e30f;
    int axes[3][2] = { {x0, x1}, {y0, y1}, {z0, z1} };
    for (int k = 0; k < 3; k++) {
        float d = rd[k];
        if (d > -1e-6f && d < 1e-6f) {
            if (ro[k] < (float)axes[k][0] || ro[k] > (float)axes[k][1])
                return 0;
        } else {
            float t1 = ((float)axes[k][0] - ro[k]) / d;
            float t2 = ((float)axes[k][1] - ro[k]) / d;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return 0;
        }
    }
    if (tmin < 0) return 0;
    *out_t = tmin;
    return 1;
}

int ed_hzd_pick_ray(const float ro[3], const float rd[3])
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int   best_kind = 0;
    int   best_idx  = -1;
    float best_t    = 1e30f;
    int trap_idx = 0, cam_idx = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];
        if (!g->triggers) continue;
        for (int i = 0; i < g->n_triggers; i++) {
            HZD_TRG *t = &g->triggers[i];
            int is_cam = trap_looks_like_camera(t);
            HZD_VEC *b1 = is_cam ? &t->cam.b1  : &t->trap.b1;
            HZD_VEC *b2 = is_cam ? &t->cam.b2  : &t->trap.b2;
            int x0 = b1->x, y0 = b1->y, z0 = b1->z;
            int x1 = b2->x, y1 = b2->y, z1 = b2->z;
            if (x0 > x1) { int s = x0; x0 = x1; x1 = s; }
            if (y0 > y1) { int s = y0; y0 = y1; y1 = s; }
            if (z0 > z1) { int s = z0; z0 = z1; z1 = s; }
            float t_hit;
            if (ray_aabb(ro, rd, x0,y0,z0, x1,y1,z1, &t_hit) && t_hit < best_t) {
                best_t   = t_hit;
                best_idx = is_cam ? cam_idx : trap_idx;
                best_kind = is_cam ? 2 : 1;
            }
            if (is_cam) cam_idx++; else trap_idx++;
        }
    }
    if (best_kind == 1) {
        g_sel_trap = best_idx;
        return 1;
    }
    if (best_kind == 2) {
        g_sel_cam = best_idx;
        return 2;
    }
    return 0;
}

int ed_hzd_collect_trap_labels(EdHzdLabel *out, int max)
{
    HZD_DEF *m = (HZD_DEF *)g_stage.hzd_map;
    if (!m) return 0;
    int n = 0;
    int trap_idx = 0;
    for (int gi = 0; gi < m->n_groups; gi++) {
        HZD_GRP *g = &m->groups[gi];
        for (int i = 0; i < g->n_triggers; i++) {
            HZD_TRG *t = &g->triggers[i];
            if (trap_looks_like_camera(t)) continue;
            int cx, cy, cz;
            aabb_center(&t->trap.b1, &t->trap.b2, &cx, &cy, &cz);
            float sx, sy;
            if (ed_world_to_screen(cx, cy, cz, &sx, &sy)) {
                if (n < max) {
                    EdHzdLabel *L = &out[n++];
                    int k = 0;
                    while (k < 12 && t->trap.name[k] && k < (int)sizeof(L->name) - 1) {
                        L->name[k] = t->trap.name[k];
                        k++;
                    }
                    L->name[k] = 0;
                    if (!k) snprintf(L->name, sizeof(L->name), "trap%d", trap_idx);
                    L->sx = sx;
                    L->sy = sy;
                    L->group = gi;
                    L->index = trap_idx;
                }
            }
            trap_idx++;
        }
    }
    return n;
}
