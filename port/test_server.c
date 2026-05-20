/**
 * test_server.c — Unix domain socket server for autonomous game inspection.
 *
 * Protocol: newline-delimited JSON over /tmp/mgs_test.sock
 * Called once per frame from main.c (TEST_HARNESS_tick).
 * Single-threaded: all access to game state is synchronous, no locks needed.
 *
 * All public symbols are prefixed TEST_HARNESS_ per the separation rule.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <stdint.h>

#include "libgte.h"
#include "libgpu.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "libhzd/libhzd.h"
#include "game/map.h"

/* -------------------------------------------------------------------------
 * Game globals we need to inspect
 * ---------------------------------------------------------------------- */

extern int GV_Clock;
extern int GV_Time;
extern int GM_LoadComplete;
extern int GM_GameStatus;
extern int GM_AlertMode;
extern int GM_AlertLevel;
extern SVECTOR GM_PlayerPosition;
extern char   *GM_StageName;
/* Health lives in linkvarbuf[] — see source/include/linkvar.h */
extern short linkvarbuf[0x60];
#define _HEALTH     ((int)linkvarbuf[11])
#define _MAX_HEALTH ((int)linkvarbuf[12])
extern DG_CHANL DG_Chanls[3];
extern uint16_t vram[512][1024];

/* Exposed by libdg_stub.c */
extern int port_last_drawn_faces;

/* Actor list — mirrored from libgv (same layout as GV_ACT linked list) */
typedef void (*_ActFunc)(void *);
typedef void (*_FreeFunc)(void *);
typedef struct _ANode {
    struct _ANode *prev;
    struct _ANode *next;
    _ActFunc       act;
    _ActFunc       die;
    _FreeFunc      free_fn;
    const char    *filename;
    int            runtime;
    int            count;
} _ANode;

typedef struct {
    _ANode first;
    _ANode last;
    short  pause;
    short  kill;
} _AList;

extern _AList gActorsList_800ACC18[7];

/* MAP / HZD access — gMapRecs is a global defined in map.c (extern there too) */
extern MAP gMapRecs_800B7910[16];
/* gMapCount_800ABAA8 is static in map.c; use GM_IterHazard instead */
extern HZD_HDL *GM_IterHazard(HZD_HDL *cur);

/* -------------------------------------------------------------------------
 * Input override globals — read by port/mts/mts.c
 * ---------------------------------------------------------------------- */

int TEST_HARNESS_override_buttons = -1;
int TEST_HARNESS_override_frames  = 0;

/* Replay/record — implemented in mts.c */
extern void        TEST_HARNESS_replay_start(const char *path);
extern void        TEST_HARNESS_record_start(const char *path);
extern void        TEST_HARNESS_record_stop(void);
extern int         TEST_HARNESS_get_input_status(void);   /* 0=idle, 1=replay, 2=record */
extern const char *TEST_HARNESS_get_input_path(void);
extern int         TEST_HARNESS_processed_inputs;
extern int         TEST_HARNESS_total_inputs;

/* -------------------------------------------------------------------------
 * Server state
 * ---------------------------------------------------------------------- */

#define SOCK_PATH   "/tmp/mgs_test.sock"
#define BUF_INIT    (8 * 1024 * 1024)   /* 8 MB initial allocation */
#define BUF_MAX     (64 * 1024 * 1024)  /* 64 MB hard cap */
#define IN_SIZE     4096

static int  srv_fd   = -1;   /* listening socket */
static int  cli_fd   = -1;   /* connected client */
static int  paused   = 0;    /* if 1, block in tick until step/run */
static int  frames_to_run = 0;  /* countdown for "run N" command */

static char in_buf[IN_SIZE];
static int  in_len = 0;

static char *out_buf = NULL;   /* heap-allocated, grows as needed */
static int   out_len = 0;
static int   out_cap = 0;
static int   out_overflow = 0; /* set if we hit BUF_MAX */

/* -------------------------------------------------------------------------
 * Output buffer helpers
 * ---------------------------------------------------------------------- */

static void out_reset(void)
{
    out_len = 0;
    out_overflow = 0;
}

