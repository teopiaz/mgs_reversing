#ifndef MGS_EDITOR_H
#define MGS_EDITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loaded stage assets (single global — only s01a for v1). */
typedef struct {
    void *map_def;       /* DG_DEF * for the stage's main KMD */
    int   map_def_id;    /* GV cache ID, low 16 = name hash */
    void *hzd_map;       /* HZD_MAP * */
    int   hzd_id;
    char  stage_name[16];
    int   loaded;
} EditorStage;

extern EditorStage g_stage;

/* ed_loader.c */
int  ed_load_stage(const char *stage_name);

/* ed_camera.c */
typedef struct {
    float pos[3];        /* world-space, +Y down (PSX convention) */
    float yaw;           /* radians, around Y */
    float pitch;
    float fov_scale;     /* multiplied into PSX H register; default 1.0 */
} EdCamera;

extern EdCamera g_cam;

void ed_camera_default(void);
void ed_camera_update(float dt, int mouse_dx, int mouse_dy, int rmb_held);

/* ed_render.c */
void ed_render_frame(void);

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
    uint32_t color;          /* derived RGB for marker (0xRRGGBB00) */
} EdActor;

extern EdActor *g_actors;
extern int      g_actor_count;
extern int      g_actor_selected; /* -1 if none */

void ed_actors_load(const char *json_path);
void ed_actors_render(void);
int  ed_actors_pick_ray(const float ray_origin[3], const float ray_dir[3]);

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

#ifdef __cplusplus
}
#endif

#endif
