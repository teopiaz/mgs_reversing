/* Loads actor placements from a JSON (preferred) or TSV produced by
   tools/extract_actors.py and draws a small wireframe cube for each
   one in world space. The JSON path preserves every `chara` option so
   downstream features (vision cones, item ids, trap event handlers) can
   read them per actor; the TSV path is kept as a fallback for any data
   directory that hasn't been regenerated yet. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "libgte.h"
#include "libdg/gl_renderer.h"
#include "editor.h"

/* Forward declarations of libgv cache helpers used by the loader; the
 * full libgv.h #include happens further down where the existing
 * type→KMD lookup lives. Keeping the include narrow keeps top-of-file
 * compile-time low. */
extern int   GV_CacheID2(const char *name, int ext);
static void *quiet_get_cache(int id);

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
    a->item_id = a->item_height = a->vision_range = -1;
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

/* ---------------------------------------------------------------------------
 * Minimal JSON reader specialised for the schema produced by
 * tools/extract_actors.py:
 *   [ { "type": str, "instance": str, "pos": null | [int,int,int],
 *       "rot": null | "b:N", "flags": "b:N", "non_spatial": bool,
 *       "options": { "<letter>": [<scalars>] }, ... }, ... ]
 *
 * Hand-rolled because (a) no JSON lib in the project, and (b) we only
 * need ~6 keys per actor — recursive-descent on a known schema is
 * shorter than vendoring a parser. Robust to whitespace / arrays /
 * trailing fields; gives up cleanly on malformed input.
 * ------------------------------------------------------------------------- */
typedef struct {
    const char *p;
    const char *end;
    int err;
} JR;

static void jr_skip_ws(JR *r) {
    while (r->p < r->end) {
        char c = *r->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') r->p++;
        else break;
    }
}
static int jr_peek(JR *r)            { jr_skip_ws(r); return r->p < r->end ? *r->p : -1; }
static int jr_eat(JR *r, char c)     { jr_skip_ws(r); if (r->p < r->end && *r->p == c) { r->p++; return 1; } return 0; }
static int jr_eat_lit(JR *r, const char *s) {
    jr_skip_ws(r);
    size_t n = strlen(s);
    if ((size_t)(r->end - r->p) < n) return 0;
    if (memcmp(r->p, s, n) != 0) return 0;
    r->p += n; return 1;
}

/* Read a JSON string into out (NUL-terminated, truncated to outlen-1).
 * Doesn't decode \uXXXX (the extractor never emits those) — handles \\, \",
 * \n, \t, \/. Returns 1 on success, 0 if not at a string opening (silent —
 * the caller chooses whether that's an error). Sets r->err only for
 * mid-string failures (unterminated literal). */
