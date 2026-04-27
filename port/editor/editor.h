#ifndef MGS_EDITOR_H
#define MGS_EDITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loaded stage assets. Stages can be a single map KMD (s01a) or several
   room KMDs (s02a — tank hangar has 7). We collect every "map-sized" KMD
   and render them all at world origin; actor/object KMDs (small bbox) are
   ignored since the editor doesn't instantiate them. */
#define EDITOR_MAX_MAP_KMDS 32

typedef struct {
    void *map_defs[EDITOR_MAX_MAP_KMDS];
    int   map_ids[EDITOR_MAX_MAP_KMDS];
    int   n_map_defs;
    void *hzd_map;       /* HZD_MAP * */
    int   hzd_id;
    char  stage_name[16];
    int   loaded;
} EditorStage;

extern EditorStage g_stage;

/* ed_loader.c */
int  ed_load_stage(const char *stage_name);
int  ed_stage_count(void);
const char *ed_stage_name(int idx);

/* ed_camera.c */
#define ED_CAM_MODE_FLY    0
#define ED_CAM_MODE_ORBIT  1

typedef struct {
    float pos[3];          /* world-space, +Y down (PSX convention) */
    float yaw;             /* radians, around Y */
    float pitch;
    float fov_scale;       /* multiplied into PSX H register; default 1.0 */
    int   mode;            /* ED_CAM_MODE_FLY | ED_CAM_MODE_ORBIT */
    float orbit_target[3]; /* world-space pivot for orbit mode */
    float orbit_dist;      /* distance from pos to orbit_target, world units */
} EdCamera;

extern EdCamera g_cam;

void ed_camera_default(void);
void ed_camera_first_person(void);
void ed_camera_update(float dt, int mouse_dx, int mouse_dy, int rmb_held);
/* Move the camera so the given world point sits `distance` units ahead of
   it along the current forward axis. Yaw/pitch are preserved — the actor
   gets centered in view from whatever angle the user is currently using. */
void ed_camera_focus(int wx, int wy, int wz, float distance);

/* Phase-3 camera helpers. Orbit/fly toggle picks an orbit_target one
 * `orbit_dist` step ahead of the current position; fly→orbit doesn't move
 * the camera. Orbit mouse-rotation rotates the camera around orbit_target.
 * Zoom: fly = dolly along view direction, orbit = scale orbit_dist. */
void ed_camera_set_mode(int mode);
void ed_camera_orbit(int mouse_dx, int mouse_dy);
void ed_camera_zoom(float wheel_steps);

/* Reframe the camera so the given world AABB fits comfortably in view.
 * Fly mode: yaw/pitch preserved, pos shifts along forward; orbit mode:
 * orbit_target = AABB center, orbit_dist scales to fit the diagonal. */
void ed_camera_frame_aabb(const float bmin[3], const float bmax[3]);

/* AABB sources for the F-key. Returns 1 if a sensible AABB was filled,
 * 0 if no geometry/selection is loaded. ed_compute_active_aabb prefers
 * a current selection (actor / HZD trap / camera) and falls back to
 * the union of stage map KMD bboxes. */
int  ed_compute_selection_aabb(float bmin[3], float bmax[3]);
int  ed_compute_stage_aabb(float bmin[3], float bmax[3]);
int  ed_compute_active_aabb(float bmin[3], float bmax[3]);

/* ---- Diagnostic stats (Info tab) ---------------------------------------- */
typedef struct {
    const char *name;     /* "PACKET0" / "PACKET1" / "NORMAL" */
    int total;            /* heap end - heap start */
    int used;             /* USED + VOIDED bytes */
    int freed;            /* FREE bytes (named "freed" — `free` collides with libc) */
    int max_free_block;   /* largest contiguous free run */
    int n_units;          /* alloc-table entries in use */
} EdHeapStat;
typedef struct {
    EdHeapStat heap[3];   /* PACKET0, PACKET1, NORMAL */
    int total;            /* sum of heap[].total */
    int used;             /* sum of heap[].used */
    int freed;            /* sum of heap[].freed */
} EdMemStats;

