/* DMO Inspector — loads pre-extracted .dmo metadata for offline inspection.
 *
 * `.dmo` data is the streamed-cinematic timeline that lives baked inside
 * DEMO.DAT, addressed by sector (e.g. `demo -f "s0102a0.dmo" -s t:1441`).
 * Running it requires the FS streamer to pump bytes per frame, which the
 * editor doesn't do (see port/doc/demo/09-streamed-demos.md). Until that
 * lands, this inspector lets the user see what each .dmo *would* show:
 * frame count, model/map references, per-frame camera path, character
 * spawn / pose counts.
 *
 * Data flow: tools/extract_dmo.py walks DEMO.DAT for every `demo -s`
 * sector found in port/gcl/decompiled/, parses the DMO_DEF + DMO_DAT
 * chain, and emits one JSON per .dmo at port/editor/data/dmo/. We load
 * `_index.json` lazily on first request, then a single dmo's JSON
 * on-demand as the user clicks through the list.
 *
 * Memory: the largest dmo (s0101a0) has ~3500 frames and decompresses
 * to ~5MB JSON; we keep one EdDmoData live at a time. The opening
 * cinematic dwarfs every other dmo, so the working set is bounded by
 * that single file. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "libgte.h"
#include "libgpu.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "editor.h"
#include "ed_dmo.h"

/* ---------------------------------------------------------------------- */
/* JSON reader — same JR pattern as ed_actors.c; copied to keep the two
 * modules independent. The function names are prefixed `dj_` to avoid
 * future collisions if both parsers ever live in the same translation
 * unit. */
typedef struct {
    const char *p;
    const char *end;
    int err;
} JR;

static void dj_ws(JR *r) {
    while (r->p < r->end) {
        char c = *r->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') r->p++;
        else break;
    }
}
static int dj_peek(JR *r)        { dj_ws(r); return r->p < r->end ? *r->p : -1; }
static int dj_eat(JR *r, char c) { dj_ws(r); if (r->p < r->end && *r->p == c) { r->p++; return 1; } return 0; }

static int dj_str(JR *r, char *out, int outlen) {
    dj_ws(r);
    if (r->p >= r->end || *r->p != '"') return 0;
    r->p++;
    int n = 0;
    while (r->p < r->end && *r->p != '"') {
        char c = *r->p++;
        if (c == '\\' && r->p < r->end) {
            char e = *r->p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                default: c = e; break;
            }
        }
        if (n + 1 < outlen) out[n++] = c;
    }
    if (n < outlen) out[n] = 0; else if (outlen > 0) out[outlen - 1] = 0;
    if (r->p >= r->end || *r->p != '"') { r->err = 1; return 0; }
    r->p++;
    return 1;
}

static int dj_long(JR *r, long *out) {
    dj_ws(r);
    char buf[32];
    int n = 0;
    if (r->p < r->end && (*r->p == '-' || *r->p == '+')) buf[n++] = *r->p++;
    while (r->p < r->end && *r->p >= '0' && *r->p <= '9' && n < (int)sizeof(buf) - 1)
        buf[n++] = *r->p++;
    if (!n) { r->err = 1; return 0; }
    buf[n] = 0;
    *out = strtol(buf, NULL, 10);
    return 1;
}

static void dj_skip(JR *r);
static void dj_skip(JR *r) {
    int c = dj_peek(r);
    if (c == -1) { r->err = 1; return; }
    if (c == '"') { char tmp[4]; dj_str(r, tmp, sizeof(tmp)); return; }
    if (c == '{') {
        r->p++;
        if (dj_eat(r, '}')) return;
        do {
            char k[32];
            if (!dj_str(r, k, sizeof(k))) { r->err = 1; return; }
            if (!dj_eat(r, ':')) { r->err = 1; return; }
            dj_skip(r);
        } while (dj_eat(r, ','));
        if (!dj_eat(r, '}')) r->err = 1;
        return;
    }
    if (c == '[') {
        r->p++;
        if (dj_eat(r, ']')) return;
        do { dj_skip(r); } while (dj_eat(r, ','));
        if (!dj_eat(r, ']')) r->err = 1;
        return;
    }
    /* literal: number / true / false / null — eat alphanum + ./+/- */
    while (r->p < r->end) {
        char c2 = *r->p;
        if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'z') ||
            c2 == '.' || c2 == '-' || c2 == '+' || c2 == 'E' || c2 == 'e')
            r->p++;
        else break;
    }
}

/* ---------------------------------------------------------------------- */

EdDmoIndexEntry *g_dmo_index       = NULL;
int              g_dmo_index_count = 0;
int              g_dmo_index_loaded = 0;
EdDmoData       *g_dmo_active      = NULL;
int              g_dmo_active_frame = 0;
int              g_dmo_show_path = 1;
int              g_dmo_show_actors = 1;
int              g_dmo_show_models = 1;
int              g_dmo_follow_cam = 0;
int              g_dmo_loop = 0;

/* Read a whole file into a heap buffer. Returns NULL on failure;
 * sets *out_size when it doesn't. Caller frees with `free`. */
static char *slurp(const char *path, long *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return NULL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    long rd = (long)fread(buf, 1, sz, fp);
    fclose(fp);
    buf[rd] = 0;
    if (out_size) *out_size = rd;
    return buf;
}

/* ---------------------------------------------------------------------- */
/* Load _index.json — minimal array-of-objects, keys: name, sector,
 * gcl_path, n_frames, n_extracted, n_models, n_maps. */
void ed_dmo_load_index(void) {
    if (g_dmo_index_loaded) return;
    g_dmo_index_loaded = 1;  /* attempt once even if missing — don't retry */

    long sz = 0;
    char *buf = slurp("data/dmo/_index.json", &sz);
    if (!buf) {
        printf("[dmo] data/dmo/_index.json not found — run "
               "`python3 tools/extract_dmo.py --all` from the repo root.\n");
        return;
    }
    JR r = { buf, buf + sz, 0 };

    if (!dj_eat(&r, '[')) { free(buf); return; }
    /* Two passes: count first, then fill. */
    JR scan = r;
    int n = 0;
    if (!dj_eat(&scan, ']')) {
        do { dj_skip(&scan); n++; } while (dj_eat(&scan, ','));
    }
    g_dmo_index = (EdDmoIndexEntry *)calloc(n, sizeof(EdDmoIndexEntry));
    g_dmo_index_count = 0;
    if (!g_dmo_index) { free(buf); return; }

    if (!dj_eat(&r, ']')) {
        do {
            EdDmoIndexEntry *e = &g_dmo_index[g_dmo_index_count];
            if (!dj_eat(&r, '{')) { r.err = 1; break; }
            do {
                char key[32];
                if (!dj_str(&r, key, sizeof(key))) { r.err = 1; break; }
                if (!dj_eat(&r, ':')) { r.err = 1; break; }
                long iv;
                if      (!strcmp(key, "name"))        dj_str(&r, e->name, sizeof(e->name));
                else if (!strcmp(key, "sector"))     { dj_long(&r, &iv); e->sector = (int)iv; }
                else if (!strcmp(key, "gcl_path"))    dj_str(&r, e->gcl_path, sizeof(e->gcl_path));
                else if (!strcmp(key, "n_frames"))   { dj_long(&r, &iv); e->n_frames = (int)iv; }
                else if (!strcmp(key, "n_extracted")){ dj_long(&r, &iv); e->n_extracted = (int)iv; }
                else if (!strcmp(key, "n_models"))   { dj_long(&r, &iv); e->n_models = (int)iv; }
                else if (!strcmp(key, "n_maps"))     { dj_long(&r, &iv); e->n_maps = (int)iv; }
                else                                  dj_skip(&r);
            } while (dj_eat(&r, ','));
            if (!dj_eat(&r, '}')) { r.err = 1; break; }
            g_dmo_index_count++;
        } while (dj_eat(&r, ','));
    }
    free(buf);
    printf("[dmo] index loaded: %d entries\n", g_dmo_index_count);
}

