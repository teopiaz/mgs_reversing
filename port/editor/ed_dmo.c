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
int              g_dmo_follow_cam = 0;

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
        const unsigned char *p = raw + adj_off + i * 24;
        EdDmoAdjust *a = &f->adjusts[i];
        a->type    = (int)le_u32(p);
        a->visible = le_s16(p + 4);
        a->rot[0]  = le_s16(p + 6);
        a->rot[1]  = le_s16(p + 8);
        a->rot[2]  = le_s16(p + 10);
        a->pos[0]  = le_s16(p + 12);
        a->pos[1]  = le_s16(p + 14);
        a->pos[2]  = le_s16(p + 16);
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

void ed_dmo_close(void)
{
    if (!g_dmo_active) return;
    EdDmoData *d = g_dmo_active;
    if (d->frames) {
        for (int i = 0; i < d->n_extracted; i++)
            free(d->frames[i].adjusts);
        free(d->frames);
    }
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
        if (d->frames) {
            for (int i = 0; i < d->n_extracted; i++)
                free(d->frames[i].adjusts);
            free(d->frames);
        }
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
        const unsigned char *col = palette[a->type & 7];
        cube_lines_at(a->pos[0], a->pos[1], a->pos[2], R, col, cd);
    }
}
