/* Editor content browser — Browser tab in the editor inspector.
 *
 * Two roots in a single tree:
 *   1. "ISO:/MGS" — files inside the disc image opened by libfs
 *      (port_iso_get). Lazy-listed per-directory via iso_list_directory.
 *   2. "Files:" — the on-disk asset trees the editor uses for custom
 *      stages: port/editor/assets, port/editor/extra_stages.
 *
 * Selecting a leaf populates the right pane with a preview. First
 * milestone implements TEXTURE previews — PNG / JPG / BMP / PCX / TGA
 * via stb_image, plus a minimal PSX .TIM decoder. KMD model and audio
 * previews can be added by extending preview_load / preview_draw_*.
 *
 * The browser intentionally never mutates the disc image or the asset
 * dirs — it's a read-only inspector. Caches one decoded texture at a
 * time (uploaded to GL); switches discard the previous handle. */

#include "imgui.h"
#include <SDL.h>
#ifdef __APPLE__
#  include <OpenGL/gl3.h>
#else
#  include <GL/glcorearb.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO   /* we feed memory buffers only */
#include "../third_party/stb_image.h"

extern "C" {
#include "../libfs/iso_reader.h"
IsoImage *port_iso_get(void);
const char *strcode_to_string(uint16_t code);
}

/* ------------------------------------------------------------------ */
/* Lazy directory tree                                                */
/* ------------------------------------------------------------------ */

/* A node is either:
 *   - A directory on disk         (is_dir, !is_iso, !is_blob)
 *   - A file on disk              (!is_dir, !is_iso, !is_blob)
 *   - A directory in the ISO      (is_dir, is_iso, !is_blob)
 *   - A file in the ISO           (!is_dir, is_iso, !is_blob)
 *   - A virtual archive directory (is_dir, is_blob) — synthetic
 *     children parsed from inside another file
 *   - A virtual blob              (!is_dir, is_blob) — a byte slice of
 *     `host_iso` starting at byte_offset for `size` bytes.
 *
 * `archive_kind` tells the lazy loader which TOC parser to use when the
 * user expands a node. */
enum ArchiveKind {
    AK_NONE     = 0,
    AK_STAGEDIR = 1,    /* STAGE.DIR — array of (name[8], sec_offset) */
    AK_DATACNF  = 2,    /* per-stage DATACNF block */
    AK_DAR      = 3,    /* concatenated DARFILE_TAG entries (textures live here) */
};

struct Node {
    std::string name;       /* display name */
    std::string full;       /* unique tree path; ISO path or FS path; for
                             * blobs a synthetic "host_iso/sub/name" string */
    bool is_dir = false;
    bool is_iso = false;    /* lives inside the disc image */
    bool is_blob = false;   /* is a slice of host_iso, not a real disc file */
    bool loaded = false;    /* children already populated */
    long size = 0;          /* size in bytes (file or blob length) */
    /* Blob-only: */
    std::string host_iso;   /* ISO path of the file we read bytes from */
    long byte_offset = 0;   /* start of this blob inside host_iso */
    ArchiveKind archive_kind = AK_NONE;
    std::vector<Node> children;
};

static Node s_root_iso;
static Node s_root_fs;
static bool s_inited = false;

static void node_sort(Node &n)
{
    std::sort(n.children.begin(), n.children.end(),
              [](const Node &a, const Node &b) {
                  if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
                  return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
              });
}

/* Identify a known archive format by filename, so the lazy loader knows
 * which TOC parser to call on expand. */
static ArchiveKind detect_archive(const std::string &iso_path)
{
    const char *slash = strrchr(iso_path.c_str(), '/');
    const char *base = slash ? slash + 1 : iso_path.c_str();
    if (!strcasecmp(base, "STAGE.DIR")) return AK_STAGEDIR;
    return AK_NONE;
}