/* ---------------------------------------------------------------------- */
/* Binary parsing of DEMO.DAT block streams.
 *
 * DEMO.DAT is laid out as packed `{type:8, size:24LE}` blocks at the
 * sector address recorded in the GCL `demo -s t:NNNN` directive. Block
 * types we care about:
 *   0x05  DMO data — first occurrence is a DMO_DEF (header), each
 *         subsequent is a DMO_DAT (per-frame camera + character pose).
 *   0xF0  end-of-stream marker.
 *   0xFF  wrap-to-top (live game ring buffer; not relevant when reading
 *         from the file directly — we just stop).
 *   other Audio / event / clock blocks. We skip them.
 *
 * Layout reference: source/include/fmt_dmo.h for DMO_DEF / DMO_DAT /
 * DMO_MAP / DMO_MDL / DMO_ADJ / DMO_CHA. The on-disc layout matches the
 * struct exactly modulo trailing 32-bit pointer fields that are
 * meaningless on disc (they're absolute offsets the runtime fixes up).
 */

#define DMO_BLOCK_DMO   0x05
#define DMO_BLOCK_END   0xF0
#define DMO_BLOCK_WRAP  0xFF
#define DMO_SECTOR      2048

/* Read 8MB by default — bigger than any disc dmo (largest is ~4MB).
 * Larger than this would be cheap to allocate but mostly wasted. */
#define DMO_READ_BYTES  (8 * 1024 * 1024)

extern int port_fs_read_dat(int file_id, long byte_off, int len, void *buf);

static unsigned long le_u32(const unsigned char *p)
{
    return (unsigned long)p[0]
         | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16)
         | ((unsigned long)p[3] << 24);
}
static long le_s32(const unsigned char *p)
{
    return (long)(int)le_u32(p);
}
static short le_s16(const unsigned char *p)
{
    int v = p[0] | (p[1] << 8);
    return (short)((v & 0x8000) ? (v - 0x10000) : v);
}

/* Parse one DMO_DEF block payload (the `raw` pointer points at the
 * 4-byte block header; layout is compatible with DMO_DEF.tag at offset 0).
 * Allocates d->maps and d->models; caller must already have d->name set. */
static int parse_def_block(EdDmoData *d, const unsigned char *raw, int sz)
{
    if (sz < 28) return 0;
    d->n_frames = (int)le_s32(raw + 8);
    int n_maps   = (int)le_s32(raw + 12);
    int n_models = (int)le_s32(raw + 16);
    unsigned long maps_off = le_u32(raw + 20);
    unsigned long mdls_off = le_u32(raw + 24);

    if (n_maps   < 0 || n_maps   > 256) n_maps   = 0;
    if (n_models < 0 || n_models > 256) n_models = 0;
    if (maps_off + (unsigned)n_maps   * 8u  > (unsigned)sz) n_maps   = 0;
    if (mdls_off + (unsigned)n_models * 20u > (unsigned)sz) n_models = 0;

    d->n_maps = n_maps;
    d->maps = n_maps ? (EdDmoMap *)calloc(n_maps, sizeof(EdDmoMap)) : NULL;
    for (int i = 0; i < n_maps; i++) {
        const unsigned char *p = raw + maps_off + i * 8;
        d->maps[i].cache_id = (int)le_u32(p);
        d->maps[i].filename = (int)le_u32(p + 4);
    }

    d->n_models = n_models;
    d->models = n_models ? (EdDmoModel *)calloc(n_models, sizeof(EdDmoModel)) : NULL;
    for (int i = 0; i < n_models; i++) {
        const unsigned char *p = raw + mdls_off + i * 20;
        d->models[i].type     = (int)le_u32(p);
        d->models[i].flag     = (int)le_u32(p + 4);
        d->models[i].cache_id = (int)le_u32(p + 8);
        d->models[i].filename = (int)le_u32(p + 12);
        d->models[i].name     = (int)le_u32(p + 16);
    }
    return 1;
}

/* Parse one DMO_DAT block payload into f. Skips charas[] (not used by the
 * inspector); allocates f->adjusts[] for the visible-character markers.
 * Layout (40-byte header):
 *   0..3   tag                 (block header replica)
 *   4..7   frame  (s32)
 *   8..13  eye_x/y/z   (3 * s16)
 *  14..19  center_x/y/z (3 * s16)
 *  20..21  roll      (s16)
 *  22..23  clip_dist (s16)
 *  24..25  pad / unused
 *  26..27  n_charas  (s16)
 *  28..31  chara_off (u32 byte offset relative to raw)
 *  32..33  n_adjusts (s16)
 *  34..35  pad
 *  36..39  adjust_off (u32 byte offset relative to raw)
 * DMO_ADJ stride: 24 bytes (int type + 8 shorts + ptr rots).
 */
