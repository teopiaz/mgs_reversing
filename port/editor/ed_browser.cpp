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
    AK_KMD      = 4,    /* DG_DEF + DG_MDL[] — emits one child per model */
};

/* KMD file layout constants. The on-disc struct is the PSX-laid-out
 * DG_DEF + DG_MDL[n] form. DG_MDL is 88 bytes (NOT 76 as a comment in
 * kmd_loader.c misstates) — DG_VECTOR is 3×int=12 and there are 16
 * 4-byte fields after the three vectors. */
static constexpr int KMD_DEF_SIZE = 32;
static constexpr int KMD_MDL_SIZE = 88;

struct Node {
    std::string name;       /* display name */
    std::string full;       /* unique tree path */
    bool is_dir = false;
    bool is_iso = false;
    bool is_blob = false;
    bool loaded = false;
    long size = 0;
    std::string host_iso;
    long byte_offset = 0;
    ArchiveKind archive_kind = AK_NONE;
    /* For KMD model children: index into the parent KMD's model array
     * (-1 = whole KMD, render all models combined). */
    int model_index = -1;
    /* For KMD blobs and their model children: byte offset of the parent
     * stage's DATACNF block in host_iso. Used to find sibling PCX
     * textures for textured rendering. -1 = unknown / no stage parent. */
    long stage_byte_offset = -1;
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

enum PreviewKind { PV_NONE, PV_TEXTURE, PV_KMD, PV_UNSUPPORTED, PV_ERROR };

static PreviewKind s_pv_kind = PV_NONE;
static std::string s_pv_path;
static std::string s_pv_msg;            /* error / status text */
static GLuint      s_pv_tex = 0;
static int         s_pv_w   = 0;
static int         s_pv_h   = 0;
static long        s_pv_bytes = 0;
static std::string s_pv_format;         /* "PNG", "JPG", "TIM 16bpp", ... */
static uint8_t s_pv_magic[16] = {0};    /* first few magic bytes for diagnostics */
static int     s_pv_magic_len = 0;

/* KMD-info preview state. The KMD header (port/libdg/kmd_loader.c
 * KMD_DEF_RAW + KMD_MDL_RAW) is small enough to summarise inline:
 * model count, per-model face/vertex counts, bounding box. */
struct KmdModelSummary {
    int n_faces;
    int n_verts;
    int n_normals;
    int flags;
    float min_x, min_y, min_z, max_x, max_y, max_z;
};
static std::vector<KmdModelSummary> s_pv_kmd;
static int   s_pv_kmd_visible = 0;
static float s_pv_kmd_bbox_min[3] = {0,0,0};
static float s_pv_kmd_bbox_max[3] = {0,0,0};

/* KMD 3D wireframe preview — built on first use, drawn into an
 * off-screen FBO each frame the user is hovering the KMD panel. */
static GLuint  s_kmd_fbo = 0, s_kmd_color = 0, s_kmd_depth = 0;
static int     s_kmd_fbo_w = 0, s_kmd_fbo_h = 0;
static GLuint  s_kmd_prog = 0;
static GLint   s_kmd_u_mvp = -1, s_kmd_u_color = -1;
static GLuint  s_kmd_vao = 0, s_kmd_vbo = 0;
static int     s_kmd_n_lines = 0;     /* line vertices (2 per segment) */
static float   s_kmd_center[3] = {0,0,0};
static float   s_kmd_radius = 1.0f;
static float   s_kmd_yaw = 0.0f;
static bool    s_kmd_have_model = false;

/* Textured-render state. When `s_kmd_have_tex == true`, the preview
 * draws textured triangles instead of wireframe lines. Each group is
 * one drawcall against the bound stage texture. */
struct KmdTexGroup { GLuint tex; int first; int count; };  /* vertex range */
static GLuint  s_kmd_tex_prog = 0;
static GLint   s_kmd_tex_u_mvp = -1, s_kmd_tex_u_tex = -1;
static GLuint  s_kmd_tex_vao = 0, s_kmd_tex_vbo = 0;
static std::vector<KmdTexGroup> s_kmd_tex_groups;
static bool    s_kmd_have_tex = false;

static void stage_tex_clear();  /* fwd — defined in texture section */

static void preview_clear()
{
    if (s_pv_tex) { glDeleteTextures(1, &s_pv_tex); s_pv_tex = 0; }
    s_pv_kind = PV_NONE;
    s_pv_w = s_pv_h = 0; s_pv_bytes = 0;
    s_pv_format.clear();
    s_pv_msg.clear();
    s_pv_kmd.clear();
    s_pv_kmd_visible = 0;
    s_pv_magic_len = 0;
    /* Release KMD-preview GPU resources too. Without this, switching
     * from a textured model to anything else leaks all of the model's
     * stage textures (and re-using s_kmd_have_model from a prior click
     * would render stale lines). */
    s_kmd_have_model = false;
    s_kmd_have_tex   = false;
    s_kmd_n_lines    = 0;
    s_kmd_tex_groups.clear();
    stage_tex_clear();
}

/* Forward declarations — defined in the texture-decoder section. */
static uint8_t *decode_pcx(const uint8_t *buf, long sz, int *out_w, int *out_h,
                           const char **fmt_out);

/* Gate noisy diagnostics behind ED_BROWSER_DEBUG=1 so a stage with
 * dozens of DAR/c-region entries doesn't drown the terminal. */
static bool browser_debug()
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("ED_BROWSER_DEBUG");
        cached = (e && atoi(e) > 0) ? 1 : 0;
    }
    return cached != 0;
}
#define BDBG(...) do { if (browser_debug()) fprintf(stderr, __VA_ARGS__); } while (0)

/* ------------------------------------------------------------------ */
/* PCX texture lookup for KMD textured rendering                      */
/* ------------------------------------------------------------------ */

/* (id, GL texture) pairs collected from the parent stage's DARs when
 * a textured model is loaded. Cleared and rebuilt per model. */
struct StageTex {
    uint16_t id;
    GLuint   tex;
    int      w, h;
};
static std::vector<StageTex> s_stage_tex;

static GLuint stage_tex_find(uint16_t id, int *w_out = NULL, int *h_out = NULL)
{
    for (auto &t : s_stage_tex)
        if (t.id == id) {
            if (w_out) *w_out = t.w;
            if (h_out) *h_out = t.h;
            return t.tex;
        }
    return 0;
}

static void stage_tex_clear()
{
    for (auto &t : s_stage_tex) if (t.tex) glDeleteTextures(1, &t.tex);
    s_stage_tex.clear();
}

/* Walk the parent stage's DATACNF at `stage_off`, find its 'n'-mode
 * DAR archives, and decode every PCX inside, uploading each to a GL
 * texture keyed by its strcode id. Only loads textures whose id is
 * present in `wanted` (the materials referenced by the current model)
 * — we don't waste GL textures on PCXes the model doesn't use. */