static void load_iso_children(Node &n)
{
    if (n.loaded) return;
    n.loaded = true;
    IsoImage *iso = port_iso_get();
    if (!iso) return;
    int cnt = 0;
    IsoDirEntry *ents = iso_list_directory(iso, n.full.c_str(), &cnt);
    if (!ents) return;
    n.children.reserve(cnt);
    for (int i = 0; i < cnt; i++) {
        Node c;
        c.name = ents[i].name;
        c.full = n.full.empty() ? ents[i].name : (n.full + "/" + ents[i].name);
        c.is_dir = ents[i].is_dir != 0;
        c.is_iso = true;
        c.size = ents[i].size;
        /* Mark recognised archive files as expandable directories so
         * the user can drill into their contents like a real folder. */
        if (!c.is_dir) {
            ArchiveKind ak = detect_archive(c.full);
            if (ak != AK_NONE) {
                c.is_dir = true;
                c.is_blob = true;       /* contents are blobs of itself */
                c.host_iso = c.full;
                c.byte_offset = 0;
                c.archive_kind = ak;
            }
        }
        n.children.push_back(std::move(c));
    }
    free(ents);
    node_sort(n);
}

static void load_fs_children(Node &n)
{
    if (n.loaded) return;
    n.loaded = true;
    DIR *d = opendir(n.full.c_str());
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        Node c;
        c.name = de->d_name;
        c.full = n.full + "/" + de->d_name;
        struct stat st;
        if (stat(c.full.c_str(), &st) == 0) {
            c.is_dir = S_ISDIR(st.st_mode);
            c.size = (long)st.st_size;
        }
        n.children.push_back(std::move(c));
    }
    closedir(d);
    node_sort(n);
}

