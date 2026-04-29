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
    int   n_rots;           /* per-joint Euler triplets — bone hierarchy size */
    short *rots;            /* [n_rots * 3] heap-owned (rx,ry,rz)... */
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
extern int              g_dmo_show_models;  /* prefer KMDs over cube markers when in cache */
extern int              g_dmo_follow_cam;   /* per-frame, snap editor cam to eye + look-at */
extern int              g_dmo_loop;         /* during Play: wrap to frame 0 instead of holding */

void ed_dmo_load_index(void);
int  ed_dmo_open(const char *name);
void ed_dmo_close(void);

/* ---- Authoring (keyframed timeline → bake → save .dmo) ----------------- */

/* Keyframe-based cinematic editor. The user places sparse camera keys
 * along a timeline and (optionally) per-DEMODOLL position keys; on save,
 * we densify by linear-interpolating between adjacent keys to produce
 * one DMO_DAT record per frame. */
typedef struct {
    int   frame;              /* 0..timeline.n_frames-1 */
    short eye[3];             /* PSX world units */
    short center[3];
    short roll;               /* 0 most cinematics; PSX 4096 = 360° */
    short clip;               /* PSX H register; ~200 default */
} EdDmoCamKey;

typedef struct {
    int   frame;
    short pos[3];
    short rot[3];             /* root rotation; per-bone rots not authored */
    short visible;            /* 1 by default; gate cinematic visibility */
} EdDmoDollKey;

#define ED_DMO_MAX_DOLLS 8
#define ED_DMO_MAX_KEYS  256

typedef struct {
    char  label[24];          /* user-facing name (e.g. "Snake") */
    int   type;               /* DMO_MDL.type — sequential 1..N at save */
    int   cache_id;           /* KMD id for editor preview; 0 = none */
    int   n_keys;
    EdDmoDollKey keys[ED_DMO_MAX_KEYS];
} EdDmoTrack;

typedef struct {
    int   active;             /* 1 once ed_dmo_timeline_new succeeded */
    char  name[32];           /* output filename (no extension) */
    int   n_frames;           /* total cinematic length in ticks */
    int   cam_n_keys;
    EdDmoCamKey cam_keys[ED_DMO_MAX_KEYS];
    int   n_dolls;
    EdDmoTrack dolls[ED_DMO_MAX_DOLLS];
} EdDmoTimeline;

extern EdDmoTimeline g_dmo_timeline;
/* Scrubber position — what frame the timeline editor is showing. -1 if
 * the timeline isn't active. */
extern int g_dmo_timeline_frame;
/* Selection: -1 = nothing, -2 = camera track header, 0..7 = doll track,
 * with `g_dmo_timeline_sel_key` indexing into that track's keys. */
extern int g_dmo_timeline_sel_track;
extern int g_dmo_timeline_sel_key;

/* Lifecycle */
void ed_dmo_timeline_new(const char *name, int n_frames);
void ed_dmo_timeline_close(void);

/* Camera-track ops */
int  ed_dmo_timeline_add_cam_key(int frame);
int  ed_dmo_timeline_remove_cam_key(int idx);
/* Snap the selected camera key's eye/center to match the editor's
 * current free-fly camera. No-op if no key is selected. */
void ed_dmo_timeline_snap_cam_to_editor(void);
/* Linear-interp the camera at `frame`. Falls back to the first/last key
 * outside the keyed range. Sets all four fields of `out`. */
void ed_dmo_timeline_eval_cam(int frame, EdDmoCamKey *out);

/* Doll-track ops */
int  ed_dmo_timeline_add_doll(const char *label, int type, int cache_id);
int  ed_dmo_timeline_remove_doll(int doll_idx);
int  ed_dmo_timeline_add_doll_key(int doll_idx, int frame);
int  ed_dmo_timeline_remove_doll_key(int doll_idx, int key_idx);
/* Snap the selected doll key's pos to the editor camera's eye position
 * (so the user can fly to where they want and click "place"). */
void ed_dmo_timeline_snap_doll_to_editor(void);
void ed_dmo_timeline_eval_doll(int doll_idx, int frame, EdDmoDollKey *out);

/* Bake the timeline into data/dmo/custom/<name>.dmo. Returns 1 on success. */
int  ed_dmo_timeline_save(void);

/* Live preview: when timeline is active, ed_dmo_render_timeline_actors
 * draws the dolls at their interpolated pos for the current scrubber
 * frame, and ed_dmo_render_timeline_path draws the cam path. */
void ed_dmo_render_timeline_path(void);
void ed_dmo_render_timeline_actors(void);

/* Load a custom .dmo from disk into the inspector. `path` is relative to
 * the editor's working dir. Returns 1 on success. */
int  ed_dmo_open_file(const char *path);
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