static void stage_tex_load(const std::string &host_iso, long stage_off,
                           const std::vector<uint16_t> &wanted)
{
    stage_tex_clear();
    if (stage_off < 0 || wanted.empty()) return;
    IsoImage *iso = port_iso_get();
    if (!iso) return;
    IsoFile f;
    if (iso_find_file(iso, host_iso.c_str(), &f) != 0) return;

    /* Read DATACNF tag sector. */
    uint8_t hdr[2048];
    if (iso_read_file(iso, &f, stage_off, 2048, hdr) != 2048) return;

    auto wanted_has = [&](uint16_t id) {
        for (auto w : wanted) if (w == id) return true;
        return false;
    };

    /* Walk tags, descending into 'n'/'r'-mode DARs (ext == 'd'). */
    long cursor = 2048;
    long i = 4;
    while (i + 8 <= 2048) {
        uint8_t mode = hdr[i + 2];
        if (mode == 0) break;
        if (mode == 'c') {
            /* Skip the c-region — find total via terminator. */
            long j = i;
            int32_t total = 0;
            while (j + 8 <= 2048 && hdr[j+2] == 'c') {
                int32_t off = (int32_t)((uint32_t)hdr[j+4] | ((uint32_t)hdr[j+5]<<8) |
                                        ((uint32_t)hdr[j+6]<<16) | ((uint32_t)hdr[j+7]<<24));
                if ((uint8_t)hdr[j+3] == 0xFF) { total = off; j += 8; break; }
                j += 8;
            }
            cursor += (total + 2047) & ~2047L;
            i = j;
            continue;
        }
        char    ext = (char)hdr[i + 3];
        int32_t sz  = (int32_t)((uint32_t)hdr[i+4] | ((uint32_t)hdr[i+5]<<8) |
                                ((uint32_t)hdr[i+6]<<16) | ((uint32_t)hdr[i+7]<<24));
        if (sz <= 0) { i += 8; continue; }

        if (ext == 'd') {
            /* DAR: walk its entries, decode each wanted PCX. */
            uint8_t *dar = (uint8_t *)malloc(sz);
            if (dar) {
                int got = iso_read_file(iso, &f, stage_off + cursor, sz, dar);
                if (got > 0) {
                    long p = 0;
                    while (p + 8 <= sz) {
                        uint16_t did = (uint16_t)dar[p] | ((uint16_t)dar[p+1] << 8);
                        uint16_t dex = (uint16_t)dar[p+2] | ((uint16_t)dar[p+3] << 8);
                        int32_t  dsz = (int32_t)((uint32_t)dar[p+4] | ((uint32_t)dar[p+5]<<8) |
                                                 ((uint32_t)dar[p+6]<<16) | ((uint32_t)dar[p+7]<<24));
                        if (dsz < 0 || p + 8 + dsz > sz) break;
                        char ec = (char)(dex & 0xFF);
                        if (ec == 'p' && wanted_has(did) && !stage_tex_find(did)) {
                            int tw = 0, th = 0;
                            uint8_t *rgba = decode_pcx(dar + p + 8, dsz,
                                                       &tw, &th, NULL);
                            if (rgba) {
                                GLuint t = 0;
                                glGenTextures(1, &t);
                                glBindTexture(GL_TEXTURE_2D, t);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                                             tw, th, 0, GL_RGBA,
                                             GL_UNSIGNED_BYTE, rgba);
                                s_stage_tex.push_back({did, t, tw, th});
                                free(rgba);
                            }
                        }
                        p += 8 + dsz;
                    }
                }
                free(dar);
            }
        }
        cursor += (sz + 2047) & ~2047L;
        i += 8;
    }
    BDBG("[browser] stage_tex_load: %zu PCXes for %zu materials\n",
         s_stage_tex.size(), wanted.size());
}

/* ------------------------------------------------------------------ */
/* KMD 3D wireframe preview                                           */
/* ------------------------------------------------------------------ */

static const char *KMD_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "uniform mat4 uMVP;\n"
    "void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";
static const char *KMD_FS =
    "#version 330 core\n"
    "uniform vec3 uColor;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vec4(uColor, 1.0); }\n";

/* Textured triangle path — interleaved (vec3 pos, vec2 uv). */
static const char *KMD_TEX_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "out vec2 vUV;\n"
    "uniform mat4 uMVP;\n"
    "void main() { vUV = aUV; gl_Position = uMVP * vec4(aPos, 1.0); }\n";
static const char *KMD_TEX_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 oColor;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "    vec4 t = texture(uTex, vUV);\n"
    /* PCX alpha is 0 only for the pure-black 'transparent' palette
     * slot. Discard those texels instead of letting them paint black
     * pixels over the dark preview background — produces a clean
     * cutout look matching how the engine renders STP-cleared pixels. */
    "    if (t.a < 0.1) discard;\n"
    "    oColor = t;\n"
    "}\n";

static GLuint kmd_compile(GLenum stage, const char *src)
{
    GLuint s = glCreateShader(stage);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n=0;
        glGetShaderInfoLog(s, sizeof log, &n, log);
        fprintf(stderr, "[browser] KMD shader compile: %.*s\n", n, log);
        glDeleteShader(s); return 0;
    }
    return s;
}

/* Link a program from a pair of shader sources. Returns 0 on failure.
 * Centralises the boilerplate that compiled the two KMD shaders twice
 * with subtly different error handling. */
static GLuint kmd_link(const char *vs_src, const char *fs_src, const char *tag)
{
    GLuint vs = kmd_compile(GL_VERTEX_SHADER,   vs_src);
    GLuint fs = kmd_compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof log, &n, log);
        fprintf(stderr, "[browser] KMD %s link: %.*s\n", tag, n, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static void kmd_preview_init_gl()
{
    if (s_kmd_prog) return;
    s_kmd_prog = kmd_link(KMD_VS, KMD_FS, "lines");
    if (!s_kmd_prog) return;
    s_kmd_u_mvp   = glGetUniformLocation(s_kmd_prog, "uMVP");
    s_kmd_u_color = glGetUniformLocation(s_kmd_prog, "uColor");

    glGenVertexArrays(1, &s_kmd_vao);
    glGenBuffers(1, &s_kmd_vbo);
    glBindVertexArray(s_kmd_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_kmd_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void *)0);
    glBindVertexArray(0);

    /* Textured path — separate shader + VAO with interleaved (pos, uv). */
    s_kmd_tex_prog = kmd_link(KMD_TEX_VS, KMD_TEX_FS, "textured");
    if (s_kmd_tex_prog) {
        s_kmd_tex_u_mvp = glGetUniformLocation(s_kmd_tex_prog, "uMVP");
        s_kmd_tex_u_tex = glGetUniformLocation(s_kmd_tex_prog, "uTex");
    }
    glGenVertexArrays(1, &s_kmd_tex_vao);
    glGenBuffers(1, &s_kmd_tex_vbo);
    glBindVertexArray(s_kmd_tex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_kmd_tex_vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5,
                          (void *)(sizeof(float) * 3));
    glBindVertexArray(0);
}