static void ensure_inited()
{
    if (s_inited) return;
    s_inited = true;
    s_root_iso.name = "ISO:/";
    s_root_iso.full = "";
    s_root_iso.is_dir = true;
    s_root_iso.is_iso = true;

    s_root_fs.name = "Files:";
    s_root_fs.full = "";
    s_root_fs.is_dir = true;
    /* Asset roots — try a few prefixes so it works whether the editor
     * was launched from port/, port/editor/, or the repo root. */
    static const char *prefixes[]  = { "", "./", "../", "port/", "../port/" };
    static const char *suffixes[]  = {
        "editor/assets", "editor/extra_stages", "doc/demo",
        "assets", "extra_stages",      /* when CWD is port/editor itself */
    };
    for (const char *suf : suffixes) {
        for (const char *pre : prefixes) {
            std::string p = std::string(pre) + suf;
            struct stat st;
            if (stat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            /* De-dupe by full path. */
            bool dup = false;
            for (auto &c : s_root_fs.children) if (c.full == p) { dup = true; break; }
            if (dup) break;
            Node c;
            c.name = p;
            c.full = p;
            c.is_dir = true;
            s_root_fs.children.push_back(std::move(c));
            break;
        }
    }
    s_root_fs.loaded = true;
}

/* ------------------------------------------------------------------ */
/* Preview state                                                      */
/* ------------------------------------------------------------------ */

enum PreviewKind { PV_NONE, PV_TEXTURE, PV_HEX, PV_ERROR };

static PreviewKind s_pv_kind = PV_NONE;
static std::string s_pv_path;
static std::string s_pv_msg;            /* error / status text */
static GLuint      s_pv_tex = 0;
static int         s_pv_w   = 0;
static int         s_pv_h   = 0;
static long        s_pv_bytes = 0;
static std::string s_pv_format;         /* "PNG", "JPG", "TIM 16bpp", ... */
static std::vector<uint8_t> s_pv_hex;   /* first ~4KB of the file for the hex pane */

static void preview_clear()
{
    if (s_pv_tex) { glDeleteTextures(1, &s_pv_tex); s_pv_tex = 0; }
    s_pv_kind = PV_NONE;
    s_pv_w = s_pv_h = 0; s_pv_bytes = 0;
    s_pv_format.clear();
    s_pv_msg.clear();
    s_pv_hex.clear();
}

static void upload_rgba(const uint8_t *rgba, int w, int h)
{
    if (s_pv_tex) glDeleteTextures(1, &s_pv_tex);
    glGenTextures(1, &s_pv_tex);
    glBindTexture(GL_TEXTURE_2D, s_pv_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    s_pv_w = w; s_pv_h = h;
    s_pv_kind = PV_TEXTURE;
}

/* Minimal PSX .TIM decoder. Returns an RGBA8 buffer (caller frees) or
 * NULL on bad data. Handles 4bpp+CLUT, 8bpp+CLUT, 16bpp (XBGR1555),
 * and 24bpp. Doesn't validate every edge case — meant for preview. */
static uint8_t *decode_tim(const uint8_t *data, long size, int *out_w, int *out_h)
{
    if (size < 8) return NULL;
    uint32_t magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    if (magic != 0x10) return NULL;
    uint32_t flags = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                     ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    int mode  = flags & 7;        /* 0=4bpp 1=8bpp 2=16bpp 3=24bpp */
    int hasC  = (flags & 8) != 0;

    long pos = 8;
    const uint16_t *clut = NULL;
    int clut_w = 0, clut_h = 0;
    if (hasC) {
        if (pos + 12 > size) return NULL;
        long clut_len = (long)data[pos] | ((long)data[pos+1]<<8) |
                        ((long)data[pos+2]<<16) | ((long)data[pos+3]<<24);
        clut_w = data[pos+8] | (data[pos+9]<<8);
        clut_h = data[pos+10] | (data[pos+11]<<8);
        if (clut_len < 12 || pos + clut_len > size) return NULL;
        clut = (const uint16_t *)(data + pos + 12);
        pos += clut_len;
    }
    if (pos + 12 > size) return NULL;
    long pix_len = (long)data[pos] | ((long)data[pos+1]<<8) |
                   ((long)data[pos+2]<<16) | ((long)data[pos+3]<<24);
    int pix_w = data[pos+8] | (data[pos+9]<<8);     /* in 16-bit units */
    int pix_h = data[pos+10] | (data[pos+11]<<8);
    if (pix_len < 12 || pos + pix_len > size) return NULL;
    const uint16_t *pix = (const uint16_t *)(data + pos + 12);

    int w = pix_w * 2;          /* 16-bit units → 4bpp 4-px units */
    int h = pix_h;
    switch (mode) {
        case 0: w = pix_w * 4; break; /* 4bpp: 4 pixels per 16-bit word */
        case 1: w = pix_w * 2; break; /* 8bpp: 2 pixels per word */
        case 2: w = pix_w;     break; /* 16bpp: 1 per word */
        case 3: w = pix_w * 2 / 3; break; /* 24bpp: 1.5 pixels per word */
        default: return NULL;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return NULL;

    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!rgba) return NULL;

    auto psx16_to_rgba = [](uint16_t p, uint8_t out[4]) {
        out[0] = (uint8_t)(((p      ) & 0x1F) << 3);     /* R */
        out[1] = (uint8_t)(((p >>  5) & 0x1F) << 3);     /* G */
        out[2] = (uint8_t)(((p >> 10) & 0x1F) << 3);     /* B */
        /* STP bit: 0 = opaque black is transparent, 1 = opaque colour.
         * For preview just show everything as opaque except 0x0000. */
        out[3] = (p == 0) ? 0 : 255;
    };

    if (mode == 2) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                psx16_to_rgba(pix[y * pix_w + x], rgba + (y * w + x) * 4);
    } else if (mode == 1) {
        const uint8_t *src = (const uint8_t *)pix;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t idx = src[y * (pix_w * 2) + x];
                uint16_t c = clut ? clut[idx] : (idx | (idx << 5) | (idx << 10));
                psx16_to_rgba(c, rgba + (y * w + x) * 4);
            }
        }
    } else if (mode == 0) {
        const uint8_t *src = (const uint8_t *)pix;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t byte = src[y * (pix_w * 2) + (x >> 1)];
                uint8_t idx  = (x & 1) ? (byte >> 4) : (byte & 0x0F);
                uint16_t c = clut ? clut[idx] : (idx * 0x1111);
                psx16_to_rgba(c, rgba + (y * w + x) * 4);
            }
        }
    } else { /* 24bpp */
        const uint8_t *src = (const uint8_t *)pix;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                long o = (y * pix_w * 2 + x * 3);
                if (o + 2 >= pix_len - 12) { rgba[(y*w+x)*4+3] = 0; continue; }
                rgba[(y * w + x) * 4 + 0] = src[o + 0];
                rgba[(y * w + x) * 4 + 1] = src[o + 1];
                rgba[(y * w + x) * 4 + 2] = src[o + 2];
                rgba[(y * w + x) * 4 + 3] = 255;
            }
        }
    }

    *out_w = w; *out_h = h;
    return rgba;
}

static bool ext_is(const std::string &p, const char *e)
{
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    const char *got = p.c_str() + dot;
    for (; *got && *e; got++, e++)
        if (tolower((unsigned char)*got) != tolower((unsigned char)*e)) return false;
    return *got == 0 && *e == 0;
}