static int parse_dat_block(EdDmoFrame *f, const unsigned char *raw, int sz)
{
    if (sz < 40) return 0;
    f->frame      = (int)le_s32(raw + 4);
    f->eye[0]     = le_s16(raw + 8);
    f->eye[1]     = le_s16(raw + 10);
    f->eye[2]     = le_s16(raw + 12);
    f->center[0]  = le_s16(raw + 14);
    f->center[1]  = le_s16(raw + 16);
    f->center[2]  = le_s16(raw + 18);
    f->roll       = le_s16(raw + 20);
    f->clip_dist  = le_s16(raw + 22);
    f->n_charas   = le_s16(raw + 26);
    f->n_adjusts  = le_s16(raw + 32);

    unsigned long adj_off = le_u32(raw + 36);
    int n = f->n_adjusts;
    if (n < 0 || n > 64) n = 0;
    if (adj_off + (unsigned)n * 24u > (unsigned)sz) n = 0;
    f->n_adjusts = (short)n;
    f->adjusts = n ? (EdDmoAdjust *)calloc(n, sizeof(EdDmoAdjust)) : NULL;
    for (int i = 0; i < n; i++) {
        unsigned long adj_self_off = adj_off + i * 24;
        const unsigned char *p = raw + adj_self_off;
        EdDmoAdjust *a = &f->adjusts[i];
        a->type    = (int)le_u32(p);
        a->visible = le_s16(p + 4);
        a->rot[0]  = le_s16(p + 6);
        a->rot[1]  = le_s16(p + 8);
        a->rot[2]  = le_s16(p + 10);
        a->pos[0]  = le_s16(p + 12);
        a->pos[1]  = le_s16(p + 14);
        a->pos[2]  = le_s16(p + 16);
        /* rots[] payload: n_rots Euler triplets at byte offset
         * (adjust_self_offset + adjust->rots_offset) — that's how the
         * runtime's OFFSET_TO_PTR(adjust, &adjust->rots) computes the
         * pointer (offset is relative to the adjust struct, not raw). */
        int n_rots = le_s16(p + 18);
        unsigned long rots_off_rel = le_u32(p + 20);
        if (n_rots <= 0 || n_rots > 64) {
            a->n_rots = 0; a->rots = NULL;
        } else {
            unsigned long rots_abs = adj_self_off + rots_off_rel;
            if (rots_abs + (unsigned)n_rots * 6u > (unsigned)sz) {
                a->n_rots = 0; a->rots = NULL;
            } else {
                a->n_rots = n_rots;
                a->rots = (short *)malloc(n_rots * 3 * sizeof(short));
                for (int j = 0; j < n_rots * 3; j++)
                    a->rots[j] = le_s16(raw + rots_abs + j * 2);
            }
        }
    }
    return 1;
}

/* Walk a buffer of DEMO.DAT bytes starting at a sector boundary. Fills
 * d with a parsed DMO_DEF + every DMO_DAT up to d->n_frames. Returns 1
 * on success (header found + at least one frame), 0 otherwise. */
static int parse_dmo_blocks(EdDmoData *d, const unsigned char *buf, int len)
{
    int off = 0;
    int got_def = 0;
    int frame_cap = 0;

    while (off + 4 <= len) {
        unsigned long hdr = le_u32(buf + off);
        unsigned int  type = hdr & 0xFF;
        unsigned int  size = (unsigned int)(hdr >> 8);
        if (type == 0 || size == 0 || size > 0x10000)            break;
        if (type == DMO_BLOCK_WRAP || type == DMO_BLOCK_END)     break;
        if ((unsigned)off + size > (unsigned)len)                break;

        if (type == DMO_BLOCK_DMO) {
            if (!got_def) {
                if (!parse_def_block(d, buf + off, size)) return 0;
                got_def = 1;
                /* Pre-allocate frame array now that we know n_frames.
                 * Cap defensively in case the header is corrupt. */
                if (d->n_frames < 0 || d->n_frames > 30000) {
                    printf("[dmo] suspicious n_frames=%d, capping\n", d->n_frames);
                    d->n_frames = 30000;
                }
                frame_cap = d->n_frames;
                d->frames = (EdDmoFrame *)calloc(frame_cap, sizeof(EdDmoFrame));
                d->n_extracted = 0;
            } else {
                if (d->n_extracted >= frame_cap) break;
                EdDmoFrame *f = &d->frames[d->n_extracted];
                if (parse_dat_block(f, buf + off, size))
                    d->n_extracted++;
            }
        }
        off += (int)size;
    }
    return got_def && d->n_extracted > 0;
}

/* Catalogue gcl_path looks like "decompiled/<stage>/(demo|scenerio).gcl".
 * Match the second segment against `stage_name`. Case-sensitive; the
 * disc and our overlays both use lowercase stage codes. */
int ed_dmo_find_for_stage(const char *stage_name, int max,
                          char (*out_names)[32])
{
    if (!stage_name || !*stage_name) return 0;
    ed_dmo_load_index();

    int found = 0;
    int slen = (int)strlen(stage_name);
    for (int i = 0; i < g_dmo_index_count; i++) {
        EdDmoIndexEntry *e = &g_dmo_index[i];
        const char *p = strstr(e->gcl_path, "decompiled/");
        if (!p) continue;
        p += sizeof("decompiled/") - 1;
        const char *slash = strchr(p, '/');
        if (!slash) continue;
        if ((slash - p) != slen) continue;
        if (memcmp(p, stage_name, slen) != 0) continue;
        if (out_names && found < max)
            snprintf(out_names[found], 32, "%s", e->name);
        found++;
    }
    return found;
}

static void free_frames(EdDmoFrame *frames, int n)
{
    if (!frames) return;
    for (int i = 0; i < n; i++) {
        if (frames[i].adjusts) {
            for (int j = 0; j < frames[i].n_adjusts; j++)
                free(frames[i].adjusts[j].rots);
            free(frames[i].adjusts);
        }
    }
    free(frames);
}

void ed_dmo_close(void)
{
    if (!g_dmo_active) return;
    EdDmoData *d = g_dmo_active;
    free_frames(d->frames, d->n_extracted);
    free(d->models);
    free(d->maps);
    free(d);
    g_dmo_active = NULL;
    g_dmo_active_frame = 0;
}