static void kmd_preview_ensure_fbo(int w, int h)
{
    if (s_kmd_fbo_w == w && s_kmd_fbo_h == h && s_kmd_fbo) return;
    if (s_kmd_fbo)   { glDeleteFramebuffers(1, &s_kmd_fbo); s_kmd_fbo = 0; }
    if (s_kmd_color) { glDeleteTextures(1, &s_kmd_color);    s_kmd_color = 0; }
    if (s_kmd_depth) { glDeleteRenderbuffers(1, &s_kmd_depth); s_kmd_depth = 0; }
    glGenTextures(1, &s_kmd_color);
    glBindTexture(GL_TEXTURE_2D, s_kmd_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &s_kmd_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s_kmd_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &s_kmd_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_kmd_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_kmd_color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, s_kmd_depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s_kmd_fbo_w = w; s_kmd_fbo_h = h;
}

/* Build a textured-triangle VBO for one KMD model, grouped by
 * material so each unique texture is one drawcall.
 *
 *  - texcoords array: 8 bytes per face (4 corners × u8 u, u8 v in
 *    0..255 PSX-texel space).
 *  - materials array: u16 per face — strcode of the PCX whose texels
 *    the corners index into.
 *
 * Returns true if the model was successfully textured (at least one
 * face emitted with a usable texture); caller falls back to wireframe
 * otherwise. */
static bool kmd_preview_build_textured(const uint8_t *buf, long sz,
                                       int model_index)
{
    s_kmd_have_tex = false;
    s_kmd_tex_groups.clear();
    if (sz < KMD_DEF_SIZE) return false;
    if (s_stage_tex.empty()) return false;

    auto rd_i32 = [](const uint8_t *p) -> int32_t {
        return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1]<<8) |
                         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24));
    };
    auto rd_u32 = [](const uint8_t *p) -> uint32_t {
        return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
               ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    };
    auto rd_i16 = [](const uint8_t *p) -> int16_t {
        return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
    };

    int32_t n_models = rd_i32(buf + 4);
    if (model_index < 0 || model_index >= n_models) return false;

    const uint8_t *m = buf + KMD_DEF_SIZE + (long)model_index * KMD_MDL_SIZE;
    int32_t  n_faces        = rd_i32(m + 4);
    int32_t  n_verts        = rd_i32(m + 52);
    uint32_t vertices_off   = rd_u32(m + 56);
    uint32_t vindices_off   = rd_u32(m + 60);
    uint32_t texcoords_off  = rd_u32(m + 76);
    uint32_t materials_off  = rd_u32(m + 80);
    if (n_faces <= 0 || n_verts <= 0) return false;
    if (!vertices_off || !vindices_off || !texcoords_off || !materials_off)
        return false;
    if ((long)vertices_off  + (long)n_verts * 8 > sz)  return false;
    if ((long)vindices_off  + (long)n_faces * 4 > sz)  return false;
    if ((long)texcoords_off + (long)n_faces * 8 > sz)  return false;
    if ((long)materials_off + (long)n_faces * 2 > sz)  return false;

    const uint8_t *v   = buf + vertices_off;
    const uint8_t *fi  = buf + vindices_off;
    const uint8_t *tc  = buf + texcoords_off;
    const uint8_t *mat = buf + materials_off;

    /* Pre-decode vertices into model-local floats. */
    std::vector<float> mv((size_t)n_verts * 3);
    for (int k = 0; k < n_verts; k++) {
        mv[k*3 + 0] = (float)rd_i16(v + k*8 + 0);
        mv[k*3 + 1] = (float)rd_i16(v + k*8 + 2);
        mv[k*3 + 2] = (float)rd_i16(v + k*8 + 4);
    }

    /* Group faces by material hash. */
    struct GroupBuf { GLuint tex; int tex_w, tex_h; std::vector<float> verts; };
    std::vector<GroupBuf> groups;
    auto get_group = [&](uint16_t mid) -> GroupBuf * {
        int tw=0, th=0;
        GLuint tx = stage_tex_find(mid, &tw, &th);
        if (!tx || tw <= 0 || th <= 0) return NULL;
        for (auto &g : groups) if (g.tex == tx) return &g;
        groups.push_back({tx, tw, th, {}});
        return &groups.back();
    };

    /* Compute bbox from used verts only. */
    float bmin[3] = { 1e9f,  1e9f,  1e9f};
    float bmax[3] = {-1e9f, -1e9f, -1e9f};

    for (int f = 0; f < n_faces; f++) {
        uint16_t mid = (uint16_t)mat[f*2] | ((uint16_t)mat[f*2 + 1] << 8);
        if (mid == 0) continue;
        GroupBuf *g = get_group(mid);
        if (!g) continue;

        uint8_t a = fi[f*4 + 0] & 0x7F;
        uint8_t b = fi[f*4 + 1] & 0x7F;
        uint8_t c = fi[f*4 + 2] & 0x7F;
        uint8_t d = fi[f*4 + 3] & 0x7F;
        if (a >= n_verts || b >= n_verts || c >= n_verts || d >= n_verts) continue;

        /* Per-corner UVs in [0,255] → [0,1]. Divide by texture w/h so
         * each PCX (which may be smaller than 256×256) gets the right
         * sampling box. */
        float uw = (float)g->tex_w;
        float uh = (float)g->tex_h;
        float uv[4][2] = {
            { (float)tc[f*8 + 0] / uw, (float)tc[f*8 + 1] / uh },
            { (float)tc[f*8 + 2] / uw, (float)tc[f*8 + 3] / uh },
            { (float)tc[f*8 + 4] / uw, (float)tc[f*8 + 5] / uh },
            { (float)tc[f*8 + 6] / uw, (float)tc[f*8 + 7] / uh },
        };
        int idxs[4] = { a, b, c, d };

        /* Quad → 2 tris (0,1,2) + (0,2,3). */
        const int tri[2][3] = { {0,1,2}, {0,2,3} };
        for (int t = 0; t < 2; t++) {
            for (int k = 0; k < 3; k++) {
                int idx = idxs[tri[t][k]];
                float x = mv[idx*3+0], y = mv[idx*3+1], z = mv[idx*3+2];
                g->verts.push_back(x);
                g->verts.push_back(y);
                g->verts.push_back(z);
                g->verts.push_back(uv[tri[t][k]][0]);
                g->verts.push_back(uv[tri[t][k]][1]);
                if (x < bmin[0]) bmin[0] = x;
                if (y < bmin[1]) bmin[1] = y;
                if (z < bmin[2]) bmin[2] = z;
                if (x > bmax[0]) bmax[0] = x;
                if (y > bmax[1]) bmax[1] = y;
                if (z > bmax[2]) bmax[2] = z;
            }
        }
    }

    if (groups.empty()) return false;

    /* Concatenate all groups into one VBO and remember offsets. */
    std::vector<float> all_verts;
    s_kmd_tex_groups.clear();
    for (auto &g : groups) {
        int first = (int)(all_verts.size() / 5);
        int count = (int)(g.verts.size() / 5);
        all_verts.insert(all_verts.end(), g.verts.begin(), g.verts.end());
        s_kmd_tex_groups.push_back({g.tex, first, count});
    }
    if (all_verts.empty()) return false;

    kmd_preview_init_gl();
    glBindVertexArray(s_kmd_tex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_kmd_tex_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(all_verts.size() * sizeof(float)),
                 all_verts.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    s_kmd_center[0] = 0.5f * (bmin[0] + bmax[0]);
    s_kmd_center[1] = 0.5f * (bmin[1] + bmax[1]);
    s_kmd_center[2] = 0.5f * (bmin[2] + bmax[2]);
    float dx = bmax[0] - bmin[0], dy = bmax[1] - bmin[1], dz = bmax[2] - bmin[2];
    float r = 0.5f * sqrtf(dx*dx + dy*dy + dz*dz);
    if (r < 1.0f) r = 1.0f;
    s_kmd_radius = r;
    s_kmd_yaw    = 0.0f;
    s_kmd_have_tex = true;
    BDBG("[browser] kmd textured: %zu groups, %d tri-verts\n",
         s_kmd_tex_groups.size(), (int)(all_verts.size() / 5));
    return true;
}