static void out_grow(int needed)
{
    if (out_overflow) return;
    int new_cap = out_cap;
    while (new_cap - out_len < needed)
        new_cap *= 2;
    if (new_cap > BUF_MAX) {
        out_overflow = 1;
        return;
    }
    if (new_cap != out_cap) {
        char *p = (char *)realloc(out_buf, new_cap);
        if (!p) { out_overflow = 1; return; }
        out_buf = p;
        out_cap = new_cap;
    }
}

static void out_append(const char *fmt, ...)
{
    if (out_overflow) return;
    va_list ap;
    int remaining = out_cap - out_len;
    va_start(ap, fmt);
    int written = vsnprintf(out_buf + out_len, remaining, fmt, ap);
    va_end(ap);
    if (written >= remaining) {
        /* Didn't fit — grow and retry */
        out_grow(written + 1);
        if (out_overflow) return;
        remaining = out_cap - out_len;
        va_start(ap, fmt);
        written = vsnprintf(out_buf + out_len, remaining, fmt, ap);
        va_end(ap);
    }
    if (written > 0 && written < out_cap - out_len)
        out_len += written;
}

static void out_send(void)
{
    if (cli_fd < 0 || out_len == 0) return;
    /* Ensure newline termination */
    if (out_buf[out_len - 1] != '\n') {
        if (out_len < out_cap - 1) {
            out_buf[out_len++] = '\n';
        }
    }
    int sent = 0;
    while (sent < out_len) {
        int n = (int)send(cli_fd, out_buf + sent, out_len - sent, 0);
        if (n <= 0) { cli_fd = -1; break; }
        sent += n;
    }
    out_reset();
}

/* -------------------------------------------------------------------------
 * JSON value helpers (pull simple fields from a command line)
 * ---------------------------------------------------------------------- */

/* Extract integer after "key": in buf. Returns defval if not found. */
static long json_get_int(const char *buf, const char *key, long defval)
{
    const char *p = strstr(buf, key);
    if (!p) return defval;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    long v;
    if (sscanf(p, "%ld", &v) == 1) return v;
    return defval;
}

/* Extract string after "key": into dest (max dstlen). Returns 1 on success. */
static int json_get_str(const char *buf, const char *key, char *dest, int dstlen)
{
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < dstlen - 1)
            dest[i++] = *p++;
        dest[i] = '\0';
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Command handlers
 * ---------------------------------------------------------------------- */

static void cmd_status(void)
{
    out_append("{\"ok\":true,\"frame\":%d,\"load_complete\":%d,\"gv_time\":%d}\n",
               GV_Time, GM_LoadComplete, GV_Time);
}

static void cmd_run(int n)
{
    if (n < 1) n = 1;
    frames_to_run = n;
    paused = 0;
    /* Response sent AFTER the frames run, at re-entry into tick */
}

static void cmd_step(void)
{
    frames_to_run = 1;
    paused = 0;
}

static void cmd_pause(void)
{
    paused = 1;
    frames_to_run = 0;
    out_append("{\"ok\":true}\n");
    out_send();
}

static void cmd_resume(void)
{
    paused = 0;
    out_append("{\"ok\":true}\n");
    out_send();
}

static void cmd_inject_input(long buttons, long frames)
{
    if (frames < 1) frames = 1;
    TEST_HARNESS_override_buttons = (int)buttons;
    TEST_HARNESS_override_frames  = (int)frames;
    out_append("{\"ok\":true}\n");
    out_send();
}

static void cmd_replay_log(const char *path)
{
    TEST_HARNESS_replay_start(path);
    out_append("{\"ok\":true,\"status\":\"replaying\"}\n");
    out_send();
}

static void cmd_record_log(const char *path)
{
    TEST_HARNESS_record_start(path);
    out_append("{\"ok\":true,\"status\":\"recording\"}\n");
    out_send();
}

static void cmd_stop_log(void)
{
    TEST_HARNESS_record_stop();
    out_append("{\"ok\":true}\n");
    out_send();
}