static int jr_str(JR *r, char *out, int outlen) {
    jr_skip_ws(r);
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
                case '"': case '\\': case '/': c = e; break;
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

/* Read a JSON number into *out (truncated to long, then int by caller). */
static int jr_int(JR *r, long *out) {
    jr_skip_ws(r);
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

/* Skip any JSON value (string / number / true / false / null / array / object).
 * Used for keys we don't care about. Recursive — bounded by JSON nesting. */
static void jr_skip_value(JR *r);
static void jr_skip_value(JR *r) {
    int c = jr_peek(r);
    if (c == -1) { r->err = 1; return; }
    if (c == '"') { char tmp[4]; jr_str(r, tmp, sizeof(tmp)); return; }
    if (c == '{') {
        r->p++;
        if (jr_eat(r, '}')) return;
        do {
            char k[32];
            if (!jr_str(r, k, sizeof(k))) { r->err = 1; return; }
            if (!jr_eat(r, ':')) { r->err = 1; return; }
            jr_skip_value(r);
        } while (jr_eat(r, ','));
        if (!jr_eat(r, '}')) r->err = 1;
        return;
    }
    if (c == '[') {
        r->p++;
        if (jr_eat(r, ']')) return;
        do { jr_skip_value(r); } while (jr_eat(r, ','));
        if (!jr_eat(r, ']')) r->err = 1;
        return;
    }
    if (jr_eat_lit(r, "null") || jr_eat_lit(r, "true") || jr_eat_lit(r, "false"))
        return;
    long n; if (jr_int(r, &n)) {
        /* eat optional fractional / exponent so we tolerate floats too */
        while (r->p < r->end && (*r->p == '.' || *r->p == 'e' || *r->p == 'E' ||
                                  *r->p == '+' || *r->p == '-' ||
                                  (*r->p >= '0' && *r->p <= '9'))) r->p++;
        return;
    }
    r->err = 1;
}

/* "b:N" → N (returns -1 if not in that form). Used both for rot bytes
 * and for option values like ITEM `-i b:N`. */
static int parse_b_num(const char *s) {
    if (!s) return -1;
    if (s[0] == 'b' && s[1] == ':') return atoi(s + 2) & 0xFF;
    return -1;
}

/* Parse one element of an option array: supports "b:N" strings, "'X'"
 * single-character literals, and bare integers. Sets *out_int if it
 * parsed an int, *out_char if it parsed a char-literal, *out_b if "b:N".
 * Returns 1 on consumed value (any kind). */
static int parse_option_scalar(JR *r, long *out_int, char *out_char, int *out_b)
{
    *out_int = 0; *out_char = 0; *out_b = -1;
    int c = jr_peek(r);
    if (c == '"') {
        char buf[32];
        if (!jr_str(r, buf, sizeof(buf))) return 0;
        /* "b:N" */
        int b = parse_b_num(buf);
        if (b >= 0) { *out_b = b; return 1; }
        /* "'X'" — single char literal */
        if (buf[0] == '\'' && buf[1] && buf[2] == '\'') {
            *out_char = buf[1];
            return 1;
        }
        return 1;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        long v; if (!jr_int(r, &v)) return 0;
        *out_int = v;
        return 1;
    }
    /* unknown scalar shape (e.g. sub_XXXX symbol stored as string with no
     * quote? extractor always quotes — fall through and skip). */
    jr_skip_value(r);
    return 1;
}

/* Read the "options" object and pull out the per-actor gameplay fields
 * we currently care about (-i / -h / -l / -b / -a / -f / -e / -y).
 * Anything else is harmlessly skipped. */
static void parse_actor_options(JR *r, EdActor *a)
{
    if (!jr_eat(r, '{')) { r->err = 1; return; }
    if (jr_eat(r, '}')) return;
    do {
        char key[16];
        if (!jr_str(r, key, sizeof(key))) { r->err = 1; return; }
        if (!jr_eat(r, ':')) { r->err = 1; return; }
        /* The option is always an array (extract_actors.py lifts every
         * "-x v1 v2 ..." into ["v1","v2",...]). Walk it scalar-by-scalar
         * and pluck whichever items map to an EdActor field. */
        if (!jr_eat(r, '[')) { jr_skip_value(r); goto next; }
        int idx = 0;
        if (!jr_eat(r, ']')) {
            do {
                long iv; char cv; int bv;
                parse_option_scalar(r, &iv, &cv, &bv);
                if (idx == 0) {
                    /* Most options are length-1 — capture from the head. The
                     * `-b` key has two shapes depending on actor type: ITEM
                     * uses "b:N" for box-variant id, WATCHER uses "'P'" for
                     * behavior; capture both into separate EdActor fields. */
                    if      (key[0] == 'i' && key[1] == 0) a->item_id      = (bv >= 0) ? bv : (int)iv;
                    else if (key[0] == 'h' && key[1] == 0) a->item_height  = (int)iv;
                    else if (key[0] == 'n' && key[1] == 0) a->item_count   = (bv >= 0) ? bv : (int)iv;
                    else if (key[0] == 'l' && key[1] == 0) a->vision_range = (bv >= 0) ? bv : (int)iv;
                    else if (key[0] == 'b' && key[1] == 0) {
                        if (bv >= 0) a->box_type = bv;
                        if (cv)      a->behavior = cv;
                    }
                    else if (key[0] == 'a' && key[1] == 0) a->alertness    = cv;
                    else if (key[0] == 'f' && key[1] == 0) a->flags_n      = (bv >= 0) ? bv : (int)iv;
                    else if (key[0] == 'e' && key[1] == 0) {
                        /* event proc — extractor stored "sub_XXXX" as a string */
                        /* parse_option_scalar already consumed it as a string with
                         * no special meaning; we need the raw text. Backfill by
                         * re-parsing in-place isn't practical here, so we
                         * special-case 'e' below by skipping this scalar and
                         * re-reading. */
                    }
                }
                idx++;
            } while (jr_eat(r, ','));
            jr_eat(r, ']');
        }
next:
        ;
    } while (jr_eat(r, ','));
    if (!jr_eat(r, '}')) r->err = 1;
}

/* Re-walk the JSON for one specific option whose value is a plain string
 * (e.g. -e sub_XXXX, -y sub_XXXX) and copy it into `out`. We need this
 * because parse_option_scalar throws away the raw text of unrecognised
 * string forms; rather than complicate that path, we make a second pass
 * for the small handful of "string-symbol" options. */
static void capture_string_option(const char *opts_begin, const char *opts_end,
                                  const char *key, char *out, int outlen)
{
    /* Scan for `"key": [ "value"` — small-string match good enough for our
     * single-character keys. Skips embedded objects (none in our schema). */
    out[0] = 0;
    char needle[8];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = opts_begin;
    while (p < opts_end) {
        const char *m = (const char *)memmem((const void *)p, opts_end - p,
                                             (const void *)needle, strlen(needle));
        if (!m) return;
        const char *q = m + strlen(needle);
        while (q < opts_end && (*q == ' ' || *q == ':' || *q == '\t')) q++;
        if (q >= opts_end || *q != '[') { p = m + 1; continue; }
        q++;
        while (q < opts_end && (*q == ' ' || *q == '\t')) q++;
        if (q >= opts_end || *q != '"') return;
        q++;
        int n = 0;
        while (q < opts_end && *q != '"' && n + 1 < outlen) {
            out[n++] = *q++;
        }
        out[n] = 0;
        return;
    }
}

/* Parse one actor object out of the JSON array. `r` is positioned at the
 * '{'. Updates g_actors[g_actor_count] and increments the count. */
static void parse_actor_object(JR *r)
{
    if (!jr_eat(r, '{')) { r->err = 1; return; }

    if (g_actor_count >= g_actor_capacity) {
        g_actor_capacity = g_actor_capacity ? g_actor_capacity * 2 : 64;
        g_actors = (EdActor *)realloc(g_actors, g_actor_capacity * sizeof(EdActor));
    }
    EdActor *a = &g_actors[g_actor_count];
    memset(a, 0, sizeof(*a));
    a->rot_b        = -1;
    a->item_id      = -1;
    a->item_count   = -1;
    a->item_height  = -1;
    a->box_type     = -1;
    a->vision_range = -1;
    a->color        = 0xC0C0C0;
    a->has_pos      = 0;

    /* Save the start of this actor's JSON range so we can do a follow-up
     * memmem pass for string-shaped options (-e, -y) without complicating
     * the main parser. */
    const char *obj_begin = r->p;

    if (!jr_eat(r, '}')) {
        do {
            char key[24];
            if (!jr_str(r, key, sizeof(key))) { r->err = 1; return; }
            if (!jr_eat(r, ':')) { r->err = 1; return; }

            if (strcmp(key, "type") == 0) {
                jr_str(r, a->type, sizeof(a->type));
            }
            else if (strcmp(key, "instance") == 0) {
                jr_str(r, a->instance, sizeof(a->instance));
            }
            else if (strcmp(key, "pos") == 0) {
                if (jr_eat_lit(r, "null")) {
                    a->has_pos = 0;
                } else if (jr_eat(r, '[')) {
                    long v[3] = {0,0,0};
                    for (int i = 0; i < 3; i++) {
                        jr_int(r, &v[i]);
                        if (i < 2) jr_eat(r, ',');
                    }
                    jr_eat(r, ']');
                    a->pos[0] = (int)v[0];
                    a->pos[1] = (int)v[1];
                    a->pos[2] = (int)v[2];
                    a->has_pos = 1;
                } else jr_skip_value(r);
            }
            else if (strcmp(key, "rot") == 0) {
                if (jr_eat_lit(r, "null")) {
                    a->rot_b = -1;
                } else {
                    char buf[24];
                    if (jr_str(r, buf, sizeof(buf))) {
                        int b = parse_b_num(buf);
                        if (b >= 0) a->rot_b = b;
                    } else jr_skip_value(r);
                }
            }
            else if (strcmp(key, "flags") == 0 ||
                     strcmp(key, "color") == 0 ||
                     strcmp(key, "proc") == 0) {
                /* Best-effort stringy fields. proc / color may be present
                 * in some extractors; flags is informational here. Any of
                 * them can be null (SEARCHLIGHT etc. has flags: null). */
                char buf[40];
                if (jr_str(r, buf, sizeof(buf))) {
                    if (strcmp(key, "color") == 0) a->color = parse_color(buf);
                    else if (strcmp(key, "proc") == 0)
                        strncpy(a->proc, buf, sizeof(a->proc) - 1);
                } else {
                    jr_skip_value(r);    /* null / array / number → skip */
                }
            }
            else if (strcmp(key, "options") == 0) {
                parse_actor_options(r, a);
            }
            else if (strcmp(key, "source") == 0) {
                /* "scenerio" or "demo" — drives the Demo tab filter +
                 * the demo-actor tint in the 3D pane. */
                char buf[16];
                if (jr_str(r, buf, sizeof(buf))) {
                    a->from_demo = (strcmp(buf, "demo") == 0) ? 1 : 0;
                } else jr_skip_value(r);
            }
            else {
                /* non_spatial, pos_option, etc. — not yet used. */
                jr_skip_value(r);
            }
        } while (jr_eat(r, ','));
        if (!jr_eat(r, '}')) r->err = 1;
    }

    /* Second-pass capture for string-symbol options. */
    const char *obj_end = r->p;
    if (obj_begin < obj_end) {
        capture_string_option(obj_begin, obj_end, "e",
                              a->event_proc, sizeof(a->event_proc));
        capture_string_option(obj_begin, obj_end, "y",
                              a->init_proc, sizeof(a->init_proc));
        capture_string_option(obj_begin, obj_end, "m",
                              a->message, sizeof(a->message));
    }

    g_actor_count++;
}

/* JSON top-level array. Returns 0 on success, -1 on parse error. */
static int load_json_buffer(const char *buf, size_t len)
{
    JR r = { buf, buf + len, 0 };
    if (!jr_eat(&r, '[')) {
        printf("editor: JSON: expected '[' at start\n");
        return -1;
    }
    if (jr_eat(&r, ']')) return 0;
    do {
        const char *obj_start = r.p;
        parse_actor_object(&r);
        if (r.err) {
            int off = (int)(r.p - buf);
            int near_off = (int)(obj_start - buf);
            char snippet[80];
            int n = (int)(r.end - r.p);
            if (n > 60) n = 60;
            memcpy(snippet, r.p, n);
            snippet[n] = 0;
            printf("editor: JSON parse error at byte %d (actor #%d started at %d)\n"
                   "  near: %s\n", off, g_actor_count, near_off, snippet);
            return -1;
        }
    } while (jr_eat(&r, ','));
    if (!jr_eat(&r, ']')) return -1;
    return 0;
}

/* Slurp a file into a malloc'd buffer. Caller frees. NULL on error. */
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (out_len) *out_len = got;
    return buf;
}

void ed_actors_load(const char *path)
{
    free(g_actors);
    g_actors = NULL;
    g_actor_count = 0;
    g_actor_capacity = 0;

    /* Prefer JSON: it has the full options dict (vision range, item id,
     * event procs, etc.). Fall back to TSV for compat with data dirs that
     * haven't been regenerated by tools/extract_actors.py --batch.
     *
     * The caller passes either ".../foo_actors.tsv" or ".../foo_actors.json";
     * we coerce to a JSON path first, then fall back. */
    char json_path[256];
    snprintf(json_path, sizeof(json_path), "%s", path);
    size_t pl = strlen(json_path);
    /* Swap any trailing ".tsv" → ".json"; pass-through for ".json". */
    if (pl >= 4 && strcmp(json_path + pl - 4, ".tsv") == 0)
        snprintf(json_path + pl - 4, sizeof(json_path) - (pl - 4), ".json");

    int loaded_json = 0;
    {
        size_t blen = 0;
        char *buf = slurp(json_path, &blen);
        if (buf) {
            if (load_json_buffer(buf, blen) == 0) loaded_json = 1;
            else printf("editor: %s parse error — falling back to TSV\n", json_path);
            free(buf);
        }
    }

    if (!loaded_json) {
        /* TSV fallback path. */
        char tsv_path[256];
        snprintf(tsv_path, sizeof(tsv_path), "%s", path);
        size_t tl = strlen(tsv_path);
        if (tl >= 5 && strcmp(tsv_path + tl - 5, ".json") == 0)
            snprintf(tsv_path + tl - 5, sizeof(tsv_path) - (tl - 5), ".tsv");
        FILE *f = fopen(tsv_path, "r");
        if (!f) {
            printf("editor: no actor data at %s or %s — markers disabled\n",
                   json_path, tsv_path);
            return;
        }
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            strip_nl(line);
            if (!line[0]) continue;
            parse_tsv_line(line);
        }
        fclose(f);
    }

    /* Resolve actor → KMD pointers once now (the GV cache is populated by
       FS_LoadStageRequest before this is called). Without caching here,
       the per-frame "Show Models" loop would call GV_GetCache for every
       unmatched actor every frame and spam [cache] MISS in stdout. */
    int with_model = 0;
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        ed_actor_kmd_for_type(a->type, &a->kmd_def);
        /* ITEM actors render through KMD_BOX_01..08 by `-b b:N` (engine
         * does KMD_BOX_01 + type). Override the generic "item" fallback
         * with the right box variant so the editor preview matches what
         * appears in-game. Falls back to the type lookup if the box
         * variant isn't cached. */
        if (strcmp(a->type, "ITEM") == 0 &&
            a->box_type >= 0 && a->box_type < 8) {
            /* Engine maps -b b:N → KMD_BOX_01+N (N in 0..7). Anything
             * outside that window comes from a malformed `chara ITEM`
             * line and would resolve to nonsense in the cache. */
            char name[16];
            snprintf(name, sizeof(name), "box_%02d", a->box_type + 1);
            int id = GV_CacheID2(name, 'k');
            void *p = quiet_get_cache(id);
            /* Sanity-check: pointers from the cache should be real
             * heap addresses. Anything in the bottom 64 KiB or unaligned
             * is a stale/bogus tag — keep the type-fallback model
             * instead so we don't crash render_kmd_unlit. */
            if (p && (uintptr_t)p >= 0x10000 && ((uintptr_t)p & 3) == 0)
                a->kmd_def = p;
        }
        if (a->kmd_def) with_model++;
    }
    g_actors_dirty = 0;
    printf("editor: loaded %d actor markers from %s (%d with model)\n",
           g_actor_count, loaded_json ? json_path : path, with_model);
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