typedef struct {
    int n_textures;       /* DG_TEX records with .used != 0 */
    int n_4bpp, n_8bpp, n_16bpp;
    int vram_pixels;      /* sum of (w+1)*(h+1) over all active textures */
    int vram_bytes;       /* same, weighted by bpp (4/8/16) */
} EdTexStats;

typedef struct {
    int total;            /* g_actor_count */
    int with_pos;
    int with_model;       /* kmd_def resolved */
    int with_rotation;    /* rot_b >= 0 */
    int from_demo;
    int from_scen;
} EdActorStats;

/* Scene-geometry stats — total faces / verts across all map KMDs. */
typedef struct {
    int n_kmds;
    int n_models;         /* sum of DG_DEF.n_models */
    int n_faces;
    int n_vertices;
} EdGeomStats;

void ed_collect_memory_stats(EdMemStats *out);
void ed_collect_texture_stats(EdTexStats *out);
void ed_collect_actor_stats(EdActorStats *out);
void ed_collect_geom_stats(EdGeomStats *out);

/* Hammer-style ortho camera (one per Top/Front/Side pane). The camera
 * looks along a fixed world axis; `center` pans within the locked plane
 * and `half_size` sets the orthographic half-width (zoom — smaller =
 * more zoomed in). The eye_inv matrix needed by the GL renderer is
 * built per-frame by ed_camera_build_ortho_eye_inv. */
typedef enum { ED_ORTHO_TOP = 0, ED_ORTHO_FRONT, ED_ORTHO_SIDE } EdOrthoAxis;
typedef struct {
    float center[3];     /* world-space focus point */
    float half_size;     /* world units from center to viewport edge (the smaller dim) */
    EdOrthoAxis axis;
} EdOrthoCam;

extern EdOrthoCam g_top_cam;    /* looks down +Y (PSX) → world XZ plane visible */
extern EdOrthoCam g_front_cam;  /* looks down +Z → world XY plane */
extern EdOrthoCam g_side_cam;   /* looks down +X → world YZ plane */

void ed_camera_ortho_default(EdOrthoCam *cam, EdOrthoAxis axis);
void ed_camera_ortho_pan(EdOrthoCam *cam, int viewport_w, int viewport_h,
                         int pixel_dx, int pixel_dy);
void ed_camera_ortho_zoom(EdOrthoCam *cam, float wheel_steps);
/* Compute eye-space (world * eye_inv / 4096 + t) bounds + matrix for an ortho cam. */
void ed_camera_ortho_compute(EdOrthoCam *cam, int viewport_w, int viewport_h,
                             float *out_lrbt /*[4] in eye coords*/);
/* Reframe an ortho cam to fit the AABB in its plane (Top: XZ, Front: XY,
 * Side: YZ) with a small margin. Center moves to the AABB midpoint. */
void ed_camera_ortho_frame_aabb(EdOrthoCam *cam,
                                const float bmin[3], const float bmax[3]);

/* True iff the dockable "3D View" panel is hovered or focused (set by ed_ui). */
int  ed_ui_3dview_active(void);
/* Index of the ortho viewport hovered (1..3) or 0 if none. */
int  ed_ui_ortho_active(void);

/* ed_render.c — full multi-viewport draw. Renders 3D pane (slot 0)
 * plus any of the ortho panes (slots 1..3) whose flag is enabled. */
void ed_render_frame(void);
/* Renders the same scene with an ortho camera; caller must bind the
 * destination viewport FBO and enable ortho/wireframe in the GL renderer. */
void ed_render_frame_ortho(EdOrthoCam *cam, int viewport_w, int viewport_h);
/* Demo playback: uses the engine's runtime DG_Chanls[1].eye_inv as the
 * camera and runs the engine's port_RenderObjects pipeline so animated
 * actors appear. Editor map/HZD overlays render on top. */
void ed_render_frame_demo(void);

/* ed_hzd.c */
void ed_hzd_render(void);