/* Build a per-edge line VBO from a KMD's vertices + face indices.
 *
 * DG_MDL layout (88 bytes on disc):
 *   off 0  : flags  (i32)
 *   off 4  : n_faces (i32)
 *   off 8  : min   (DG_VECTOR = 3 × i32)
 *   off 20 : max
 *   off 32 : pos
 *   off 44 : parent (i32)
 *   off 48 : extend (i32)
 *   off 52 : n_verts (i32)
 *   off 56 : vertices_off (u32)
 *   off 60 : vindices_off (u32)
 *   off 64 : n_normals (i32)
 *   off 68 : normals_off (u32)
 *   off 72 : nindices_off (u32)
 *   off 76 : texcoords_off (u32)
 *   off 80 : materials_off (u32)
 *   off 84 : padding
 *
 * Faces are quads (4 indices); we wire the four edges (0-1, 1-2,
 * 2-3, 3-0) for a clean wireframe.
 *
 * `model_filter` ≥ 0 → render only that one model in isolation,
 * positioned at the world origin (we drop the per-model pos so the
 * camera frames it cleanly). < 0 → render all combined. */
static void kmd_preview_build_lines(const uint8_t *buf, long sz,
                                    int model_filter = -1)
{
    s_kmd_have_model = false;
    s_kmd_n_lines = 0;
    if (sz < KMD_DEF_SIZE) return;
    auto rd_i32 = [](const uint8_t *p) -> int32_t {
        return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1]<<8) |
                         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24));
    };
    auto rd_u32 = [](const uint8_t *p) -> uint32_t {
        return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
               ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    };
    auto rd_i16 = [](const uint8_t *p) -> int16_t {
        return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
    };

    int32_t n_models = rd_i32(buf + 4);
    if (n_models <= 0 || n_models > 256) return;

    std::vector<float> verts;
    verts.reserve(4096);

    float min[3] = { 1e9f,  1e9f,  1e9f};
    float max[3] = {-1e9f, -1e9f, -1e9f};

    for (int i = 0; i < n_models; i++) {
        if (model_filter >= 0 && i != model_filter) continue;
        const uint8_t *m = buf + KMD_DEF_SIZE + (long)i * KMD_MDL_SIZE;
        int32_t  n_faces      = rd_i32(m + 4);
        int32_t  n_verts      = rd_i32(m + 52);
        uint32_t vertices_off = rd_u32(m + 56);
        uint32_t vindices_off = rd_u32(m + 60);
        /* If we're rendering just this model on its own, drop its
         * parent-relative offset so it lands at the origin. */
        float pos_x = (model_filter >= 0) ? 0.0f : (float)rd_i32(m + 32);
        float pos_y = (model_filter >= 0) ? 0.0f : (float)rd_i32(m + 36);
        float pos_z = (model_filter >= 0) ? 0.0f : (float)rd_i32(m + 40);

        if (n_verts <= 0 || n_faces <= 0) continue;
        if (vertices_off == 0 || vindices_off == 0) continue;
        if ((long)vertices_off + (long)n_verts * 8 > sz)   continue;
        if ((long)vindices_off + (long)n_faces * 4 > sz)   continue;

        const uint8_t *v  = buf + vertices_off;
        const uint8_t *fi = buf + vindices_off;

        /* Pre-decode vertices into a model-local float array. */
        std::vector<float> mv;
        mv.resize((size_t)n_verts * 3);
        for (int k = 0; k < n_verts; k++) {
            float x = (float)rd_i16(v + k*8 + 0);
            float y = (float)rd_i16(v + k*8 + 2);
            float z = (float)rd_i16(v + k*8 + 4);
            mv[k*3 + 0] = x + pos_x;
            mv[k*3 + 1] = y + pos_y;
            mv[k*3 + 2] = z + pos_z;
            if (mv[k*3+0] < min[0]) min[0] = mv[k*3+0];
            if (mv[k*3+1] < min[1]) min[1] = mv[k*3+1];
            if (mv[k*3+2] < min[2]) min[2] = mv[k*3+2];
            if (mv[k*3+0] > max[0]) max[0] = mv[k*3+0];
            if (mv[k*3+1] > max[1]) max[1] = mv[k*3+1];
            if (mv[k*3+2] > max[2]) max[2] = mv[k*3+2];
        }

        /* Emit 4 edges per face as 8 line vertices. */
        for (int f = 0; f < n_faces; f++) {
            uint8_t a = fi[f*4 + 0] & 0x7F;
            uint8_t b = fi[f*4 + 1] & 0x7F;
            uint8_t c = fi[f*4 + 2] & 0x7F;
            uint8_t d = fi[f*4 + 3] & 0x7F;
            if (a >= n_verts || b >= n_verts || c >= n_verts || d >= n_verts) continue;
            uint8_t e[8] = { a,b, b,c, c,d, d,a };
            for (int j = 0; j < 8; j++) {
                int idx = e[j];
                verts.push_back(mv[idx*3+0]);
                verts.push_back(mv[idx*3+1]);
                verts.push_back(mv[idx*3+2]);
            }
        }
    }

    if (verts.empty()) return;

    kmd_preview_init_gl();
    glBindVertexArray(s_kmd_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_kmd_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    s_kmd_n_lines = (int)(verts.size() / 3);    /* one vertex per push */

    s_kmd_center[0] = 0.5f * (min[0] + max[0]);
    s_kmd_center[1] = 0.5f * (min[1] + max[1]);
    s_kmd_center[2] = 0.5f * (min[2] + max[2]);
    float dx = max[0] - min[0], dy = max[1] - min[1], dz = max[2] - min[2];
    float r = 0.5f * sqrtf(dx*dx + dy*dy + dz*dz);
    if (r < 1.0f) r = 1.0f;
    s_kmd_radius = r;
    s_kmd_yaw    = 0.0f;
    s_kmd_have_model = true;
}

/* Build a column-major perspective matrix. */
static void kmd_make_perspective(float out[16], float fovy, float aspect,
                                 float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    for (int i = 0; i < 16; i++) out[i] = 0.0f;
    out[0]  = f / aspect;
    out[5]  = f;
    out[10] = (zfar + znear) / (znear - zfar);
    out[11] = -1.0f;
    out[14] = (2.0f * zfar * znear) / (znear - zfar);
}

/* MVP = persp * view * model. model = translate(-center) ; view =
 * translate(0,0,-distance) * rotateY(yaw) * rotateX(-25°). */