/* Public toggle for the vision-cone overlay. Defined in ed_ui.cpp via the
 * Actors-tab checkbox; consumed here per render frame. */
int g_show_vision_cones = 1;

/* Draw a horizontal vision wedge at ground height for the given actor.
 * Uses `-l` (vision range) and `-r` (heading byte) so any actor with both
 * fields populated gets a cone. Half-angle is hard-coded to 50° because
 * the engine doesn't expose the per-actor vision angle in a usable way
 * — it's good enough for editor visualisation. The wedge is a triangle:
 * apex at the actor, two arms forward-left/right, plus a chord segment
 * across the tips. */
static void draw_vision_cone(const EdActor *a, const unsigned char col[3])
{
    if (a->vision_range <= 0 || a->rot_b < 0) return;
    float angle = (float)a->rot_b * (6.2831853f / 256.0f);
    const float half_fov = 50.0f * (3.14159265f / 180.0f);  /* 50° half-angle */
    float al = angle - half_fov;
    float ar = angle + half_fov;
    float r  = (float)a->vision_range;
    int   ax = a->pos[0], ay = a->pos[1], az = a->pos[2];
    int   tl[3] = { ax + (int)(sinf(al) * r), ay, az + (int)(cosf(al) * r) };
    int   tr[3] = { ax + (int)(sinf(ar) * r), ay, az + (int)(cosf(ar) * r) };
    int   o[3], el[3], er[3];
    short cd = ed_render_clip_dist();
    ed_world_to_eye(ax, ay, az, o);
    ed_world_to_eye(tl[0], tl[1], tl[2], el);
    ed_world_to_eye(tr[0], tr[1], tr[2], er);
    /* Dimmer color so the cone doesn't drown out the marker. */
    unsigned char dim[3] = { (unsigned char)(col[0] * 5 / 8),
                             (unsigned char)(col[1] * 5 / 8),
                             (unsigned char)(col[2] * 5 / 8) };
    gl_submit_line3d(o,  el, dim, dim, cd);
    gl_submit_line3d(o,  er, dim, dim, cd);
    /* Arc as a few short segments between tl and tr (4 chord lines). */
    const int kArcN = 4;
    int prev[3] = { tl[0], tl[1], tl[2] };
    int prev_eye[3]; ed_world_to_eye(prev[0], prev[1], prev[2], prev_eye);
    for (int s = 1; s <= kArcN; s++) {
        float t = (float)s / (float)kArcN;
        float ang = al + (ar - al) * t;
        int p[3] = { ax + (int)(sinf(ang) * r), ay, az + (int)(cosf(ang) * r) };
        int pe[3]; ed_world_to_eye(p[0], p[1], p[2], pe);
        gl_submit_line3d(prev_eye, pe, dim, dim, cd);
        prev_eye[0] = pe[0]; prev_eye[1] = pe[1]; prev_eye[2] = pe[2];
    }
}