/* ed_actors.c */
typedef struct {
    char     type[32];      /* e.g. "WATCHER" */
    char     instance[16];  /* e.g. "$s:1465" */
    int      pos[3];        /* x, y, z in PSX units */
    int      has_pos;
    int      proc_line;
    char     proc[32];      /* e.g. "sub_7ECB" */
    uint32_t color;         /* derived RGB for marker (0xRRGGBB00) */
    int      rot_b;         /* "b:N" rotation, 0..255 = 0..360°. -1 if absent. */
    int      from_demo;     /* 1 if entry came from demo.gcl, 0 if scenerio. */
    void    *kmd_def;       /* DG_DEF * resolved at load. NULL = no model. */
    /* Gameplay options harvested from data/<stage>_actors.json. -1 / empty
     * when the option wasn't present on the chara line. Loaded only via
     * the JSON path (TSV fallback leaves them all as -1 / empty). */
    int      item_id;       /* ITEM `-i b:N` (linkvar.h IT_*) */
    int      item_count;    /* ITEM `-n N` (count picked up) */
    int      item_height;   /* ITEM `-h N` */
    int      box_type;      /* ITEM `-b b:N` → KMD_BOX_01+N; -1 if absent */
    int      vision_range;  /* WATCHER/COMMANDER `-l N` cone length */
    char     behavior;      /* WATCHER `-b 'X'` single char (P/Z/S/...) */
    char     alertness;     /* WATCHER `-a 'X'` single char */
    int      flags_n;       /* `-f N` bitmask */
    char     event_proc[32];/* TRAP / others `-e sub_XXXX` event handler */
    char     init_proc[32]; /* `-y sub_XXXX` init handler */
    char     message[32];   /* ITEM `-m` friendly name (e.g. "RATION") */
} EdActor;

extern EdActor *g_actors;
extern int      g_actor_count;
extern int      g_actor_selected; /* -1 if none */

void ed_actors_load(const char *json_path);
int  ed_actors_save(const char *tsv_path);    /* rewrites TSV; 0 on success */
void ed_actors_render(void);
int  ed_actors_pick_ray(const float ray_origin[3], const float ray_dir[3]);

/* Tracks unsaved edits to the in-memory actor list. UI shows a "*" marker
   when set; cleared on save and on stage reload. */
extern int g_actors_dirty;

/* ed_ui.cpp */
void ed_ui_init(void *sdl_window);
void ed_ui_shutdown(void);
void ed_ui_process_event(void *sdl_event);
void ed_ui_new_frame(void);
void ed_ui_draw(void);
void ed_ui_render(void);
int  ed_ui_wants_mouse(void);
int  ed_ui_wants_keyboard(void);

/* ed_render.c — exposed so the inspector can highlight selected items */
extern int g_show_walls;
extern int g_show_floors;
extern int g_show_traps;
extern int g_show_cameras;
extern int g_show_zones;
extern int g_show_routes;
extern int g_show_actors;

/* HZD selection hints (-1 = none). */
extern int g_sel_wall, g_sel_floor, g_sel_trap, g_sel_cam, g_sel_zone, g_sel_route;

/* World-axes gizmo + render-actor-KMD toggles. */
extern int g_show_axes;
extern int g_show_actor_models;
extern int g_show_actor_rotations;
extern int g_show_trap_labels;
extern int g_show_vision_cones;

/* Render-side helpers for UI overlays and picking. Each works against the
   per-frame eye_inv captured by ed_render_frame. */
int  ed_world_to_screen(int wx, int wy, int wz, float *sx_norm, float *sy_norm);
void ed_screen_to_world_ray(float sx_norm, float sy_norm, float origin[3], float dir[3]);

/* Walk every spatial trap (HZD_TRP) and emit projected screen-space labels
   so ed_ui can draw the names with ImGui. Returns number of labels filled.
   `sx_norm`/`sy_norm` are 0..1 normalized; ed_ui scales to display size. */
typedef struct {
    char  name[16];
    float sx, sy;
    int   group, index;
} EdHzdLabel;
int  ed_hzd_collect_trap_labels(EdHzdLabel *out, int max);

/* HZD list helpers — for the inspector tables. Returns the count + center
   point of the i-th item so the UI can focus the camera on click. */