static void cmd_get_state(void)
{
    DG_CHANL *chanl = &DG_Chanls[1];
    MATRIX *eye = &chanl->eye_inv;

    /* Camera position is stored in eye.t (translation of eye matrix, NOT eye_inv) */
    MATRIX *cam = &chanl->eye;

    out_append("{\"ok\":true");
    out_append(",\"frame\":%d", GV_Time);

    /* Stage name */
    {
        const char *sname = (GM_StageName && GM_StageName[0]) ? GM_StageName : "unknown";
        out_append(",\"stage\":\"%s\"", sname);
    }

    out_append(",\"load_complete\":%d", GM_LoadComplete);
    out_append(",\"game_status\":%d", GM_GameStatus);
    out_append(",\"alert_mode\":%d", GM_AlertMode);
    out_append(",\"alert_level\":%d", GM_AlertLevel);

    /* Snake */
    out_append(",\"snake\":{\"pos\":[%d,%d,%d],\"health\":%d,\"max_health\":%d}",
               GM_PlayerPosition.vx, GM_PlayerPosition.vy, GM_PlayerPosition.vz,
               _HEALTH, _MAX_HEALTH);

    /* Camera: eye.t holds world-space eye position; eye_inv is the view matrix */
    out_append(",\"camera\":{");
    out_append("\"pos\":[%d,%d,%d]", cam->t[0], cam->t[1], cam->t[2]);
    out_append(",\"eye_inv\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]]",
               eye->m[0][0], eye->m[0][1], eye->m[0][2],
               eye->m[1][0], eye->m[1][1], eye->m[1][2],
               eye->m[2][0], eye->m[2][1], eye->m[2][2]);
    out_append(",\"eye_inv_t\":[%d,%d,%d]", eye->t[0], eye->t[1], eye->t[2]);
    out_append(",\"clip_dist\":%d}", chanl->clip_distance);

    out_append(",\"objs_count\":%d", chanl->objs_index);
    out_append(",\"faces_last_frame\":%d", port_last_drawn_faces);

    /* Replay/record status */
    {
        static const char *status_names[] = {"idle", "replaying", "recording"};
        int st = TEST_HARNESS_get_input_status();
        const char *path = TEST_HARNESS_get_input_path();
        out_append(",\"replay_status\":{\"status\":\"%s\",\"path\":%s%s%s"
                   ",\"processed_inputs\":%d,\"total_inputs\":%d}",
                   status_names[st],
                   (path && path[0]) ? "\"" : "",
                   (path && path[0]) ? path  : "null",
                   (path && path[0]) ? "\"" : "",
                   TEST_HARNESS_processed_inputs,
                   TEST_HARNESS_total_inputs);
    }

    out_append("}\n");
    out_send();
}

static void cmd_get_actors(void)
{
    static const char *level_names[] = {
        "DAEMON","MANAGER","LEVEL2","LEVEL3","LEVEL4","LEVEL5","DAEMON2"
    };

    out_append("{\"ok\":true,\"actors\":[");
    int first = 1;
    for (int lv = 0; lv < 7; lv++) {
        _AList *list = &gActorsList_800ACC18[lv];
        _ANode *head = &list->first;
        _ANode *cur  = head->next;
        int idx = 0;
        while (cur && cur != &list->last && idx < 256) {
            if (!first) out_append(",");
            first = 0;
            const char *name = (cur->filename) ? cur->filename : "???";
            out_append("{\"priority\":%d,\"level\":\"%s\",\"name\":\"%s\","
                       "\"active\":%d,\"count\":%d,\"runtime\":%d}",
                       lv, level_names[lv], name,
                       (cur->act != NULL) ? 1 : 0,
                       cur->count, cur->runtime);
            cur = cur->next;
            idx++;
        }
    }
    out_append("]}\n");
    out_send();
}