static void kmd_compute_mvp(float out[16], float aspect)
{
    float proj[16];
    kmd_make_perspective(proj, 60.0f * (float)M_PI / 180.0f, aspect,
                          s_kmd_radius * 0.05f, s_kmd_radius * 20.0f);
    float dist = s_kmd_radius * 2.8f;
    float cy = cosf(s_kmd_yaw), sy = sinf(s_kmd_yaw);
    float px = -0.4f, cx = cosf(px), sx = sinf(px);
    /* Compose: M = T(-c) ; R_y(yaw) ; R_x(px) ; T(0,0,-dist).
     * We multiply column-major: out = proj * (R_x * R_y * T_center)
     * with the camera offset applied after. Doing it by hand:           */
    float tx = -s_kmd_center[0], ty = -s_kmd_center[1], tz = -s_kmd_center[2];
    /* view-space pos of a model point p:
     *   v = R_x * R_y * (p + c_offset) + (0, 0, -dist)
     * Translate-after-rotate, so feed each component through manually. */
    float m[16] = {
        cy,            sx*sy,         -cx*sy,        0,
        0,             cx,            sx,            0,
        sy,           -sx*cy,         cx*cy,         0,
        cy*tx + sy*tz,
        sx*sy*tx + cx*ty - sx*cy*tz,
       -cx*sy*tx + sx*ty + cx*cy*tz - dist,
        1.0f
    };
    /* out = proj * m (column-major matmul). */
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += proj[k*4 + row] * m[col*4 + k];
            out[col*4 + row] = s;
        }
}

static void kmd_preview_render_frame()
{
    if (!(s_kmd_have_model || s_kmd_have_tex)) return;
    int w = s_kmd_fbo_w, h = s_kmd_fbo_h;
    if (w <= 0 || h <= 0) return;

    GLint  prev_fbo, prev_vp[4], prev_prog;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);

    glBindFramebuffer(GL_FRAMEBUFFER, s_kmd_fbo);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    float mvp[16];
    kmd_compute_mvp(mvp, (float)w / (float)h);

    if (s_kmd_have_tex && !s_kmd_tex_groups.empty() && s_kmd_tex_prog) {
        /* Textured tri pass — one drawcall per material group. */
        glUseProgram(s_kmd_tex_prog);
        glUniformMatrix4fv(s_kmd_tex_u_mvp, 1, GL_FALSE, mvp);
        glUniform1i(s_kmd_tex_u_tex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(s_kmd_tex_vao);
        for (auto &g : s_kmd_tex_groups) {
            glBindTexture(GL_TEXTURE_2D, g.tex);
            glDrawArrays(GL_TRIANGLES, g.first, g.count);
        }
        glBindVertexArray(0);
    } else if (s_kmd_have_model && s_kmd_n_lines > 0) {
        glLineWidth(1.0f);
        glUseProgram(s_kmd_prog);
        glUniformMatrix4fv(s_kmd_u_mvp, 1, GL_FALSE, mvp);
        glUniform3f(s_kmd_u_color, 0.6f, 0.85f, 1.0f);
        glBindVertexArray(s_kmd_vao);
        glDrawArrays(GL_LINES, 0, s_kmd_n_lines);
        glBindVertexArray(0);
    }

    s_kmd_yaw += 0.012f;
    if (s_kmd_yaw > 6.2831853f) s_kmd_yaw -= 6.2831853f;

    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glUseProgram(prev_prog);
}

/* Detect MGS-style KMD by parsing the first 32 bytes. Returns true if
 * the header looks sane (n_models in [1..256], bbox finite). Populates
 * s_pv_kmd / s_pv_kmd_visible / s_pv_kmd_bbox_* on success. */
static bool try_decode_kmd(const uint8_t *buf, long sz)
{
    if (sz < 32) return false;
    auto rd_i32 = [](const uint8_t *p) -> int32_t {
        return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1]<<8) |
                         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24));
    };
    auto rd_vec = [&](const uint8_t *p, float out[3]) {
        out[0] = (float)rd_i32(p);
        out[1] = (float)rd_i32(p + 4);
        out[2] = (float)rd_i32(p + 8);
    };
    int32_t n_visible = rd_i32(buf + 0);
    int32_t n_models  = rd_i32(buf + 4);
    if (n_models <= 0 || n_models > 256) return false;
    float min[3], max[3];
    rd_vec(buf + 8,  min);
    rd_vec(buf + 20, max);
    long need = KMD_DEF_SIZE + (long)n_models * KMD_MDL_SIZE;
    if (need > sz) return false;

    s_pv_kmd.clear();
    s_pv_kmd.reserve(n_models);
    for (int i = 0; i < n_models; i++) {
        const uint8_t *m = buf + KMD_DEF_SIZE + (long)i * KMD_MDL_SIZE;
        KmdModelSummary s;
        s.flags     = rd_i32(m + 0);
        s.n_faces   = rd_i32(m + 4);
        float mmin[3], mmax[3], mpos[3];
        rd_vec(m + 8,  mmin);
        rd_vec(m + 20, mmax);
        rd_vec(m + 32, mpos);
        s.min_x = mmin[0]; s.min_y = mmin[1]; s.min_z = mmin[2];
        s.max_x = mmax[0]; s.max_y = mmax[1]; s.max_z = mmax[2];
        s.n_verts   = rd_i32(m + 52);
        s.n_normals = rd_i32(m + 64);
        s_pv_kmd.push_back(s);
    }
    s_pv_kmd_visible = n_visible;
    s_pv_kmd_bbox_min[0] = min[0]; s_pv_kmd_bbox_min[1] = min[1]; s_pv_kmd_bbox_min[2] = min[2];
    s_pv_kmd_bbox_max[0] = max[0]; s_pv_kmd_bbox_max[1] = max[1]; s_pv_kmd_bbox_max[2] = max[2];

    /* Also build the line VBO for the 3D preview. */
    kmd_preview_build_lines(buf, sz);
    return true;
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

/* MGS-PCX decoder. The disc stores textures as RLE-compressed PCX with
 * either an 8bpp indexed body + 256-colour palette appended after, or
 * a 4bpp body using the 16-colour header palette. Both layouts come
 * out of source/libdg/loader.c and port/libdg/pcx_loader.c.
 *
 * Returns an RGBA8 buffer (caller frees) or NULL on bad data. */