typedef struct {
    int  cx, cy, cz;
    char tag[20];   /* short human label */
} EdHzdItem;
int  ed_hzd_count_walls(void);
int  ed_hzd_count_floors(void);
int  ed_hzd_count_traps(void);
int  ed_hzd_count_cameras(void);
int  ed_hzd_count_routes(void);
int  ed_hzd_get_wall(int idx,   EdHzdItem *out);
int  ed_hzd_get_floor(int idx,  EdHzdItem *out);
int  ed_hzd_get_trap(int idx,   EdHzdItem *out);
int  ed_hzd_get_camera(int idx, EdHzdItem *out);
int  ed_hzd_get_route(int idx,  EdHzdItem *out);

/* ------ Demo (cutscene) player ------------------------------------------ */

/* Three-state transport. The engine actor system ticks when state is PLAY;
 * frame counter advances accordingly. STOP fully resets engine state by
 * reloading the stage; PAUSE just freezes the tick loop. */
typedef enum { ED_DEMO_STOPPED = 0, ED_DEMO_PLAYING, ED_DEMO_PAUSED } EdDemoState;

extern EdDemoState g_demo_state;
extern int         g_demo_frame;        /* monotonic tick counter while playing */
extern int         g_demo_loaded;       /* 1 if demo.gcx was found + handed to GCL */

/* Snapshot of the engine's runtime camera (DG_Chanls[1].eye_inv) decomposed
 * into world pos + Euler rotation. Updated each frame the engine ticks.
 * Pos in PSX world units; rot in 4096-fixed units (PSX convention). */
extern float       g_demo_cam_pos[3];
extern float       g_demo_cam_rot_yaw;
extern float       g_demo_cam_rot_pitch;
extern float       g_demo_cam_rot_roll;

/* Transport control. Idempotent: calling Play while already playing is a
 * no-op; Stop while stopped does nothing. Implemented in ed_demo.c. */
void ed_demo_play(void);
void ed_demo_pause(void);
void ed_demo_stop(void);
void ed_demo_step_one(void);            /* tick the engine once + freeze */
/* Per-editor-frame hook: when state is PLAY, advances the engine + camera
 * snapshot. Cheap when stopped or paused. */
void ed_demo_tick(void);

/* Live diagnostics — populated by ed_demo_tick. Exposed to the Demo tab so
 * the user can verify whether actors are actually executing and which
 * DG_Chanls channel the runtime camera is writing. */
extern int g_demo_diag_actor_count;     /* live actor count after last tick */
extern int g_demo_diag_chanl_dirty;     /* bitmask: channels whose eye_inv changed last tick */
extern int g_demo_diag_gv_clock;        /* GV_Clock as observed last tick */
extern int g_demo_active_chanl;         /* 0/1/2 — channel the demo is driving */
/* Print every active actor (level, name, runtime fraction) to stdout. */
void ed_demo_dump_actors(void);

/* Stage metadata — read by the inspector "Stage" tab. */
typedef struct {
    char ext;            /* k, h, p, b, ... */
    int  id;             /* low 16 = name hash */
    int  approx_size;    /* -1 if unknown */
} EdStageEntry;
int  ed_stage_collect_entries(EdStageEntry *out, int max);

/* Actor → KMD lookup. If a model is found, returns non-zero and writes a
   pointer to its DG_DEF in *out_def. Otherwise the cube marker is used. */
int  ed_actor_kmd_for_type(const char *type, void **out_def);

/* GM_Items[] catalog (linkvar.h IT_*). Returns a short lowercase name for
 * the given item id, or "?" if out of range. Used for ITEM marker labels
 * and the items inspector. */
const char *ed_item_name(int item_id);

/* RGB tint suggested for an item id (0xRRGGBB). Picks the family color
 * (consumables green, equipment blue, key items yellow, ...). */
uint32_t    ed_item_color(int item_id);

/* World-space ray vs HZD trap / camera AABBs. On hit, sets the matching
   g_sel_* index, returns 1 (kind: 1=trap, 2=camera). 0 if no hit. */
int  ed_hzd_pick_ray(const float ray_origin[3], const float ray_dir[3]);

#ifdef __cplusplus
}
#endif

#endif