static bool is_texture_ext(const std::string &p)
{
    return ext_is(p, ".png") || ext_is(p, ".jpg") || ext_is(p, ".jpeg")
        || ext_is(p, ".bmp") || ext_is(p, ".tga") || ext_is(p, ".pcx")
        || ext_is(p, ".tim");
}

/* Read a node's payload — from disk file, ISO file, or a byte-slice of
 * an ISO host file. Returns a fresh malloc'd buffer; caller frees. */
static uint8_t *read_leaf(const Node &n, long *out_size)
{
    *out_size = 0;
    const long MAX = 64 * 1024 * 1024;

    if (n.is_blob) {
        /* Zero-size blobs are reference entries, not real files. Refuse
         * to read instead of guessing a window (was reading the cap of
         * 64 MB before, producing nonsense previews). */
        if (n.size <= 0) return NULL;
        IsoImage *iso = port_iso_get();
        if (!iso) return NULL;
        IsoFile f;
        if (iso_find_file(iso, n.host_iso.c_str(), &f) != 0) return NULL;
        long sz = n.size;
        if (sz > MAX) sz = MAX;
        uint8_t *buf = (uint8_t *)malloc(sz);
        if (!buf) return NULL;
        int got = iso_read_file(iso, &f, n.byte_offset, (int)sz, buf);
        if (got <= 0) { free(buf); return NULL; }
        *out_size = got;
        return buf;
    }
    if (n.is_iso) {
        IsoImage *iso = port_iso_get();
        if (!iso) return NULL;
        IsoFile f;
        if (iso_find_file(iso, n.full.c_str(), &f) != 0) return NULL;
        long cap = f.size;
        if (cap > MAX) cap = MAX;
        uint8_t *buf = (uint8_t *)malloc(cap);
        if (!buf) return NULL;
        int got = iso_read_file(iso, &f, 0, (int)cap, buf);
        if (got <= 0) { free(buf); return NULL; }
        *out_size = got;
        return buf;
    }
    FILE *fp = fopen(n.full.c_str(), "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    if (sz > MAX) sz = MAX;
    uint8_t *buf = (uint8_t *)malloc(sz > 0 ? sz : 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t r = sz > 0 ? fread(buf, 1, sz, fp) : 0;
    fclose(fp);
    *out_size = (long)r;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Archive parsers                                                    */
/* ------------------------------------------------------------------ */

/* Read a slice of an ISO file directly (host_iso, byte_offset, size).
 * Returns NULL on failure. Used by archive parsers that only need the
 * TOC, not the full payload. */
static uint8_t *read_iso_slice(const std::string &host, long off, long sz)
{
    if (sz <= 0) return NULL;
    IsoImage *iso = port_iso_get();
    if (!iso) return NULL;
    IsoFile f;
    if (iso_find_file(iso, host.c_str(), &f) != 0) return NULL;
    if (off >= f.size) return NULL;
    if (off + sz > f.size) sz = f.size - off;
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) return NULL;
    int got = iso_read_file(iso, &f, off, (int)sz, buf);
    if (got <= 0) { free(buf); return NULL; }
    return buf;
}

/* Map DATACNF tag.ext (single byte) → human extension. The engine uses
 * 1-char codes: 'k' = KMD, 'h' = HZD, 'g' = GCX, 'p' = PCX, etc. */
static const char *datacnf_ext_to_str(char ext)
{
    switch (ext) {
        case 'k': return "kmd";
        case 'h': return "hzd";
        case 'g': return "gcx";
        case 'p': return "pcx";
        case 't': return "tim";
        case 's': return "sfx";
        case 'd': return "dar";
        case 'b': return "bin";
        case 'm': return "mdl";
        case 'a': return "anm";
        default: {
            /* Safe single-char fallback. */
            static char b[2]; b[0] = (ext >= 0x20 && ext < 0x7F) ? ext : '?'; b[1] = 0;
            return b;
        }
    }
}

/* Parse a DATACNF sub-archive starting at host_iso[byte_offset]. Each
 * tagged entry becomes a child blob with byte_offset relative to the
 * full ISO file and size = tag.size. */
static void parse_datacnf(Node &n)
{
    uint8_t *hdr = read_iso_slice(n.host_iso, n.byte_offset, 2048);
    if (!hdr) {
        fprintf(stderr, "[browser] datacnf: read_iso_slice failed host='%s' off=%ld\n",
                n.host_iso.c_str(), n.byte_offset);
        return;
    }
    int version       = hdr[0];
    int total_sectors = (int)hdr[2] | ((int)hdr[3] << 8);
    fprintf(stderr, "[browser] datacnf '%s' @ off=%ld  version=%d sectors=%d\n",
            n.full.c_str(), n.byte_offset, version, total_sectors);
    fprintf(stderr, "[browser]   first 32 bytes:");
    for (int i = 0; i < 32; i++) fprintf(stderr, " %02x", hdr[i]);
    fprintf(stderr, "\n");

    /* Find the tag terminator (mode==0) so we know where the data
     * actually begins. */
    long tag_pos = 4;
    long scan = tag_pos;
    int  n_tags = 0;
    while (scan + 8 <= 2048) {
        uint8_t mode = hdr[scan + 2];
        if (mode == 0) { scan += 8; break; }
        scan += 8; n_tags++;
    }
    long data_off = (scan + 2047) & ~2047L;
    fprintf(stderr, "[browser]   walked %d tags, data starts at +%ld\n", n_tags, data_off);

    /* Walk every tag until mode==0. The engine treats size==0 entries
     * as legitimate markers (e.g. section separators, soft references),
     * so we must keep going past them — earlier code was bailing on the
     * first size==0 and missing 95 % of each stage's files. */
    long cursor = data_off;
    int  emitted = 0;
    int  slot    = 0;       /* dedupe suffix when two tags share a name */
    for (long i = tag_pos; i + 8 <= 2048; i += 8, slot++) {
        uint16_t id   = (uint16_t)hdr[i] | ((uint16_t)hdr[i + 1] << 8);
        uint8_t  mode = hdr[i + 2];
        char     ext  = (char)hdr[i + 3];
        int32_t  sz   = (int32_t)((uint32_t)hdr[i + 4] |
                                  ((uint32_t)hdr[i + 5] << 8) |
                                  ((uint32_t)hdr[i + 6] << 16) |
                                  ((uint32_t)hdr[i + 7] << 24));
        if (mode == 0) break;
        if (sz > 256 * 1024 * 1024) break;   /* obviously corrupt */
        if (sz < 0) sz = 0;
        /* Size-0 entries are not real files — they're cache REFERENCES
         * (mode='c'). The actual data lives inside one of the earlier
         * DAR archives; the engine resolves them by hash. Hide them
         * from the tree so the user only sees real, readable blobs. */
        if (sz == 0) continue;

        const char *resolved = strcode_to_string(id);
        char namebuf[96];
        if (resolved) snprintf(namebuf, sizeof namebuf, "%s.%s",
                               resolved, datacnf_ext_to_str(ext));
        else          snprintf(namebuf, sizeof namebuf, "0x%04x.%s",
                               id, datacnf_ext_to_str(ext));

        Node c;
        c.name = namebuf;
        char fullbuf[128];
        snprintf(fullbuf, sizeof fullbuf, "%s/[%02d]%s",
                 n.full.c_str(), slot, namebuf);
        c.full = fullbuf;
        c.is_blob = true;
        c.host_iso = n.host_iso;
        c.byte_offset = n.byte_offset + cursor;
        c.size = sz;
        /* `.dar` entries (ext == 'd', mode == 'r' or 'n') are themselves
         * archives — mark as expandable so the user can drill into the
         * TIM textures + sub-files they contain. */
        if (ext == 'd' && sz > 0) {
            c.is_dir = true;
            c.archive_kind = AK_DAR;
        }
        if (sz > 0) cursor += (sz + 2047) & ~2047L;
        n.children.push_back(std::move(c));
        emitted++;
    }
    fprintf(stderr, "[browser]   emitted %d children\n", emitted);
    free(hdr);
}

/* Parse STAGE.DIR header → stage directory entries pointing into
 * itself. Each entry becomes a sub-directory whose lazy load parses
 * the per-stage DATACNF block. */
static void parse_stagedir(Node &n)
{
    uint8_t *hdr = read_iso_slice(n.host_iso, 0, 4096);
    if (!hdr) {
        fprintf(stderr, "[browser] stagedir: read failed for '%s'\n",
                n.host_iso.c_str());
        return;
    }
    uint32_t tbl_size = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                        ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    fprintf(stderr, "[browser] stagedir '%s' tbl_size=%u\n",
            n.host_iso.c_str(), tbl_size);
    if (tbl_size > 4 * 1024 * 1024) { free(hdr); return; }

    /* If the header sector wasn't big enough, refetch. */
    long needed = 4 + tbl_size;
    if (needed > 4096) {
        free(hdr);
        hdr = read_iso_slice(n.host_iso, 0, needed);
        if (!hdr) return;
    }

    /* Each entry is char name[8] + i32 offset_sectors. The offset is
     * relative to the START of STAGE.DIR (we host on it). */
    long entry_count = tbl_size / 12;
    int emitted = 0;
    for (long i = 0; i < entry_count; i++) {
        const uint8_t *e = hdr + 4 + i * 12;
        if (e[0] == 0) continue;        /* empty slot */
        char name[9] = {0};
        for (int k = 0; k < 8; k++) name[k] = (char)e[k];
        name[8] = 0;
        /* Trim trailing spaces. */
        for (int k = 7; k >= 0 && (name[k] == ' ' || name[k] == 0); k--)
            name[k] = 0;
        int32_t off_sectors = (int32_t)((uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                                        ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24));
        if (off_sectors < 0) continue;

        Node c;
        c.name = name;
        c.full = n.full + "/" + name;
        c.is_dir = true;
        c.is_blob = true;
        c.host_iso = n.host_iso;
        c.byte_offset = (long)off_sectors * 2048;
        c.archive_kind = AK_DATACNF;
        n.children.push_back(std::move(c));
        emitted++;
    }
    fprintf(stderr, "[browser]   stagedir emitted %d stage entries\n", emitted);
    free(hdr);
}

/* DAR archive: a stream of DARFILE_TAG entries.
 *   u16 id   u16 ext   i32 size   [size bytes of data]
 * No outer header. Walks until we run out of bytes or hit a clearly
 * invalid record. Children get a sensible extension from `ext` (low
 * byte, usually a single ASCII char like 'k' for KMD or 't' for TIM). */
static void parse_dar(Node &n)
{
    if (n.size <= 0) return;
    uint8_t *buf = read_iso_slice(n.host_iso, n.byte_offset, n.size);
    if (!buf) {
        fprintf(stderr, "[browser] dar: read failed @ off=%ld size=%ld\n",
                n.byte_offset, n.size);
        return;
    }
    long pos = 0;
    int  slot = 0;
    int  emitted = 0;
    while (pos + 8 <= n.size) {
        uint16_t id  = (uint16_t)buf[pos] | ((uint16_t)buf[pos + 1] << 8);
        uint16_t ext = (uint16_t)buf[pos + 2] | ((uint16_t)buf[pos + 3] << 8);
        int32_t  sz  = (int32_t)((uint32_t)buf[pos + 4] |
                                 ((uint32_t)buf[pos + 5] << 8) |
                                 ((uint32_t)buf[pos + 6] << 16) |
                                 ((uint32_t)buf[pos + 7] << 24));
        if (sz < 0 || pos + 8 + sz > n.size) break;

        char extc = (char)(ext & 0xFF);
        const char *resolved = strcode_to_string(id);
        char namebuf[96];
        if (resolved) snprintf(namebuf, sizeof namebuf, "%s.%s",
                               resolved, datacnf_ext_to_str(extc));
        else          snprintf(namebuf, sizeof namebuf, "0x%04x.%s",
                               id, datacnf_ext_to_str(extc));

        Node c;
        c.name = namebuf;
        char fullbuf[160];
        snprintf(fullbuf, sizeof fullbuf, "%s/[%02d]%s",
                 n.full.c_str(), slot, namebuf);
        c.full = fullbuf;
        c.is_blob = true;
        c.host_iso = n.host_iso;
        c.byte_offset = n.byte_offset + pos + 8;    /* skip the 8-byte hdr */
        c.size = sz;
        n.children.push_back(std::move(c));
        emitted++;
        slot++;

        pos += 8 + sz;
        /* DAR entries aren't sector-aligned; the engine just walks
         * byte-by-byte. */
    }
    fprintf(stderr, "[browser] dar '%s' emitted %d entries\n",
            n.full.c_str(), emitted);
    free(buf);
}

static void load_blob_children(Node &n)
{
    if (n.loaded) return;
    n.loaded = true;
    switch (n.archive_kind) {
        case AK_STAGEDIR: parse_stagedir(n); break;
        case AK_DATACNF:  parse_datacnf(n);  break;
        case AK_DAR:      parse_dar(n);      break;
        default: break;
    }
    node_sort(n);
}

static void load_preview(const Node &n)
{
    preview_clear();
    s_pv_path = n.full;
    s_pv_bytes = n.size;

    /* Always read at least the first ~4KB so we have something to show:
     * a texture if we can decode it, otherwise a hex dump. */
    long sz = 0;
    uint8_t *buf = read_leaf(n, &sz);
    if (!buf || sz <= 0) {
        free(buf);
        s_pv_kind = PV_ERROR;
        s_pv_msg  = n.is_iso ? "Could not read from ISO." : "Could not read file.";
        return;
    }
    s_pv_bytes = sz;

    /* Stash the head of the file for the hex pane (used by PV_HEX, and
     * shown alongside the texture preview when small enough). */
    long hex_keep = sz < 4096 ? sz : 4096;
    s_pv_hex.assign(buf, buf + hex_keep);

    /* Try the texture path. Try by extension first, then sniff magic
     * bytes — TIM headers (0x10 0x00 0x00 0x00) are common in MGS data
     * without a .tim extension. */
    int w = 0, h = 0;
    uint8_t *rgba = NULL;
    bool tried_tim = false;
    if (ext_is(n.full, ".tim")) {
        rgba = decode_tim(buf, sz, &w, &h);
        tried_tim = true;
        if (rgba) s_pv_format = "TIM (PSX)";
    } else if (is_texture_ext(n.full)) {
        int comp = 0;
        rgba = stbi_load_from_memory(buf, (int)sz, &w, &h, &comp, 4);
        if (rgba) {
            const char *e = strrchr(n.full.c_str(), '.');
            s_pv_format = e ? (e + 1) : "image";
            for (auto &c : s_pv_format) c = (char)toupper((unsigned char)c);
        }
    }
    /* Magic-byte fallback: try TIM if first 4 bytes match. */
    if (!rgba && !tried_tim && sz >= 4 &&
        buf[0] == 0x10 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x00) {
        rgba = decode_tim(buf, sz, &w, &h);
        if (rgba) s_pv_format = "TIM (PSX, sniffed)";
    }
    /* And stb_image as a generic fallback. */
    if (!rgba) {
        int comp = 0;
        uint8_t *r = stbi_load_from_memory(buf, (int)sz, &w, &h, &comp, 4);
        if (r) {
            rgba = r;
            s_pv_format = "image (sniffed)";
        }
    }
    free(buf);

    if (rgba) {
        upload_rgba(rgba, w, h);
        /* The TIM path malloc'd; the stb path uses its own free. */
        if (s_pv_format.find("TIM") != std::string::npos) free(rgba);
        else                                              stbi_image_free(rgba);
    } else {
        /* No texture decoder claimed it — fall back to hex dump so the
         * user at least sees the file content + size. */
        s_pv_kind = PV_HEX;
        const char *e = strrchr(n.full.c_str(), '.');
        s_pv_format = e ? (e + 1) : "raw";
        for (auto &c : s_pv_format) c = (char)toupper((unsigned char)c);
    }
}

/* ------------------------------------------------------------------ */
/* Tree rendering                                                     */
/* ------------------------------------------------------------------ */

static void draw_node(Node &n)
{
    if (n.is_dir) {
        char label[128];
        if (n.is_blob && n.archive_kind == AK_STAGEDIR)
            snprintf(label, sizeof label, "%s  [STAGE.DIR]", n.name.c_str());
        else if (n.is_blob && n.archive_kind == AK_DATACNF)
            snprintf(label, sizeof label, "%s  [stage]", n.name.c_str());
        else if (n.is_blob && n.archive_kind == AK_DAR)
            snprintf(label, sizeof label, "%s  [DAR]", n.name.c_str());
        else
            snprintf(label, sizeof label, "%s", n.name.c_str());
        /* PushID by unique tree path so two children with the same
         * display name (common — `0x0000.dar` appears twice per stage)
         * don't share an ImGui id. */
        ImGui::PushID(n.full.c_str());
        bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanFullWidth);
        if (open) {
            if (!n.loaded) {
                if (n.is_blob)     load_blob_children(n);
                else if (n.is_iso) load_iso_children(n);
                else               load_fs_children(n);
            }
            for (auto &c : n.children) draw_node(c);
            ImGui::TreePop();
        }
        ImGui::PopID();
    } else {
        /* Selectable hit-tests the whole row reliably and handles the
         * highlight state for us. PushID + the unique 'full' path keep
         * same-named files in different dirs from clashing. */
        ImGui::PushID(n.full.c_str());
        bool selected = (s_pv_path == n.full);
        if (ImGui::Selectable(n.name.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
            load_preview(n);
        }
        ImGui::PopID();
    }
}

/* ------------------------------------------------------------------ */
/* Right pane                                                         */
/* ------------------------------------------------------------------ */

static void draw_hex_dump(const std::vector<uint8_t> &bytes)
{
    /* Classic 16-bytes-per-row hex + ASCII gutter. */
    char line[128];
    for (size_t off = 0; off < bytes.size(); off += 16) {
        char *p = line;
        p += snprintf(p, 16, "%06zx  ", off);
        for (int i = 0; i < 16; i++) {
            if (off + i < bytes.size())
                p += snprintf(p, 4, "%02x ", bytes[off + i]);
            else
                p += snprintf(p, 4, "   ");
            if (i == 7) { *p++ = ' '; }
        }
        *p++ = ' '; *p++ = '|';
        for (int i = 0; i < 16; i++) {
            if (off + i >= bytes.size()) break;
            uint8_t b = bytes[off + i];
            *p++ = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        *p++ = '|'; *p = '\0';
        ImGui::TextUnformatted(line);
    }
    if (s_pv_bytes > (long)bytes.size())
        ImGui::TextDisabled("… %ld more bytes not shown",
                            s_pv_bytes - (long)bytes.size());
}

static void draw_preview_pane()
{
    if (s_pv_kind == PV_NONE) {
        ImGui::TextDisabled("Pick a file from the tree to preview.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Decoded previews: PNG / JPG / BMP / PCX / TGA / TIM. "
            "Anything else falls back to a hex dump so you can still "
            "spot magic bytes. KMD models and audio come next.");
        return;
    }

    ImGui::TextUnformatted(s_pv_path.c_str());
    ImGui::TextDisabled("%s  ·  %ld bytes",
        s_pv_format.empty() ? "?" : s_pv_format.c_str(), s_pv_bytes);
    ImGui::Separator();

    if (s_pv_kind == PV_ERROR) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "%s", s_pv_msg.c_str());
        return;
    }
    if (s_pv_kind == PV_TEXTURE && s_pv_tex) {
        ImGui::Text("%d x %d", s_pv_w, s_pv_h);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float aspect = s_pv_h > 0 ? (float)s_pv_w / (float)s_pv_h : 1.0f;
        float dw = avail.x, dh = avail.x / aspect;
        if (dh > avail.y) { dh = avail.y; dw = dh * aspect; }
        if (dw < 32) dw = 32;
        if (dh < 32) dh = 32;
        ImGui::Image((ImTextureID)(intptr_t)s_pv_tex, ImVec2(dw, dh));
        return;
    }
    if (s_pv_kind == PV_HEX) {
        ImGui::BeginChild("##hex", ImVec2(0, 0), 0);
        ImGui::PushFont(NULL); /* default font; if a mono is available use it */
        draw_hex_dump(s_pv_hex);
        ImGui::PopFont();
        ImGui::EndChild();
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

extern "C" void ed_browser_tab(void)
{
    ensure_inited();

    ImGui::Columns(2, "##browser_cols", true);
    if (ImGui::GetColumnWidth(0) < 32.0f) ImGui::SetColumnWidth(0, 240.0f);

    /* Left pane: file tree. */
    ImGui::BeginChild("##browser_tree", ImVec2(0, 0), 0);
    draw_node(s_root_iso);
    ImGui::Separator();
    draw_node(s_root_fs);
    ImGui::EndChild();

    ImGui::NextColumn();

    /* Right pane: preview. */
    ImGui::BeginChild("##browser_preview", ImVec2(0, 0), 0);
    draw_preview_pane();
    ImGui::EndChild();

    ImGui::Columns(1);
}