static uint8_t *decode_pcx(const uint8_t *buf, long sz, int *out_w, int *out_h,
                           const char **fmt_out)
{
    if (sz < 128 || buf[0] != 0x0A) return NULL;

    uint16_t min_x  = (uint16_t)buf[4]  | ((uint16_t)buf[5]  << 8);
    uint16_t min_y  = (uint16_t)buf[6]  | ((uint16_t)buf[7]  << 8);
    uint16_t max_x  = (uint16_t)buf[8]  | ((uint16_t)buf[9]  << 8);
    uint16_t max_y  = (uint16_t)buf[10] | ((uint16_t)buf[11] << 8);
    uint16_t bpl    = (uint16_t)buf[66] | ((uint16_t)buf[67] << 8);

    /* The MGS header tucks per-image PCXINFO into the pad area at
     * offset 70. Its `flags` low bit picks 8bpp (set) vs 4bpp (clear). */
    uint16_t mgs_flags = (uint16_t)buf[72] | ((uint16_t)buf[73] << 8);
    bool eight_bpp = (mgs_flags & 1) != 0;

    int mx = (int)min_x - 1;
    int my = (int)min_y - 1;
    int width  = (int)max_x - mx;
    int height = (int)max_y - my;
    if (!eight_bpp) width /= 2;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return NULL;

    const uint8_t *body = buf + 128;
    const uint8_t *end  = buf + sz;

    int w = width, h = height;
    uint8_t *indices = (uint8_t *)calloc((size_t)w * h, 1);
    if (!indices) return NULL;

    if (eight_bpp) {
        /* 8bpp RLE decode straight into indices[] (one byte per pixel). */
        int pos = 0;
        while (pos < w * h && body < end) {
            uint8_t b = *body++;
            if (b >= 0xC0) {
                int n = b & 0x3F;
                if (body >= end) break;
                uint8_t v = *body++;
                while (n-- > 0 && pos < w * h) indices[pos++] = v;
            } else {
                indices[pos++] = b;
            }
        }
        /* Palette: 0x0C marker + 256 RGB triples appended after the
         * image data. */
        const uint8_t *pal = NULL;
        if (sz >= 769 && buf[sz - 769] == 0x0C) pal = buf + sz - 768;
        uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
        if (!rgba) { free(indices); return NULL; }
        for (int i = 0; i < w * h; i++) {
            uint8_t v = indices[i];
            if (pal) {
                rgba[i*4 + 0] = pal[v * 3 + 0];
                rgba[i*4 + 1] = pal[v * 3 + 1];
                rgba[i*4 + 2] = pal[v * 3 + 2];
            } else {
                rgba[i*4 + 0] = rgba[i*4 + 1] = rgba[i*4 + 2] = v;
            }
            rgba[i*4 + 3] = (rgba[i*4+0] | rgba[i*4+1] | rgba[i*4+2]) ? 255 : 0;
        }
        free(indices);
        *out_w = w; *out_h = h;
        if (fmt_out) *fmt_out = "PCX 8bpp";
        return rgba;
    }

    /* 4bpp: 4 bit-planes (R,G,B,A) per scanline; decode_4bpp logic
     * mirrors DG_PcxRead4Bpp. */
    int bpl_total = 4 * bpl;
    uint8_t *line = (uint8_t *)malloc((size_t)bpl_total);
    if (!line) { free(indices); return NULL; }
    for (int y = 0; y < h; y++) {
        int got = 0;
        while (got < bpl_total && body < end) {
            uint8_t b = *body++;
            if (b < 0xC0) {
                line[got++] = b;
            } else {
                int n = b & 0x3F;
                if (body >= end) break;
                uint8_t v = *body++;
                while (n-- > 0 && got < bpl_total) line[got++] = v;
            }
        }
        const uint8_t *rp = line;
        const uint8_t *gp = rp + bpl;
        const uint8_t *bp = gp + bpl;
        const uint8_t *ap = bp + bpl;
        for (int x = 0; x < w; x += 4) {
            uint8_t r = *rp++, g = *gp++, bl = *bp++, a = *ap++;
            int shift = 128;
            for (int k = 0; k < 8 && (x + (k / 2)) < w; k += 2) {
                uint8_t color = 0;
                if (shift & r)  color |= 1;
                if (shift & g)  color |= 2;
                if (shift & bl) color |= 4;
                if (shift & a)  color |= 8;
                shift >>= 1;
                if (shift & r)  color |= 0x10;
                if (shift & g)  color |= 0x20;
                if (shift & bl) color |= 0x40;
                if (shift & a)  color |= 0x80;
                shift >>= 1;
                if (x + (k / 2) < w) indices[y * w + x + (k / 2)] = color;
            }
        }
    }
    free(line);

    /* 4bpp palette = the 48-byte header palette (offset 16, 16 colours
     * × 3 bytes). */
    const uint8_t *pal = buf + 16;
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!rgba) { free(indices); return NULL; }
    for (int i = 0; i < w * h; i++) {
        uint8_t v = indices[i] & 0x0F;
        rgba[i*4 + 0] = pal[v * 3 + 0];
        rgba[i*4 + 1] = pal[v * 3 + 1];
        rgba[i*4 + 2] = pal[v * 3 + 2];
        rgba[i*4 + 3] = (pal[v*3]|pal[v*3+1]|pal[v*3+2]) ? 255 : 0;
    }
    free(indices);
    *out_w = w; *out_h = h;
    if (fmt_out) *fmt_out = "PCX 4bpp";
    return rgba;
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
        BDBG("[browser] datacnf: read failed host='%s' off=%ld\n",
             n.host_iso.c_str(), n.byte_offset);
        return;
    }

    /* Walk the tag table once just to locate the data sector — the
     * actual file walk uses the same offsets but emits children. */
    long tag_pos = 4;
    long scan = tag_pos;
    while (scan + 8 <= 2048) {
        if (hdr[scan + 2] == 0) { scan += 8; break; }
        scan += 8;
    }
    long data_off = (scan + 2047) & ~2047L;

    /* Two phases — DATACNF tags interleave two file-storage models:
     *
     *  1. Inline data (modes 's' / 'n' / 'r'):
     *     The tag's `size` is the actual file length; the file's bytes
     *     come straight after the previous tag's data, sector-aligned.
     *     Walked the obvious way: blob at cursor, advance cursor by
     *     sector-rounded size.
     *
     *  2. The "c-region" (mode 'c'):
     *     Several consecutive 'c' tags share a packed concatenated
     *     region that begins at the current data cursor. Each 'c'
     *     tag's `size` field is actually the OFFSET (4-byte aligned)
     *     of that file from the c-region base; the file's byte length
     *     is the difference between this and the next 'c' tag's
     *     offset. A terminator 'c' tag has ext == 0xFF and size ==
     *     total c-region size in bytes. After the terminator, the
     *     cursor advances to the next non-'c' run.
     *     This is where KMD models and most stage assets live. */
    long cursor = data_off;
    int  emitted = 0;
    int  slot    = 0;
    long i = tag_pos;
    while (i + 8 <= 2048) {
        uint8_t mode = hdr[i + 2];
        if (mode == 0) break;

        if (mode == 'c') {
            /* Walk a contiguous c-region run. */
            long c_base = cursor;
            long j = i;
            /* Build a temporary list of (id, ext, offset) for the run
             * so we can compute each file's size from the next tag. */
            struct CTag { uint16_t id; char ext; int32_t off; };
            std::vector<CTag> ctags;
            while (j + 8 <= 2048) {
                uint8_t  cm = hdr[j + 2];
                if (cm != 'c') break;
                uint16_t cid = (uint16_t)hdr[j] | ((uint16_t)hdr[j + 1] << 8);
                char     cex = (char)hdr[j + 3];
                int32_t  csz = (int32_t)((uint32_t)hdr[j + 4] |
                                         ((uint32_t)hdr[j + 5] << 8) |
                                         ((uint32_t)hdr[j + 6] << 16) |
                                         ((uint32_t)hdr[j + 7] << 24));
                ctags.push_back({cid, cex, csz});
                j += 8;
                /* Terminator: ext == 0xFF; size is total c-region size. */
                if ((uint8_t)cex == 0xFF) break;
            }
            /* Emit one blob per real 'c' entry. The trailing terminator
             * tells us the run's total byte size — without it we can't
             * size the LAST entry; fall back to "until terminator's
             * size" in that case. */
            int32_t total_region = 0;
            if (!ctags.empty() && (uint8_t)ctags.back().ext == 0xFF)
                total_region = ctags.back().off;
            for (size_t k = 0; k < ctags.size(); k++) {
                const CTag &t = ctags[k];
                if ((uint8_t)t.ext == 0xFF) continue;          /* skip terminator */
                int32_t next_off = (k + 1 < ctags.size()) ? ctags[k + 1].off
                                                          : total_region;
                int32_t this_off = (t.off + 3) & ~3;           /* 4-byte aligned */
                int32_t sz_c = next_off - t.off;
                if (sz_c <= 0 || this_off >= total_region) { slot++; continue; }

                const char *resolved = strcode_to_string(t.id);
                char namebuf[96];
                if (resolved) snprintf(namebuf, sizeof namebuf, "%s.%s",
                                       resolved, datacnf_ext_to_str(t.ext));
                else          snprintf(namebuf, sizeof namebuf, "0x%04x.%s",
                                       t.id, datacnf_ext_to_str(t.ext));

                Node c;
                c.name = namebuf;
                char fullbuf[128];
                snprintf(fullbuf, sizeof fullbuf, "%s/[%02d]%s",
                         n.full.c_str(), slot, namebuf);
                c.full = fullbuf;
                c.is_blob = true;
                c.host_iso = n.host_iso;
                c.byte_offset = n.byte_offset + c_base + this_off;
                c.size = sz_c;
                if (t.ext == 'k') {
                    c.is_dir = true;
                    c.archive_kind = AK_KMD;
                    c.stage_byte_offset = n.byte_offset;
                }
                n.children.push_back(std::move(c));
                emitted++;
                slot++;
            }
            /* Advance cursor past the whole c-region (sector aligned),
             * skip the consumed tag slots. */
            cursor += (total_region + 2047) & ~2047L;
            i = j;
            continue;
        }

        /* Inline data path (mode != 'c'). */
        uint16_t id   = (uint16_t)hdr[i] | ((uint16_t)hdr[i + 1] << 8);
        char     ext  = (char)hdr[i + 3];
        int32_t  sz   = (int32_t)((uint32_t)hdr[i + 4] |
                                  ((uint32_t)hdr[i + 5] << 8) |
                                  ((uint32_t)hdr[i + 6] << 16) |
                                  ((uint32_t)hdr[i + 7] << 24));
        if (sz > 256 * 1024 * 1024) break;
        if (sz < 0) sz = 0;
        if (sz == 0) { i += 8; slot++; continue; }

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
        if (ext == 'd') { c.is_dir = true; c.archive_kind = AK_DAR; }
        else if (ext == 'k') { c.is_dir = true; c.archive_kind = AK_KMD; }
        cursor += (sz + 2047) & ~2047L;
        n.children.push_back(std::move(c));
        emitted++;
        slot++;
        i += 8;
    }
    BDBG("[browser] datacnf '%s' off=%ld -> %d children\n",
         n.full.c_str(), n.byte_offset, emitted);
    free(hdr);
}