int ed_dmo_open(const char *name)
{
    ed_dmo_close();

    /* Look up the sector for this dmo name in the catalogue. The
     * catalogue itself is a tiny (~3KB) JSON harvested by
     * tools/extract_dmo.py from `demo -s` references in the decompiled
     * GCL — we keep that as data because it depends on script content,
     * not on the DEMO.DAT bytes. The per-frame data, however, comes
     * from DEMO.DAT directly. */
    ed_dmo_load_index();
    int sector = -1;
    for (int i = 0; i < g_dmo_index_count; i++) {
        if (strcmp(g_dmo_index[i].name, name) == 0) {
            sector = g_dmo_index[i].sector;
            break;
        }
    }
    if (sector < 0) {
        printf("[dmo] open: %s not in catalogue\n", name);
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc(DMO_READ_BYTES);
    if (!buf) return 0;
    long byte_off = (long)sector * DMO_SECTOR;
    int got = port_fs_read_dat(5 /* FS_FILEID_DEMO */, byte_off,
                               DMO_READ_BYTES, buf);
    if (got <= 0) {
        printf("[dmo] open: DEMO.DAT read failed at sector 0x%X "
               "(byte 0x%lX) — make sure the editor was launched with "
               "an --iso path so DEMO.DAT is mounted.\n",
               sector, byte_off);
        free(buf);
        return 0;
    }

    EdDmoData *d = (EdDmoData *)calloc(1, sizeof(EdDmoData));
    if (!d) { free(buf); return 0; }
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->sector = sector;

    if (!parse_dmo_blocks(d, buf, got)) {
        printf("[dmo] open: %s parse failed at sector 0x%X\n", name, sector);
        free_frames(d->frames, d->n_extracted);
        free(d->models); free(d->maps); free(d); free(buf);
        return 0;
    }
    free(buf);

    g_dmo_active = d;
    g_dmo_active_frame = 0;
    printf("[dmo] opened %s: %d/%d frames, %d models, %d maps "
           "(direct DEMO.DAT read)\n",
           name, d->n_extracted, d->n_frames, d->n_models, d->n_maps);
    return 1;
}


/* ---------------------------------------------------------------------- */
/* 3D-viewport camera-path overlay. Draws line segments connecting every
 * frame's eye position so the user can see the cinematic camera trajectory
 * laid over the stage. Highlights the currently-selected frame with a
 * yellow gizmo and a yellow line to its lookat target. Cheap: ~N segments
 * for N frames, all submitted as gl_submit_line3d. */
extern void  ed_world_to_eye(int wx, int wy, int wz, int eye[3]);
extern short ed_render_clip_dist(void);
extern void  gl_submit_line3d(const int *a, const int *b,
                              const unsigned char *cola,
                              const unsigned char *colb,
                              short clip_distance);

void ed_dmo_render_path(void)
{
    if (!g_dmo_active || !g_dmo_show_path) return;
    EdDmoData *d = g_dmo_active;
    if (d->n_extracted < 2) return;

    short cd = ed_render_clip_dist();
    /* Path color: muted teal that doesn't clash with the actor type-hash
     * palette. The current-frame gizmo uses bright yellow for visibility. */
    static const unsigned char path_col[3]    = { 80, 200, 220 };
    static const unsigned char gizmo_col[3]   = { 255, 240, 60 };
    static const unsigned char lookat_col[3]  = { 255, 140, 60 };

    int prev[3];
    ed_world_to_eye(d->frames[0].eye[0],
                    d->frames[0].eye[1],
                    d->frames[0].eye[2], prev);
    for (int i = 1; i < d->n_extracted; i++) {
        int cur[3];
        EdDmoFrame *f = &d->frames[i];
        ed_world_to_eye(f->eye[0], f->eye[1], f->eye[2], cur);
        gl_submit_line3d(prev, cur, path_col, path_col, cd);
        prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
    }

    /* Selected-frame gizmo: a small cross at eye + a line to center. */
    int af = g_dmo_active_frame;
    if (af < 0 || af >= d->n_extracted) return;
    EdDmoFrame *f = &d->frames[af];
    int eye[3], ctr[3];
    ed_world_to_eye(f->eye[0],    f->eye[1],    f->eye[2],    eye);
    ed_world_to_eye(f->center[0], f->center[1], f->center[2], ctr);
    /* lookat ray (eye → center) */
    gl_submit_line3d(eye, ctr, gizmo_col, lookat_col, cd);
    /* 3-axis cross at eye, ~250 PSX units (matches actor MARKER_R) */
    const int R = 250;
    int ax[3];
    int ex = f->eye[0], ey = f->eye[1], ez = f->eye[2];
    ed_world_to_eye(ex - R, ey,     ez,     ax);
    int bx[3]; ed_world_to_eye(ex + R, ey, ez, bx);
    gl_submit_line3d(ax, bx, gizmo_col, gizmo_col, cd);
    ed_world_to_eye(ex, ey - R, ez, ax);
    ed_world_to_eye(ex, ey + R, ez, bx);
    gl_submit_line3d(ax, bx, gizmo_col, gizmo_col, cd);
    ed_world_to_eye(ex, ey, ez - R, ax);
    ed_world_to_eye(ex, ey, ez + R, bx);
    gl_submit_line3d(ax, bx, gizmo_col, gizmo_col, cd);
}

/* Per-frame actor markers. For each visible DMO_ADJ at the active frame,
 * draw a colored cube at its world position. The marker is the only thing
 * the inspector can show today: rendering an actual posed character would
 * mean resolving DMO_MDL.cache_id to a KMD in the GV_CacheSystem and
 * applying the adjust's skeletal `rots[]` (which the extractor drops
 * anyway). The cubes are still useful — the user can scrub the timeline
 * and see Snake / Meryl / guards traverse the scene. */
static void cube_lines_at(int cx, int cy, int cz, int r,
                          const unsigned char col[3], short cd)
{
    int x0 = cx - r, x1 = cx + r;
    int y0 = cy - r, y1 = cy + r;
    int z0 = cz - r, z1 = cz + r;
    int a[3], b[3];
    #define DMO_LINE(ax,ay,az, bx,by,bz) \
        do { ed_world_to_eye(ax,ay,az,a); ed_world_to_eye(bx,by,bz,b); \
             gl_submit_line3d(a, b, col, col, cd); } while(0)
    DMO_LINE(x0,y0,z0, x1,y0,z0); DMO_LINE(x1,y0,z0, x1,y0,z1);
    DMO_LINE(x1,y0,z1, x0,y0,z1); DMO_LINE(x0,y0,z1, x0,y0,z0);
    DMO_LINE(x0,y1,z0, x1,y1,z0); DMO_LINE(x1,y1,z0, x1,y1,z1);
    DMO_LINE(x1,y1,z1, x0,y1,z1); DMO_LINE(x0,y1,z1, x0,y1,z0);
    DMO_LINE(x0,y0,z0, x0,y1,z0); DMO_LINE(x1,y0,z0, x1,y1,z0);
    DMO_LINE(x1,y0,z1, x1,y1,z1); DMO_LINE(x0,y0,z1, x0,y1,z1);
    #undef DMO_LINE
}

/* Look up a KMD in the engine's cache by low-24-bit id. Returns NULL when
 * the model isn't loaded (common: cinematic references a chara not
 * shipped in the stage's DATACNF). The void* is a DG_DEF; we keep it
 * opaque to avoid dragging libdg through ed_dmo.h. */
extern GV_CACHE_PAGE GV_CacheSystem;

static void *dmo_cache_lookup(int cache_id)
{
    int target = cache_id & 0xFFFFFF;
    if (!target) return NULL;
    int start = target % MAX_CACHE_TAGS;
    for (int i = 0; i < MAX_CACHE_TAGS; i++) {
        int slot = (start + i) % MAX_CACHE_TAGS;
        GV_CACHE_TAG *t = &GV_CacheSystem.tags[slot];
        int cur = t->id & 0xFFFFFF;
        if (cur == 0) return NULL;
        if (cur == target) return t->ptr;
    }
    return NULL;
}

extern void render_kmd_unlit(DG_DEF *def, int dist, int wx, int wy, int wz);
extern void render_kmd_posed(DG_DEF *def, int dist, const MATRIX *bones,
                             int wx, int wy, int wz);

/* Build the world-space bone matrices for a posed KMD. Walks DG_MDL[]
 * in order (assumes parents come before children — true for disc KMDs).
 * For each bone:
 *   local = R(rots[i])  with translation = mdl[i].pos
 *   if root (parent < 0): bone[i] = T(adj.pos) * R(adj.rot) * local
 *   else:                 bone[i] = bone[parent] * local
 *
 * Returns the number of bones filled (clamped to `cap`). When the
 * adjust has fewer rots than the KMD has bones, missing entries are
 * treated as identity rotations — characters stand in T-pose for the
 * unposed body parts. */
static int build_bone_matrices(DG_DEF *def, EdDmoAdjust *adj,
                               MATRIX *out, int cap)
{
    int n = def->n_models;
    if (n > cap) n = cap;
    SVECTOR root_rot = { adj->rot[0], adj->rot[1], adj->rot[2], 0 };
    MATRIX root;
    RotMatrixZYX_gte(&root_rot, &root);
    root.t[0] = adj->pos[0];
    root.t[1] = adj->pos[1];
    root.t[2] = adj->pos[2];

    for (int i = 0; i < n; i++) {
        DG_MDL *mdl = &def->model[i];
        SVECTOR rot = {0,0,0,0};
        if (adj->rots && i < adj->n_rots) {
            rot.vx = adj->rots[i*3 + 0];
            rot.vy = adj->rots[i*3 + 1];
            rot.vz = adj->rots[i*3 + 2];
        }
        MATRIX local;
        RotMatrixZYX_gte(&rot, &local);
        local.t[0] = mdl->pos.vx;
        local.t[1] = mdl->pos.vy;
        local.t[2] = mdl->pos.vz;

        int parent = mdl->parent;
        if (parent < 0 || parent >= i) {
            CompMatrix(&root, &local, &out[i]);
        } else {
            CompMatrix(&out[parent], &local, &out[i]);
        }
    }
    return n;
}

void ed_dmo_render_actors(void)
{
    if (!g_dmo_active || !g_dmo_show_actors) return;
    EdDmoData *d = g_dmo_active;
    if (g_dmo_active_frame < 0 || g_dmo_active_frame >= d->n_extracted) return;
    EdDmoFrame *f = &d->frames[g_dmo_active_frame];
    if (!f->adjusts || f->n_adjusts == 0) return;

    short cd = ed_render_clip_dist();
    /* Marker size: characters are ~1700 units tall; 600 gives a head-sized
     * cube that's distinct from the 250-unit static-actor cubes. */
    const int R = 600;
    /* Type-hash palette — same scheme as ed_actors.c uses for chara
     * markers. Index 0 (typically Snake) gets a high-contrast cyan. */
    static const unsigned char palette[8][3] = {
        { 100, 220, 255 }, { 255, 180, 100 }, { 200, 120, 255 },
        { 120, 220, 120 }, { 240, 240, 100 }, { 255, 100, 160 },
        { 200, 200, 200 }, { 160, 100,  80 },
    };

    for (int i = 0; i < f->n_adjusts; i++) {
        EdDmoAdjust *a = &f->adjusts[i];
        if (!a->visible) continue;
        /* Try to render the actual character KMD. Match the adjust's type
         * to a DMO_DEF.models[] entry by linear search (same as
         * source/kojo/demo.c::demothrd_8007CFE8), then look up its KMD in
         * the cache by cache_id. Translation only; rotation is
         * skeletal-animation territory. Falls back to a cube marker
         * when no model matched / not in cache. */
        void *def = NULL;
        if (g_dmo_show_models) {
            for (int m = 0; m < d->n_models; m++) {
                if (d->models[m].type == a->type) {
                    def = dmo_cache_lookup(d->models[m].cache_id);
                    break;
                }
            }
        }
        if (def) {
            /* Skeletal-pose render: build world matrices for every bone
             * and call render_kmd_posed. Per-bone rotations come from
             * DMO_ADJ.rots[], root rotation+translation from DMO_ADJ
             * itself. Static fall-through if pose-build fails (corrupt
             * data → render at adj.pos with translation only). */
            #define MAX_BONES 64
            MATRIX bones[MAX_BONES];
            int nb = build_bone_matrices((DG_DEF *)def, a, bones, MAX_BONES);
            if (nb > 0) {
                render_kmd_posed((DG_DEF *)def, cd, bones, 0, 0, 0);
            } else {
                render_kmd_unlit((DG_DEF *)def, cd,
                                 a->pos[0], a->pos[1], a->pos[2]);
            }
            #undef MAX_BONES
        } else {
            const unsigned char *col = palette[a->type & 7];
            cube_lines_at(a->pos[0], a->pos[1], a->pos[2], R, col, cd);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Authoring — keyframe-based timeline editor.
 *
 * The user places sparse "keys" along a frame timeline (one set for the
 * camera, one per DEMODOLL track) and the bake step interpolates them to
 * dense per-frame DMO_DAT records.
 *
 * On disk: data/dmo/custom/<name>.dmo, in the same {type:8, size:24LE}
 * block format as DEMO.DAT entries — re-parsable by ed_dmo_open_file. */

EdDmoTimeline g_dmo_timeline = {0};
int g_dmo_timeline_frame      = 0;
int g_dmo_timeline_sel_track  = -1;     /* -2 cam, 0..n_dolls-1 doll */
int g_dmo_timeline_sel_key    = -1;

extern void ed_camera_get_forward(float fwd[3]);

void ed_dmo_timeline_close(void)
{
    memset(&g_dmo_timeline, 0, sizeof(g_dmo_timeline));
    g_dmo_timeline_frame = 0;
    g_dmo_timeline_sel_track = -1;
    g_dmo_timeline_sel_key = -1;
}

void ed_dmo_timeline_new(const char *name, int n_frames)
{
    ed_dmo_timeline_close();
    if (n_frames <= 0 || n_frames > 30000) n_frames = 600;
    g_dmo_timeline.active = 1;
    g_dmo_timeline.n_frames = n_frames;
    snprintf(g_dmo_timeline.name, sizeof(g_dmo_timeline.name), "%s",
             (name && *name) ? name : "untitled");

    /* Seed two camera keys so the cinematic isn't empty: one at frame 0
     * snapped to current editor camera, one at the end with a slight
     * forward dolly. The user is expected to immediately re-snap them
     * to make sense for their stage. */
    int k0 = ed_dmo_timeline_add_cam_key(0);
    int k1 = ed_dmo_timeline_add_cam_key(n_frames - 1);
    g_dmo_timeline_sel_track = -2;
    g_dmo_timeline_sel_key   = k0;
    ed_dmo_timeline_snap_cam_to_editor();
    g_dmo_timeline_sel_key   = k1;
    ed_dmo_timeline_snap_cam_to_editor();
    /* Nudge the second key 2000 units forward along view so there's
     * visible motion between the two keys without further input. */
    if (k1 >= 0) {
        EdDmoCamKey *k = &g_dmo_timeline.cam_keys[k1];
        float fwd[3]; ed_camera_get_forward(fwd);
        k->eye[0]    = (short)(k->eye[0]    + fwd[0] * 2000.0f);
        k->eye[2]    = (short)(k->eye[2]    + fwd[2] * 2000.0f);
        k->center[0] = (short)(k->center[0] + fwd[0] * 2000.0f);
        k->center[2] = (short)(k->center[2] + fwd[2] * 2000.0f);
    }
    g_dmo_timeline_sel_key = -1;
}

/* ---- Camera keys (sorted by frame for binary-friendly traversal) ----- */

static int cam_key_insert_pos(int frame)
{
    int n = g_dmo_timeline.cam_n_keys;
    int i = 0;
    while (i < n && g_dmo_timeline.cam_keys[i].frame < frame) i++;
    return i;
}

int ed_dmo_timeline_add_cam_key(int frame)
{
    if (!g_dmo_timeline.active) return -1;
    if (frame < 0) frame = 0;
    if (frame >= g_dmo_timeline.n_frames) frame = g_dmo_timeline.n_frames - 1;
    if (g_dmo_timeline.cam_n_keys >= ED_DMO_MAX_KEYS) return -1;
    int pos = cam_key_insert_pos(frame);
    /* Replace if a key already lives on this exact frame. */
    if (pos < g_dmo_timeline.cam_n_keys &&
        g_dmo_timeline.cam_keys[pos].frame == frame) {
        return pos;
    }
    /* Shift later keys down. */
    for (int i = g_dmo_timeline.cam_n_keys; i > pos; i--)
        g_dmo_timeline.cam_keys[i] = g_dmo_timeline.cam_keys[i - 1];
    EdDmoCamKey *k = &g_dmo_timeline.cam_keys[pos];
    memset(k, 0, sizeof(*k));
    k->frame = frame;
    k->clip = 200;
    /* Initialize from interpolated current value so a new key doesn't
     * snap the camera back to origin — preserves a smooth path. */
    EdDmoCamKey eval;
    ed_dmo_timeline_eval_cam(frame, &eval);
    for (int i = 0; i < 3; i++) {
        k->eye[i]    = eval.eye[i];
        k->center[i] = eval.center[i];
    }
    k->roll = eval.roll;
    k->clip = eval.clip ? eval.clip : 200;
    g_dmo_timeline.cam_n_keys++;
    return pos;
}

int ed_dmo_timeline_remove_cam_key(int idx)
{
    if (idx < 0 || idx >= g_dmo_timeline.cam_n_keys) return 0;
    /* Disallow removing the last two keys; without them the bake produces
     * a degenerate single-shot cinematic. */
    if (g_dmo_timeline.cam_n_keys <= 2) return 0;
    for (int i = idx; i + 1 < g_dmo_timeline.cam_n_keys; i++)
        g_dmo_timeline.cam_keys[i] = g_dmo_timeline.cam_keys[i + 1];
    g_dmo_timeline.cam_n_keys--;
    return 1;
}

void ed_dmo_timeline_snap_cam_to_editor(void)
{
    if (g_dmo_timeline_sel_track != -2) return;
    if (g_dmo_timeline_sel_key < 0 ||
        g_dmo_timeline_sel_key >= g_dmo_timeline.cam_n_keys) return;
    EdDmoCamKey *k = &g_dmo_timeline.cam_keys[g_dmo_timeline_sel_key];
    float fwd[3]; ed_camera_get_forward(fwd);
    k->eye[0]    = (short)g_cam.pos[0];
    k->eye[1]    = (short)g_cam.pos[1];
    k->eye[2]    = (short)g_cam.pos[2];
    const float reach = 4096.0f;
    k->center[0] = (short)(g_cam.pos[0] + fwd[0] * reach);
    k->center[1] = (short)(g_cam.pos[1] + fwd[1] * reach);
    k->center[2] = (short)(g_cam.pos[2] + fwd[2] * reach);
}

/* Linear interpolation between adjacent keys. Outside the keyed range,
 * clamps to the first / last key's value. */
static int lerp_s16(int a, int b, int num, int denom)
{
    if (denom <= 0) return a;
    long long delta = (long long)(b - a) * num;
    return a + (int)(delta / denom);
}

void ed_dmo_timeline_eval_cam(int frame, EdDmoCamKey *out)
{
    memset(out, 0, sizeof(*out));
    out->clip = 200;
    int n = g_dmo_timeline.cam_n_keys;
    if (n == 0) return;
    if (frame <= g_dmo_timeline.cam_keys[0].frame) {
        *out = g_dmo_timeline.cam_keys[0];
        out->frame = frame;
        return;
    }
    if (frame >= g_dmo_timeline.cam_keys[n - 1].frame) {
        *out = g_dmo_timeline.cam_keys[n - 1];
        out->frame = frame;
        return;
    }
    int i = 0;
    while (i + 1 < n && g_dmo_timeline.cam_keys[i + 1].frame <= frame) i++;
    EdDmoCamKey *a = &g_dmo_timeline.cam_keys[i];
    EdDmoCamKey *b = &g_dmo_timeline.cam_keys[i + 1];
    int span = b->frame - a->frame;
    int t    = frame - a->frame;
    for (int j = 0; j < 3; j++) {
        out->eye[j]    = (short)lerp_s16(a->eye[j],    b->eye[j],    t, span);
        out->center[j] = (short)lerp_s16(a->center[j], b->center[j], t, span);
    }
    out->roll = (short)lerp_s16(a->roll, b->roll, t, span);
    out->clip = (short)lerp_s16(a->clip, b->clip, t, span);
    out->frame = frame;
}

/* ---- Doll tracks ----------------------------------------------------- */

int ed_dmo_timeline_add_doll(const char *label, int type, int cache_id)
{
    if (!g_dmo_timeline.active) return -1;
    if (g_dmo_timeline.n_dolls >= ED_DMO_MAX_DOLLS) return -1;
    EdDmoTrack *t = &g_dmo_timeline.dolls[g_dmo_timeline.n_dolls];
    memset(t, 0, sizeof(*t));
    snprintf(t->label, sizeof(t->label), "%s",
             (label && *label) ? label : "Doll");
    t->type     = type > 0 ? type : g_dmo_timeline.n_dolls + 1;
    t->cache_id = cache_id;
    /* Seed one visible key at frame 0 — saves the user a click. */
    EdDmoDollKey *k = &t->keys[0];
    memset(k, 0, sizeof(*k));
    k->visible = 1;
    k->frame = 0;
    /* Position at editor camera — usable default. */
    k->pos[0] = (short)g_cam.pos[0];
    k->pos[1] = (short)g_cam.pos[1];
    k->pos[2] = (short)g_cam.pos[2];
    t->n_keys = 1;
    return g_dmo_timeline.n_dolls++;
}

int ed_dmo_timeline_remove_doll(int doll_idx)
{
    if (doll_idx < 0 || doll_idx >= g_dmo_timeline.n_dolls) return 0;
    for (int i = doll_idx; i + 1 < g_dmo_timeline.n_dolls; i++)
        g_dmo_timeline.dolls[i] = g_dmo_timeline.dolls[i + 1];
    g_dmo_timeline.n_dolls--;
    return 1;
}

static int doll_key_insert_pos(EdDmoTrack *t, int frame)
{
    int i = 0;
    while (i < t->n_keys && t->keys[i].frame < frame) i++;
    return i;
}

int ed_dmo_timeline_add_doll_key(int doll_idx, int frame)
{
    if (doll_idx < 0 || doll_idx >= g_dmo_timeline.n_dolls) return -1;
    EdDmoTrack *t = &g_dmo_timeline.dolls[doll_idx];
    if (t->n_keys >= ED_DMO_MAX_KEYS) return -1;
    if (frame < 0) frame = 0;
    if (frame >= g_dmo_timeline.n_frames) frame = g_dmo_timeline.n_frames - 1;
    int pos = doll_key_insert_pos(t, frame);
    if (pos < t->n_keys && t->keys[pos].frame == frame) return pos;
    for (int i = t->n_keys; i > pos; i--) t->keys[i] = t->keys[i - 1];
    EdDmoDollKey *k = &t->keys[pos];
    memset(k, 0, sizeof(*k));
    k->frame = frame;
    k->visible = 1;
    EdDmoDollKey eval;
    ed_dmo_timeline_eval_doll(doll_idx, frame, &eval);
    for (int i = 0; i < 3; i++) { k->pos[i] = eval.pos[i]; k->rot[i] = eval.rot[i]; }
    k->visible = eval.visible;
    t->n_keys++;
    return pos;
}

int ed_dmo_timeline_remove_doll_key(int doll_idx, int key_idx)
{
    if (doll_idx < 0 || doll_idx >= g_dmo_timeline.n_dolls) return 0;
    EdDmoTrack *t = &g_dmo_timeline.dolls[doll_idx];
    if (key_idx < 0 || key_idx >= t->n_keys) return 0;
    if (t->n_keys <= 1) return 0;
    for (int i = key_idx; i + 1 < t->n_keys; i++) t->keys[i] = t->keys[i + 1];
    t->n_keys--;
    return 1;
}

void ed_dmo_timeline_snap_doll_to_editor(void)
{
    if (g_dmo_timeline_sel_track < 0 ||
        g_dmo_timeline_sel_track >= g_dmo_timeline.n_dolls) return;
    EdDmoTrack *t = &g_dmo_timeline.dolls[g_dmo_timeline_sel_track];
    if (g_dmo_timeline_sel_key < 0 ||
        g_dmo_timeline_sel_key >= t->n_keys) return;
    EdDmoDollKey *k = &t->keys[g_dmo_timeline_sel_key];
    k->pos[0] = (short)g_cam.pos[0];
    k->pos[1] = (short)g_cam.pos[1];
    k->pos[2] = (short)g_cam.pos[2];
}

void ed_dmo_timeline_eval_doll(int doll_idx, int frame, EdDmoDollKey *out)
{
    memset(out, 0, sizeof(*out));
    out->visible = 1;
    if (doll_idx < 0 || doll_idx >= g_dmo_timeline.n_dolls) return;
    EdDmoTrack *t = &g_dmo_timeline.dolls[doll_idx];
    if (t->n_keys == 0) return;
    if (frame <= t->keys[0].frame)              { *out = t->keys[0];               out->frame = frame; return; }
    if (frame >= t->keys[t->n_keys - 1].frame)  { *out = t->keys[t->n_keys - 1];   out->frame = frame; return; }
    int i = 0;
    while (i + 1 < t->n_keys && t->keys[i + 1].frame <= frame) i++;
    EdDmoDollKey *a = &t->keys[i];
    EdDmoDollKey *b = &t->keys[i + 1];
    int span = b->frame - a->frame;
    int tt   = frame - a->frame;
    for (int j = 0; j < 3; j++) {
        out->pos[j] = (short)lerp_s16(a->pos[j], b->pos[j], tt, span);
        out->rot[j] = (short)lerp_s16(a->rot[j], b->rot[j], tt, span);
    }
    /* Visibility uses A's value across the span — flips at the next key. */
    out->visible = a->visible;
    out->frame = frame;
}

/* ---- Live preview render -------------------------------------------- */

void ed_dmo_render_timeline_path(void)
{
    if (!g_dmo_timeline.active || g_dmo_timeline.cam_n_keys < 2) return;
    short cd = ed_render_clip_dist();
    static const unsigned char path_col[3]  = {  80, 200, 220 };
    static const unsigned char key_col[3]   = { 255, 220,  80 };
    static const unsigned char gizmo_col[3] = { 255, 240,  60 };

    /* Path between adjacent keys (linearly interpolated, sampled every
     * 4 frames so curves stay readable). */
    int prev[3];
    EdDmoCamKey s; ed_dmo_timeline_eval_cam(0, &s);
    ed_world_to_eye(s.eye[0], s.eye[1], s.eye[2], prev);
    int step = g_dmo_timeline.n_frames / 256;
    if (step < 1) step = 1;
    for (int f = step; f < g_dmo_timeline.n_frames; f += step) {
        EdDmoCamKey e; ed_dmo_timeline_eval_cam(f, &e);
        int cur[3];
        ed_world_to_eye(e.eye[0], e.eye[1], e.eye[2], cur);
        gl_submit_line3d(prev, cur, path_col, path_col, cd);
        prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
    }
    /* Key markers — small ×-cross in yellow. */
    for (int i = 0; i < g_dmo_timeline.cam_n_keys; i++) {
        EdDmoCamKey *k = &g_dmo_timeline.cam_keys[i];
        const int R = 300;
        int a[3], b[3];
        ed_world_to_eye(k->eye[0] - R, k->eye[1], k->eye[2], a);
        ed_world_to_eye(k->eye[0] + R, k->eye[1], k->eye[2], b);
        gl_submit_line3d(a, b, key_col, key_col, cd);
        ed_world_to_eye(k->eye[0], k->eye[1] - R, k->eye[2], a);
        ed_world_to_eye(k->eye[0], k->eye[1] + R, k->eye[2], b);
        gl_submit_line3d(a, b, key_col, key_col, cd);
        ed_world_to_eye(k->eye[0], k->eye[1], k->eye[2] - R, a);
        ed_world_to_eye(k->eye[0], k->eye[1], k->eye[2] + R, b);
        gl_submit_line3d(a, b, key_col, key_col, cd);
    }
    /* Gizmo at scrubber frame: eye + line to center. */
    EdDmoCamKey g; ed_dmo_timeline_eval_cam(g_dmo_timeline_frame, &g);
    int eye[3], ctr[3];
    ed_world_to_eye(g.eye[0],    g.eye[1],    g.eye[2],    eye);
    ed_world_to_eye(g.center[0], g.center[1], g.center[2], ctr);
    gl_submit_line3d(eye, ctr, gizmo_col, gizmo_col, cd);
}

void ed_dmo_render_timeline_actors(void)
{
    if (!g_dmo_timeline.active) return;
    short cd = ed_render_clip_dist();
    static const unsigned char palette[8][3] = {
        { 100, 220, 255 }, { 255, 180, 100 }, { 200, 120, 255 },
        { 120, 220, 120 }, { 240, 240, 100 }, { 255, 100, 160 },
        { 200, 200, 200 }, { 160, 100,  80 },
    };
    for (int i = 0; i < g_dmo_timeline.n_dolls; i++) {
        EdDmoDollKey p; ed_dmo_timeline_eval_doll(i, g_dmo_timeline_frame, &p);
        if (!p.visible) continue;
        const unsigned char *col = palette[i & 7];
        cube_lines_at(p.pos[0], p.pos[1], p.pos[2], 600, col, cd);
    }
}

/* ---- Bake → .dmo binary --------------------------------------------- */

static void w_u32(unsigned char **p, unsigned int v)
{
    (*p)[0] = (unsigned char)(v       & 0xFF);
    (*p)[1] = (unsigned char)((v >>  8) & 0xFF);
    (*p)[2] = (unsigned char)((v >> 16) & 0xFF);
    (*p)[3] = (unsigned char)((v >> 24) & 0xFF);
    *p += 4;
}
static void w_s16(unsigned char **p, short v)
{
    unsigned short u = (unsigned short)v;
    (*p)[0] = (unsigned char)(u      & 0xFF);
    (*p)[1] = (unsigned char)((u>>8) & 0xFF);
    *p += 2;
}
static void w_block_tag(unsigned char *blk, unsigned int type, unsigned int size)
{
    unsigned int hdr = (size << 8) | (type & 0xFF);
    blk[0] = (unsigned char)(hdr       & 0xFF);
    blk[1] = (unsigned char)((hdr >>  8) & 0xFF);
    blk[2] = (unsigned char)((hdr >> 16) & 0xFF);
    blk[3] = (unsigned char)((hdr >> 24) & 0xFF);
}

int ed_dmo_timeline_save(void)
{
    if (!g_dmo_timeline.active || g_dmo_timeline.cam_n_keys < 2) {
        printf("[dmo-author] need at least 2 camera keys + an active "
               "timeline before saving\n");
        return 0;
    }

    int N = g_dmo_timeline.n_frames;
    int n_models = g_dmo_timeline.n_dolls;

    /* DMO_DEF block:
     *   28-byte header + n_models * 20 bytes of DMO_MDL records. We
     *   don't ship maps (n_maps = 0). */
    int def_sz = 28 + n_models * 20;
    /* DMO_DAT per frame:
     *   40-byte header + n_dolls * 24 bytes of DMO_ADJ records (each
     *   adjust has rots_off = 0 since we don't author per-bone rots). */
    int dat_sz = 40 + n_models * 24;
    int end_sz = 4;
    int total  = def_sz + N * dat_sz + end_sz;

    unsigned char *buf = (unsigned char *)calloc(total, 1);
    if (!buf) return 0;
    unsigned char *p = buf;

    /* DMO_DEF */
    w_block_tag(p, 0x05, def_sz); p += 4;
    w_u32(&p, 0);                   /* frame    = 0 */
    w_u32(&p, (unsigned)N);         /* n_frames */
    w_u32(&p, 0);                   /* n_maps   */
    w_u32(&p, (unsigned)n_models);  /* n_models */
    w_u32(&p, 0);                   /* maps_off */
    w_u32(&p, n_models ? 28u : 0u); /* models_off */
    for (int i = 0; i < n_models; i++) {
        EdDmoTrack *t = &g_dmo_timeline.dolls[i];
        w_u32(&p, (unsigned)t->type);     /* type */
        w_u32(&p, 0);                     /* flag */
        w_u32(&p, (unsigned)t->cache_id); /* cache_id (KMD lookup) */
        w_u32(&p, (unsigned)(t->cache_id & 0xFFFFFF)); /* filename hash */
        w_u32(&p, 0);                     /* name */
    }

    /* DMO_DAT blocks */
    for (int f = 0; f < N; f++) {
        EdDmoCamKey c; ed_dmo_timeline_eval_cam(f, &c);
        w_block_tag(p, 0x05, dat_sz); p += 4;
        w_u32(&p, (unsigned)f);
        w_s16(&p, c.eye[0]); w_s16(&p, c.eye[1]); w_s16(&p, c.eye[2]);
        w_s16(&p, c.center[0]); w_s16(&p, c.center[1]); w_s16(&p, c.center[2]);
        w_s16(&p, c.roll);
        w_s16(&p, c.clip ? c.clip : 200);
        w_s16(&p, 0);                     /* pad @ 24 */
        w_s16(&p, 0);                     /* n_charas @ 26 */
        w_u32(&p, 0);                     /* chara_off */
        w_s16(&p, (short)n_models);       /* n_adjusts */
        w_s16(&p, 0);                     /* pad @ 34 */
        /* adjust_off relative to block start; our adjusts begin right
         * after the 40-byte header. */
        w_u32(&p, n_models ? 40u : 0u);
        /* Adjust array: 24 bytes per doll. */
        for (int i = 0; i < n_models; i++) {
            EdDmoDollKey d; ed_dmo_timeline_eval_doll(i, f, &d);
            EdDmoTrack *t = &g_dmo_timeline.dolls[i];
            w_u32(&p, (unsigned)t->type);
            w_s16(&p, d.visible);
            w_s16(&p, d.rot[0]); w_s16(&p, d.rot[1]); w_s16(&p, d.rot[2]);
            w_s16(&p, d.pos[0]); w_s16(&p, d.pos[1]); w_s16(&p, d.pos[2]);
            w_s16(&p, 0);                 /* n_rots = 0 (no per-bone authoring) */
            w_u32(&p, 0);                 /* rots_off */
        }
    }

    /* End-of-stream marker */
    w_block_tag(p, 0xF0, end_sz); p += 4;

    char path[160];
    snprintf(path, sizeof(path), "data/dmo/custom/%s.dmo",
             g_dmo_timeline.name[0] ? g_dmo_timeline.name : "untitled");
    system("mkdir -p data/dmo/custom 2>/dev/null");
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("[dmo-author] cannot open %s for write\n", path);
        free(buf); return 0;
    }
    int wrote = (int)fwrite(buf, 1, total, fp);
    fclose(fp);
    free(buf);
    if (wrote != total) {
        printf("[dmo-author] short write %d/%d → %s\n", wrote, total, path);
        return 0;
    }
    printf("[dmo-author] saved %s (%d frames, %d models, %d bytes)\n",
           path, N, n_models, total);
    return 1;
}


/* ---- Load custom .dmo file -------------------------------------------- */

int ed_dmo_open_file(const char *path)
{
    ed_dmo_close();
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("[dmo] open_file: cannot read %s\n", path);
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > DMO_READ_BYTES) {
        printf("[dmo] open_file: bad size %ld in %s\n", sz, path);
        fclose(fp);
        return 0;
    }
    unsigned char *buf = (unsigned char *)malloc(sz);
    if (!buf) { fclose(fp); return 0; }
    long rd = (long)fread(buf, 1, sz, fp);
    fclose(fp);
    if (rd != sz) { free(buf); return 0; }

    EdDmoData *d = (EdDmoData *)calloc(1, sizeof(EdDmoData));
    if (!d) { free(buf); return 0; }
    /* Pull a friendly name out of the path tail. */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(d->name, sizeof(d->name), "%s", base);
    d->sector = -1;     /* not from DEMO.DAT */

    if (!parse_dmo_blocks(d, buf, (int)rd)) {
        printf("[dmo] open_file: %s parse failed\n", path);
        free_frames(d->frames, d->n_extracted);
        free(d->models); free(d->maps); free(d); free(buf);
        return 0;
    }
    free(buf);

    g_dmo_active = d;
    g_dmo_active_frame = 0;
    printf("[dmo] opened file %s: %d frames\n", path, d->n_extracted);
    return 1;
}