static void cmd_get_mesh(int simplified)
{
    DG_CHANL *chanl = &DG_Chanls[1];
    out_append("{\"ok\":true,\"simplified\":%d,\"objects\":[", simplified ? 1 : 0);

    int first_obj = 1;
    for (int qi = 0; qi < chanl->objs_index; qi++) {
        DG_OBJS *objs = chanl->queue[qi];
        if (!objs || !objs->def || !objs->objs) continue;
        if (objs->def->n_models <= 0 || objs->def->n_models > 256) continue;

        if (!first_obj) out_append(",");
        first_obj = 0;

        out_append("{\"world_t\":[%d,%d,%d]",
                   objs->world.t[0], objs->world.t[1], objs->world.t[2]);
        out_append(",\"flag\":\"0x%X\"", objs->flag);
        out_append(",\"group_id\":%d", objs->group_id);
        out_append(",\"n_models\":%d", objs->def->n_models);
        out_append(",\"models\":[");

        DG_OBJ *obj = objs->objs;
        int first_mdl = 1;
        for (int mi = 0; mi < objs->def->n_models; mi++, obj++) {
            if (!obj->model) continue;
            DG_MDL *mdl = obj->model;

            if (!first_mdl) out_append(",");
            first_mdl = 0;

            out_append("{\"n_verts\":%d,\"n_faces\":%d", mdl->n_verts, mdl->n_faces);
            out_append(",\"min\":[%d,%d,%d],\"max\":[%d,%d,%d]",
                       mdl->min.vx, mdl->min.vy, mdl->min.vz,
                       mdl->max.vx, mdl->max.vy, mdl->max.vz);
            out_append(",\"obj_world_t\":[%d,%d,%d]",
                       obj->world.t[0], obj->world.t[1], obj->world.t[2]);

            if (!simplified && mdl->vertices && mdl->n_verts > 0 && mdl->n_verts <= 4096) {
                out_append(",\"vertices\":[");
                for (int vi = 0; vi < mdl->n_verts; vi++) {
                    if (vi) out_append(",");
                    out_append("[%d,%d,%d]",
                               mdl->vertices[vi].vx,
                               mdl->vertices[vi].vy,
                               mdl->vertices[vi].vz);
                }
                out_append("]");
            }

            if (!simplified && mdl->vindices && mdl->n_faces > 0 && mdl->n_faces <= 8192) {
                out_append(",\"faces\":[");
                unsigned int *vi_arr = (unsigned int *)mdl->vindices;
                for (int fi = 0; fi < mdl->n_faces; fi++) {
                    if (fi) out_append(",");
                    unsigned int vi = vi_arr[fi];
                    out_append("[%d,%d,%d,%d]",
                               (vi >> 0)  & 0x7F,
                               (vi >> 8)  & 0x7F,
                               (vi >> 16) & 0x7F,
                               (vi >> 24) & 0x7F);
                }
                out_append("]");
            }
            out_append("}");
        }
        out_append("]}");
    }
    out_append("]}\n");
    if (out_overflow) {
        out_reset();
        out_append("{\"ok\":false,\"error\":\"mesh data exceeded buffer limit\"}\n");
    }
    out_send();
}

static void cmd_get_collision(void)
{
    out_append("{\"ok\":true,\"maps\":[");
    int first_map = 1;

    HZD_HDL *hdl = GM_IterHazard(NULL);
    while (hdl) {
        if (!hdl->header) { hdl = GM_IterHazard(hdl); continue; }

        if (!first_map) out_append(",");
        first_map = 0;

        HZD_MAP *hzm = hdl->header;
        HZD_GRP *grp = hdl->group;
        if (!grp) { hdl = GM_IterHazard(hdl); continue; }

        out_append("{\"n_groups\":%d", hzm->n_groups);
        out_append(",\"bounds\":[[%d,%d],[%d,%d]]",
                   hzm->min_x, hzm->min_y, hzm->max_x, hzm->max_y);

        /* Emit the single active group (hdl->group points to current group) */
        out_append(",\"walls\":[");
        int fw = 1;
        for (int wi = 0; wi < grp->n_walls; wi++) {
            if (!fw) out_append(",");
            fw = 0;
            HZD_SEG *s = &grp->walls[wi];
            out_append("{\"p1\":[%d,%d],\"p2\":[%d,%d]}",
                       s->p1.x, s->p1.z, s->p2.x, s->p2.z);
        }
        out_append("]");

        out_append(",\"floors\":[");
        int ff = 1;
        for (int fi = 0; fi < grp->n_floors; fi++) {
            if (!ff) out_append(",");
            ff = 0;
            HZD_FLR *f = &grp->floors[fi];
            out_append("{\"corners\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]]}",
                       f->p1.x, f->p1.y, f->p1.z,
                       f->p2.x, f->p2.y, f->p2.z,
                       f->p3.x, f->p3.y, f->p3.z,
                       f->p4.x, f->p4.y, f->p4.z);
        }
        out_append("]");

        out_append(",\"triggers\":[");
        int ft = 1;
        for (int ti = 0; ti < grp->n_triggers; ti++) {
            if (!ft) out_append(",");
            ft = 0;
            HZD_TRG *t = &grp->triggers[ti];
            /* Use the trap union member for name/bounds */
            char safe_name[16];
            memset(safe_name, 0, sizeof(safe_name));
            memcpy(safe_name, t->trap.name, sizeof(t->trap.name));
            /* Sanitize: replace non-printable with '?' */
            for (int ci = 0; ci < (int)sizeof(safe_name) - 1; ci++) {
                char c = safe_name[ci];
                if (c == '\0') break;
                if (c < 0x20 || c > 0x7E) safe_name[ci] = '?';
            }
            out_append("{\"name\":\"%s\",\"b1\":[%d,%d],\"b2\":[%d,%d]}",
                       safe_name,
                       t->trap.b1.x, t->trap.b1.z,
                       t->trap.b2.x, t->trap.b2.z);
        }
        out_append("]");

        out_append("}");
        hdl = GM_IterHazard(hdl);
    }
    out_append("]}\n");
    if (out_overflow) {
        out_reset();
        out_append("{\"ok\":false,\"error\":\"collision data exceeded buffer limit\"}\n");
    }
    out_send();
}