/* Parse STAGE.DIR header → stage directory entries pointing into
 * itself. Each entry becomes a sub-directory whose lazy load parses
 * the per-stage DATACNF block. */
static void parse_stagedir(Node &n)
{
    uint8_t *hdr = read_iso_slice(n.host_iso, 0, 4096);
    if (!hdr) return;
    uint32_t tbl_size = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                        ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
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
    BDBG("[browser] stagedir '%s' -> %d stages\n", n.host_iso.c_str(), emitted);
    free(hdr);
}

/* KMD as an archive: peek at the model count and emit one child per
 * model. Each child shares the parent KMD's host_iso + byte_offset +
 * size — we differentiate them by model_index. When the user clicks
 * a model child, load_preview re-reads the whole KMD and renders just
 * that model in isolation. */
static void parse_kmd(Node &n)
{
    if (n.size < KMD_DEF_SIZE) return;
    uint8_t hdr[KMD_DEF_SIZE];
    IsoImage *iso = port_iso_get();
    if (!iso) return;
    IsoFile f;
    if (iso_find_file(iso, n.host_iso.c_str(), &f) != 0) return;
    if (iso_read_file(iso, &f, n.byte_offset, KMD_DEF_SIZE, hdr) != KMD_DEF_SIZE)
        return;
    int32_t n_models =
        (int32_t)((uint32_t)hdr[4] | ((uint32_t)hdr[5]<<8) |
                  ((uint32_t)hdr[6]<<16) | ((uint32_t)hdr[7]<<24));
    if (n_models <= 0 || n_models > 256) return;
    /* The model headers immediately follow; total fixed-size table
     * must fit inside the blob. */
    long table_end = KMD_DEF_SIZE + (long)n_models * KMD_MDL_SIZE;
    if (table_end > n.size) return;

    n.children.reserve(n_models);
    for (int i = 0; i < n_models; i++) {
        Node c;
        char namebuf[24]; snprintf(namebuf, sizeof namebuf, "model %d", i);
        c.name = namebuf;
        char fullbuf[160];
        snprintf(fullbuf, sizeof fullbuf, "%s/model%d", n.full.c_str(), i);
        c.full = fullbuf;
        c.is_blob = true;
        c.host_iso = n.host_iso;
        c.byte_offset = n.byte_offset;  /* model children share parent bytes */
        c.size = n.size;
        c.model_index = i;
        c.stage_byte_offset = n.stage_byte_offset;
        n.children.push_back(std::move(c));
    }
    BDBG("[browser] kmd '%s' -> %d model children\n",
         n.full.c_str(), n_models);
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
    if (!buf) return;
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
        /* Guard against overflow: pos is long, sz is int32_t — cast both
         * to a wide signed type before the comparison. */
        if (sz < 0 || (long long)pos + 8 + (long long)sz > (long long)n.size)
            break;

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
        if (extc == 'k') { c.is_dir = true; c.archive_kind = AK_KMD; }
        n.children.push_back(std::move(c));
        emitted++;
        slot++;

        pos += 8 + sz;
        /* DAR entries aren't sector-aligned; the engine just walks
         * byte-by-byte. */
    }
    BDBG("[browser] dar '%s' -> %d entries\n", n.full.c_str(), emitted);
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
        case AK_KMD:      parse_kmd(n);      break;
        default: break;
    }
    /* KMD model children should stay in index order, not name-sorted. */
    if (n.archive_kind != AK_KMD) node_sort(n);
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

    /* Stash the first 16 bytes as a "magic" tag for the no-preview
     * diagnostic — useful for figuring out what an unknown blob is. */
    s_pv_magic_len = (int)(sz < 16 ? sz : 16);
    memcpy(s_pv_magic, buf, s_pv_magic_len);

    /* Decoder chain — pick the best match by extension, fall back to
     * magic-byte sniffing so the in-archive blobs without a real
     * extension still get the right view. The TIM and PCX paths
     * malloc; the stb path uses its own free — track which to use. */
    int w = 0, h = 0;
    uint8_t *rgba = NULL;
    enum { ALLOC_TIM, ALLOC_PCX, ALLOC_STB } alloc_kind = ALLOC_STB;

    /* TIM: explicit .tim or magic 10 00 00 00. */
    if (ext_is(n.full, ".tim") ||
        (sz >= 4 && buf[0] == 0x10 && buf[1] == 0x00 &&
                    buf[2] == 0x00 && buf[3] == 0x00)) {
        rgba = decode_tim(buf, sz, &w, &h);
        if (rgba) { s_pv_format = "TIM (PSX)"; alloc_kind = ALLOC_TIM; }
    }
    /* PCX: explicit .pcx or 0x0A magic. */
    if (!rgba && (ext_is(n.full, ".pcx") || (sz > 0 && buf[0] == 0x0A))) {
        const char *fmt = NULL;
        rgba = decode_pcx(buf, sz, &w, &h, &fmt);
        if (rgba) { s_pv_format = fmt ? fmt : "PCX"; alloc_kind = ALLOC_PCX; }
    }
    /* Standard formats via stb_image. */
    if (!rgba) {
        int comp = 0;
        uint8_t *r = stbi_load_from_memory(buf, (int)sz, &w, &h, &comp, 4);
        if (r) {
            rgba = r;
            const char *e = strrchr(n.full.c_str(), '.');
            s_pv_format = e ? (e + 1) : "image";
            for (auto &c : s_pv_format) c = (char)toupper((unsigned char)c);
            alloc_kind = ALLOC_STB;
        }
    }
    /* KMD model: parse the header and show a summary. If the user
     * clicked a specific model child, also try the textured render
     * path (load matching PCX textures from the parent stage's DARs);
     * fall back to wireframe if no textures are usable. */
    bool is_kmd = !rgba && try_decode_kmd(buf, sz);
    if (is_kmd && n.model_index >= 0) {
        /* Collect the model's material hashes so we know which PCXes
         * to fetch from the stage's DARs. Layout: per-face u16. */
        std::vector<uint16_t> wanted;
        if (sz >= KMD_DEF_SIZE) {
            int32_t nm = (int32_t)((uint32_t)buf[4] | ((uint32_t)buf[5]<<8) |
                                   ((uint32_t)buf[6]<<16) | ((uint32_t)buf[7]<<24));
            if (n.model_index < nm) {
                const uint8_t *mm = buf + KMD_DEF_SIZE +
                                    (long)n.model_index * KMD_MDL_SIZE;
                int32_t  n_faces = (int32_t)((uint32_t)mm[4] | ((uint32_t)mm[5]<<8) |
                                             ((uint32_t)mm[6]<<16) | ((uint32_t)mm[7]<<24));
                uint32_t materials_off =
                    (uint32_t)mm[80] | ((uint32_t)mm[81]<<8) |
                    ((uint32_t)mm[82]<<16) | ((uint32_t)mm[83]<<24);
                if (materials_off &&
                    (long)materials_off + (long)n_faces * 2 <= sz) {
                    const uint8_t *mat = buf + materials_off;
                    for (int f = 0; f < n_faces; f++) {
                        uint16_t mid = (uint16_t)mat[f*2] |
                                       ((uint16_t)mat[f*2 + 1] << 8);
                        if (mid == 0) continue;
                        bool dup = false;
                        for (auto w : wanted) if (w == mid) { dup = true; break; }
                        if (!dup) wanted.push_back(mid);
                    }
                }
            }
        }
        /* Try the textured path first. If it fails (no stage parent
         * known, or no matching PCXes, or no usable materials) we fall
         * back to the wireframe so the user always sees something. */
        if (n.stage_byte_offset >= 0 && !wanted.empty()) {
            stage_tex_load(n.host_iso, n.stage_byte_offset, wanted);
            kmd_preview_build_textured(buf, sz, n.model_index);
        }
        if (!s_kmd_have_tex) {
            kmd_preview_build_lines(buf, sz, n.model_index);
        }
        s_pv_format = s_kmd_have_tex ? "KMD model (textured)" : "KMD model";
    }

    free(buf);

    if (rgba) {
        upload_rgba(rgba, w, h);
        if (alloc_kind == ALLOC_STB) stbi_image_free(rgba);
        else                          free(rgba);
        return;
    }
    if (is_kmd) {
        s_pv_kind = PV_KMD;
        s_pv_format = "KMD (3D model)";
        return;
    }
    /* No decoder fit. Show a clean "no preview" pane (no hex view). */
    s_pv_kind = PV_UNSUPPORTED;
    const char *e = strrchr(n.full.c_str(), '.');
    s_pv_format = e ? (e + 1) : "raw";
    for (auto &c : s_pv_format) c = (char)toupper((unsigned char)c);
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
        else if (n.is_blob && n.archive_kind == AK_KMD)
            snprintf(label, sizeof label, "%s  [KMD]", n.name.c_str());
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

