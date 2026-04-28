#ifndef MGS_EDITOR_DMO_H
#define MGS_EDITOR_DMO_H

#ifdef __cplusplus
extern "C" {
#endif

/* DMO Inspector — see ed_dmo.c for the full description.
 *
 * The data here is *offline* — sourced from JSON pre-extracted by
 * tools/extract_dmo.py. There is no live engine state. Values mirror the
 * runtime DMO_DEF / DMO_DAT structs (source/include/fmt_dmo.h) but with
 * pointer fields replaced by parallel-array indices we never use. */

typedef struct {
    char name[32];          /* e.g. "s0102a0.dmo" */
    int  sector;            /* sector offset within DEMO.DAT */
    char gcl_path[64];      /* relative path of the .gcl that referenced it */
    int  n_frames;          /* DMO_DEF.n_frames (cutscene length in ticks) */
    int  n_extracted;       /* how many DMO_DAT records the extractor saw */
    int  n_models;          /* DMO_DEF.n_models */
    int  n_maps;            /* DMO_DEF.n_maps */
} EdDmoIndexEntry;

typedef struct {
    int   type;             /* matches DMO_MDL.type — index into header.models */
    short visible;
    short pos[3];           /* PSX world units */
    short rot[3];           /* root rotation, pre-skeletal */
} EdDmoAdjust;

typedef struct {
    int frame;              /* frame index from the runtime DMO_DAT */
    short eye[3];           /* PSX world units */
    short center[3];        /* lookat target */
    short roll;
    short clip_dist;        /* PSX H register (FOV) */
    short n_charas;         /* DMO_CHA spawn/despawn events this frame */
    short n_adjusts;        /* DMO_ADJ per-character poses this frame */
    EdDmoAdjust *adjusts;   /* [n_adjusts] — heap-owned, freed on close */
} EdDmoFrame;

typedef struct {
    int type;
    int flag;
    int cache_id;
    int filename;
    int name;
} EdDmoModel;

typedef struct {
    int cache_id;
    int filename;
} EdDmoMap;

typedef struct {
    char        name[32];
    int         sector;
    int         n_frames;       /* from header */
    int         n_extracted;    /* actual frames[] length — usually == n_frames */
    int         n_models;
    int         n_maps;
    EdDmoFrame *frames;         /* [n_extracted] */
    EdDmoModel *models;         /* [n_models] */
    EdDmoMap   *maps;           /* [n_maps] */
} EdDmoData;

extern EdDmoIndexEntry *g_dmo_index;
extern int              g_dmo_index_count;
extern int              g_dmo_index_loaded;
extern EdDmoData       *g_dmo_active;       /* NULL = nothing selected */
extern int              g_dmo_active_frame; /* index into g_dmo_active->frames */
extern int              g_dmo_show_path;    /* draw camera path in 3D pane */
extern int              g_dmo_show_actors;  /* draw per-frame DMO_ADJ markers */
extern int              g_dmo_follow_cam;   /* per-frame, snap editor cam to eye + look-at */

void ed_dmo_load_index(void);
int  ed_dmo_open(const char *name);
void ed_dmo_close(void);
/* Look up dmos referenced by `decompiled/<stage>/{demo,scenerio}.gcl`.
 * Fills out_names[0..max-1] with at most `max` dmo filenames; returns
 * the total found (may exceed `max`). Order follows the catalogue's
 * sector ordering — i.e., chronological play order for stages that
 * chain multiple `.dmo`s. */
int  ed_dmo_find_for_stage(const char *stage_name, int max,
                           char (*out_names)[32]);
/* Submit camera-path lines for the current dmo. No-op when nothing is
 * loaded or g_dmo_show_path is off. Calls into gl_submit_line3d using
 * whatever eye_inv / clip_dist the caller has already bound. */
void ed_dmo_render_path(void);
/* Submit per-character markers for the active frame. Type-colored cubes
 * at each visible DMO_ADJ.pos. No-op when no dmo / off / invalid frame. */
void ed_dmo_render_actors(void);

#ifdef __cplusplus
}
#endif

#endif