static void cmd_screenshot(const char *path)
{
    if (!path || !path[0]) {
        out_append("{\"ok\":false,\"error\":\"no path\"}\n");
        out_send();
        return;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        out_append("{\"ok\":false,\"error\":\"cannot open file\"}\n");
        out_send();
        return;
    }

    /* Write a minimal BMP: 320x224 24-bit RGB */
    const int W = 320, H = 224;
    const int row_bytes = W * 3;
    /* BMP rows must be padded to 4-byte boundary */
    const int pad = (4 - (row_bytes % 4)) % 4;
    const int padded_row = row_bytes + pad;
    const int pixel_data_size = padded_row * H;
    const int file_size = 54 + pixel_data_size;

    /* BMP file header (14 bytes) */
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF;
    hdr[3] = (file_size >> 8)  & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF;
    hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;  /* pixel data offset */

    /* DIB header (40 bytes) */
    hdr[14] = 40;   /* header size */
    hdr[18] = W & 0xFF; hdr[19] = (W >> 8) & 0xFF;
    /* Height is negative for top-down (easier to write in scan order) */
    int neg_h = -H;
    hdr[22] = neg_h & 0xFF; hdr[23] = (neg_h >> 8) & 0xFF;
    hdr[24] = (neg_h >> 16) & 0xFF; hdr[25] = (neg_h >> 24) & 0xFF;
    hdr[26] = 1;    /* color planes */
    hdr[28] = 24;   /* bits per pixel */
    hdr[34] = pixel_data_size & 0xFF;
    hdr[35] = (pixel_data_size >> 8) & 0xFF;
    hdr[36] = (pixel_data_size >> 16) & 0xFF;
    hdr[37] = (pixel_data_size >> 24) & 0xFF;

    fwrite(hdr, 1, 54, fp);

    unsigned char row_buf[320 * 3 + 4];
    memset(row_buf, 0, sizeof(row_buf));

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t px = vram[y][x];
            /* PSX 15-bit BGR1555: bits [4:0]=R, [9:5]=G, [14:10]=B */
            int r = ((px >> 0) & 0x1F) << 3;
            int g = ((px >> 5) & 0x1F) << 3;
            int b = ((px >> 10) & 0x1F) << 3;
            /* BMP stores BGR */
            row_buf[x * 3 + 0] = (unsigned char)b;
            row_buf[x * 3 + 1] = (unsigned char)g;
            row_buf[x * 3 + 2] = (unsigned char)r;
        }
        fwrite(row_buf, 1, padded_row, fp);
    }
    fclose(fp);

    out_append("{\"ok\":true,\"path\":\"%s\"}\n", path);
    out_send();
}

static void cmd_peek(const char *addr_str, long size)
{
    if (!addr_str || !addr_str[0]) {
        out_append("{\"ok\":false,\"error\":\"no addr\"}\n");
        out_send();
        return;
    }
    if (size < 1 || size > 4096) {
        out_append("{\"ok\":false,\"error\":\"size out of range (1-4096)\"}\n");
        out_send();
        return;
    }

    unsigned long addr = strtoul(addr_str, NULL, 16);
    const unsigned char *p = (const unsigned char *)(uintptr_t)addr;

    out_append("{\"ok\":true,\"addr\":\"0x%lX\",\"size\":%ld,\"data\":\"", addr, size);
    for (long i = 0; i < size; i++) {
        out_append("%02X", p[i]);
    }
    out_append("\"}\n");
    out_send();
}