void ed_actors_render(void)
{
    for (int i = 0; i < g_actor_count; i++) {
        EdActor *a = &g_actors[i];
        if (!a->has_pos) continue;
        /* For ITEM actors, override the GCL-derived hash color with a
         * family color (green=consumable, blue=equip, yellow=key, ...) so
         * the overhead view tells categories apart at a glance. Other
         * actors keep their type-hash color from extract_actors. */
        uint32_t mark_color = a->color;
        if (strcmp(a->type, "ITEM") == 0 && a->item_id >= 0)
            mark_color = ed_item_color(a->item_id);
        unsigned char col[3] = {
            (unsigned char)((mark_color >> 16) & 0xFF),
            (unsigned char)((mark_color >> 8)  & 0xFF),
            (unsigned char)( mark_color        & 0xFF),
        };
        /* Demo (cutscene) actors get a desaturated tint so the Actors
         * pane / 3D view can tell scen vs. demo apart at a glance.
         * Pulls each channel toward 128 (mid-grey) by 40% to wash out the
         * type-hash color without losing it entirely. */
        if (a->from_demo) {
            for (int k = 0; k < 3; k++)
                col[k] = (unsigned char)(((int)col[k] * 6 + 128 * 4) / 10);
        }
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
        if (g_show_vision_cones) {
            draw_vision_cone(a, col);
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
    /* Mirrors tools/mgs_tools/common/strcode.py gv_strcode (5-bit rol + add). */
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

/* Compact in-game item-id catalog. Mirrors source/include/linkvar.h IT_*
 * (range 0..23 — IT_None=-1 means "missing -i"). The names are lowercase
 * one-word labels chosen to fit on the 3D-view marker overlay. */
static const struct {
    const char *name;
    uint32_t    rgb;     /* family color: green=consumable, blue=equip,
                            yellow=key, white=weapon-aux, gray=unknown */
} k_items[] = {
    { "cigs",     0x6B8FCC },  /* 0  */
    { "scope",    0x6B8FCC },  /* 1  */
    { "box1",     0xB07F50 },  /* 2  */
    { "box2",     0xB07F50 },  /* 3  */
    { "box3",     0xB07F50 },  /* 4  */
    { "nvg",      0x6B8FCC },  /* 5  */
    { "thermg",   0x6B8FCC },  /* 6  */
    { "gasmask",  0x6B8FCC },  /* 7  */
    { "armor",    0x6B8FCC },  /* 8  */
    { "ketchup",  0x4FA864 },  /* 9  */
    { "stealth",  0x6B8FCC },  /* 10 */
    { "bandana",  0x6B8FCC },  /* 11 */
    { "camera",   0x6B8FCC },  /* 12 */
    { "ration",   0x4FA864 },  /* 13 */
    { "coldmed",  0x4FA864 },  /* 14 */
    { "diazepam", 0x4FA864 },  /* 15 */
    { "palkey",   0xD5C03A },  /* 16 */
    { "card",     0xD5C03A },  /* 17 */
    { "timer",    0xCF6B4F },  /* 18 */
    { "minedet",  0x6B8FCC },  /* 19 */
    { "disk",     0xD5C03A },  /* 20 */
    { "rope",     0xD5C03A },  /* 21 */
    { "hkerchf",  0xD5C03A },  /* 22 */
    { "supress",  0xCF6B4F },  /* 23 */
};

const char *ed_item_name(int item_id)
{
    if (item_id < 0 || item_id >= (int)(sizeof(k_items)/sizeof(k_items[0])))
        return "?";
    return k_items[item_id].name;
}

uint32_t ed_item_color(int item_id)
{
    if (item_id < 0 || item_id >= (int)(sizeof(k_items)/sizeof(k_items[0])))
        return 0xC0C0C0;
    return k_items[item_id].rgb;
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