static void draw_preview_pane()
{
    if (s_pv_kind == PV_NONE) {
        ImGui::TextDisabled("Pick a file from the tree to preview.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Visual previews: PNG / JPG / BMP / TGA, PCX textures, "
            "TIM textures, and KMD 3D models (rotating wireframe).");
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
    if (s_pv_kind == PV_KMD) {
        /* 3D wireframe panel — render-into-FBO each frame so the model
         * spins on its own. Fit width minus padding, square-ish; use the
         * pane size so resizing the right column scales it. */
        if (s_kmd_have_model) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            int   w = (int)avail.x;
            int   h = (int)(w * 0.6f);
            if (h < 180) h = 180;
            if (h > 480) h = 480;
            if (w  < 64) w  = 64;
            kmd_preview_ensure_fbo(w, h);
            kmd_preview_render_frame();
            /* GL textures put origin at bottom-left; flip vertically. */
            ImGui::Image((ImTextureID)(intptr_t)s_kmd_color,
                         ImVec2((float)w, (float)h),
                         ImVec2(0, 1), ImVec2(1, 0));
        }
        ImGui::Text("Models: %d   (visible mask: %d)",
                    (int)s_pv_kmd.size(), s_pv_kmd_visible);
        ImGui::Text("Overall bbox:");
        ImGui::Indent();
        ImGui::Text("min  ( %.0f , %.0f , %.0f )",
                    s_pv_kmd_bbox_min[0], s_pv_kmd_bbox_min[1], s_pv_kmd_bbox_min[2]);
        ImGui::Text("max  ( %.0f , %.0f , %.0f )",
                    s_pv_kmd_bbox_max[0], s_pv_kmd_bbox_max[1], s_pv_kmd_bbox_max[2]);
        ImGui::Unindent();
        ImGui::Spacing();
        if (ImGui::BeginTable("##kmd_models", 5,
                              ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("#",       ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableSetupColumn("faces",   ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("verts",   ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("normals", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("bbox",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < s_pv_kmd.size(); i++) {
                const auto &m = s_pv_kmd[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%zu", i);
                ImGui::TableNextColumn(); ImGui::Text("%d", m.n_faces);
                ImGui::TableNextColumn(); ImGui::Text("%d", m.n_verts);
                ImGui::TableNextColumn(); ImGui::Text("%d", m.n_normals);
                ImGui::TableNextColumn();
                ImGui::Text("(%.0f,%.0f,%.0f) → (%.0f,%.0f,%.0f)",
                            m.min_x, m.min_y, m.min_z,
                            m.max_x, m.max_y, m.max_z);
            }
            ImGui::EndTable();
        }
        return;
    }
    if (s_pv_kind == PV_UNSUPPORTED) {
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.65f, 1.0f),
                           "No visual preview for this file type.");
        ImGui::Spacing();
        ImGui::TextDisabled("Magic bytes:");
        char magic[64] = {0}; char *p = magic;
        for (int i = 0; i < s_pv_magic_len && p < magic + sizeof(magic) - 4; i++)
            p += snprintf(p, 4, "%02x ", s_pv_magic[i]);
        ImGui::Text("  %s", magic);
        ImGui::TextDisabled("Decodable now: PNG/JPG/BMP/TGA, PCX, TIM, KMD.");
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