static void cmd_poke(const char *addr_str, const char *data_hex)
{
    if (!addr_str || !addr_str[0] || !data_hex || !data_hex[0]) {
        out_append("{\"ok\":false,\"error\":\"missing addr or data\"}\n");
        out_send();
        return;
    }

    unsigned long addr = strtoul(addr_str, NULL, 16);
    unsigned char *p = (unsigned char *)(uintptr_t)addr;

    long n = 0;
    const char *h = data_hex;
    while (h[0] && h[1] && n < 4096) {
        char byte_str[3] = { h[0], h[1], '\0' };
        p[n] = (unsigned char)strtoul(byte_str, NULL, 16);
        h += 2;
        n++;
    }

    out_append("{\"ok\":true,\"bytes_written\":%ld}\n", n);
    out_send();
}

/* -------------------------------------------------------------------------
 * Command dispatch
 * ---------------------------------------------------------------------- */

static void dispatch(const char *line)
{
    /* Extract "cmd" value */
    char cmd[64] = {0};
    json_get_str(line, "\"cmd\"", cmd, sizeof(cmd));

    if (strcmp(cmd, "status") == 0) {
        cmd_status();
        out_send();
    } else if (strcmp(cmd, "step") == 0) {
        cmd_step();
        /* Response sent after frame runs */
    } else if (strcmp(cmd, "step_for_frames") == 0) {
        /* Alias for run — same semantics */
        long n = json_get_int(line, "\"frames\"", 1);
        cmd_run((int)n);
        /* Response sent after frames run */
    } else if (strcmp(cmd, "run") == 0) {
        long n = json_get_int(line, "\"frames\"", 1);
        cmd_run((int)n);
        /* Response sent after frames run */
    } else if (strcmp(cmd, "pause") == 0) {
        cmd_pause();
    } else if (strcmp(cmd, "resume") == 0) {
        cmd_resume();
    } else if (strcmp(cmd, "quit") == 0) {
        out_append("{\"ok\":true}\n");
        out_send();
        if (cli_fd >= 0) { close(cli_fd); cli_fd = -1; }
        extern int g_running;
        g_running = 0;
    } else if (strcmp(cmd, "inject_input") == 0) {
        long buttons = json_get_int(line, "\"buttons\"", 0);
        long frames  = json_get_int(line, "\"frames\"",  1);
        cmd_inject_input(buttons, frames);
    } else if (strcmp(cmd, "replay_log") == 0) {
        char path[256] = {0};
        json_get_str(line, "\"path\"", path, sizeof(path));
        cmd_replay_log(path);
    } else if (strcmp(cmd, "record_log") == 0) {
        char path[256] = {0};
        json_get_str(line, "\"path\"", path, sizeof(path));
        cmd_record_log(path);
    } else if (strcmp(cmd, "stop_log") == 0) {
        cmd_stop_log();
    } else if (strcmp(cmd, "get_state") == 0) {
        cmd_get_state();
    } else if (strcmp(cmd, "get_actors") == 0) {
        cmd_get_actors();
    } else if (strcmp(cmd, "get_mesh") == 0) {
        long simplified = json_get_int(line, "\"simplified\"", 0);
        cmd_get_mesh((int)simplified);
    } else if (strcmp(cmd, "get_collision") == 0) {
        cmd_get_collision();
    } else if (strcmp(cmd, "screenshot") == 0) {
        char path[256] = {0};
        json_get_str(line, "\"path\"", path, sizeof(path));
        cmd_screenshot(path);
    } else if (strcmp(cmd, "peek") == 0) {
        char addr[32] = {0};
        long size = json_get_int(line, "\"size\"", 16);
        json_get_str(line, "\"addr\"", addr, sizeof(addr));
        cmd_peek(addr, size);
    } else if (strcmp(cmd, "poke") == 0) {
        char addr[32] = {0};
        char data[8192] = {0};
        json_get_str(line, "\"addr\"", addr, sizeof(addr));
        json_get_str(line, "\"data\"", data, sizeof(data));
        cmd_poke(addr, data);
    } else {
        out_append("{\"ok\":false,\"error\":\"unknown command: %s\"}\n", cmd);
        out_send();
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void TEST_HARNESS_init(void)
{
    out_buf = (char *)malloc(BUF_INIT);
    if (!out_buf) { fprintf(stderr, "[test] malloc failed\n"); return; }
    out_cap = BUF_INIT;
    out_len = 0;

    /* Remove stale socket */
    unlink(SOCK_PATH);

    srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("[test] socket"); return; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[test] bind"); close(srv_fd); srv_fd = -1; return;
    }
    if (listen(srv_fd, 1) < 0) {
        perror("[test] listen"); close(srv_fd); srv_fd = -1; return;
    }

    /* Non-blocking accept */
    fcntl(srv_fd, F_SETFL, O_NONBLOCK);

    printf("[test] listening on %s\n", SOCK_PATH);
}

void TEST_HARNESS_tick(void)
{
    if (srv_fd < 0) return;

    /* If we still have frames to run, just decrement and return */
    if (frames_to_run > 0) {
        frames_to_run--;
        if (frames_to_run == 0) {
            /* Done running — send response and enter wait */
            out_append("{\"ok\":true,\"frame\":%d,\"load_complete\":%d}\n",
                       GV_Time, GM_LoadComplete);
            out_send();
            paused = 1;
        }
        return;
    }

    /* If not paused and no client, do a quick non-blocking accept and return */
    if (!paused && cli_fd < 0) {
        cli_fd = accept(srv_fd, NULL, NULL);
        if (cli_fd < 0) { cli_fd = -1; return; }
        fcntl(cli_fd, F_SETFL, O_NONBLOCK);
        printf("[test] client connected\n");
    }

    if (cli_fd < 0 && !paused) return;

    /* Process loop: if paused, block; if not paused, do one non-blocking read */
    for (;;) {
        /* Accept new connection if none */
        if (cli_fd < 0) {
            struct pollfd pfd = { srv_fd, POLLIN, 0 };
            if (poll(&pfd, 1, paused ? 50 : 0) > 0) {
                cli_fd = accept(srv_fd, NULL, NULL);
                if (cli_fd >= 0) {
                    fcntl(cli_fd, F_SETFL, O_NONBLOCK);
                    printf("[test] client connected\n");
                } else {
                    cli_fd = -1;
                }
            }
            if (cli_fd < 0) {
                if (paused) continue; else break;
            }
        }

        /* Try to read data from client */
        {
            struct pollfd pfd = { cli_fd, POLLIN, 0 };
            int ready = poll(&pfd, 1, paused ? 50 : 0);

            if (ready > 0) {
                int room = IN_SIZE - in_len - 1;
                int n = (int)recv(cli_fd, in_buf + in_len, room, 0);
                if (n <= 0) {
                    /* Client disconnected */
                    close(cli_fd); cli_fd = -1;
                    in_len = 0;
                    if (!paused) break;
                    continue;
                }
                in_len += n;
                in_buf[in_len] = '\0';

                /* Process complete lines */
                char *p = in_buf;
                char *nl;
                while ((nl = memchr(p, '\n', in_buf + in_len - p)) != NULL) {
                    *nl = '\0';
                    if (nl > p) {
                        /* Strip CR if present */
                        if (*(nl - 1) == '\r') *(nl - 1) = '\0';
                        dispatch(p);

                        /* After run/step dispatch, frames_to_run > 0: exit tick */
                        if (frames_to_run > 0) {
                            /* Shift remaining input */
                            int consumed = (int)((nl + 1) - in_buf);
                            int remaining = in_len - consumed;
                            if (remaining > 0)
                                memmove(in_buf, nl + 1, remaining);
                            in_len = remaining;
                            paused = 0;
                            return;  /* Let the game run a frame */
                        }
                    }
                    p = nl + 1;
                }
                /* Shift unconsumed data to front */
                int consumed = (int)(p - in_buf);
                int remaining = in_len - consumed;
                if (remaining > 0 && consumed > 0)
                    memmove(in_buf, p, remaining);
                in_len = remaining;

            } else if (ready < 0) {
                /* Error */
                close(cli_fd); cli_fd = -1;
                in_len = 0;
                if (!paused) break;
            }
        }

        /* If not paused, do a single pass and return */
        if (!paused) break;
        /* If paused, loop back and wait for next command */
    }
}
