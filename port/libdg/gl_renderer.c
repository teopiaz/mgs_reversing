/*
 * OpenGL rendering backend for the MGS macOS port.
 *
 * Phase 1: creates a GL 3.3 Core context on the SDL window and presents the
 * CPU-side VRAM array via a fullscreen-quad shader that samples a 1024x512
 * R16UI texture and decodes PSX 1-bit STP + 5-5-5 BGR to RGB.
 *
 * The software rasterizer in vram.c / libdg_stub.c still runs and writes into
 * vram[][]; this backend only replaces the final "VRAM -> window" step.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <SDL.h>

/* macOS ships GL 4.1 Core via the OpenGL framework. We don't need a loader. */
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>

#include "gl_renderer.h"

/* The CPU-side VRAM array lives in vram.c. */
extern uint16_t vram[512][1024];

static int          g_enabled = 0;
static struct SDL_Window *g_window = NULL;
static SDL_GLContext      g_ctx    = NULL;

static GLuint g_vao           = 0;
static GLuint g_vbo           = 0;
static GLuint g_vram_tex      = 0;
static GLuint g_zbuf_tex      = 0;   /* 320x224 R16UI -- 2D drawn-mask */
static GLuint g_blit_prog     = 0;
static GLint  g_blit_u_region = -1;  /* vec4 = (x, y, w, h) in VRAM texels */

/* Dirty-row tracking. Marks [dirty_y0, dirty_y1) as needing re-upload. */
static int g_dirty_y0 = 0;
static int g_dirty_y1 = 512;

/* 3D pipeline state. */
typedef struct {
    float    pos[3];      /* eye-space x, y, z (fixed-point world units as float) */
    float    dist;        /* chanl->clip_distance */
    float    uv[2];       /* 0..255 typical */
    float    face_z;      /* flat per-face eye-Z for depth test (painter's) */
    unsigned char rgba[4];/* modulation color, a unused */
    unsigned short tpage; /* PSX tpage register */
    unsigned short clut;  /* PSX CLUT index */
    unsigned short flags; /* bit 0 = textured */
    unsigned short _pad;
} GL3DVert;

static GL3DVert *g_tri3d_buf   = NULL;
static size_t    g_tri3d_count = 0;
static size_t    g_tri3d_cap   = 0;

static GLuint g_tri3d_vao   = 0;
static GLuint g_tri3d_vbo   = 0;
static GLuint g_tri3d_prog  = 0;
static GLint  g_tri3d_u_half_screen = -1;
static GLint  g_tri3d_u_near_far    = -1;

/* Stored by port_ClearImage; applied as glClearColor in gl_renderer_present. */
static float g_clear_rgb[3] = {0.0f, 0.0f, 0.0f};
static int   g_codec_mode   = 0;

/* 2D semi-transparent primitive pipeline.
 * Drawn between the 3D pass and the VRAM overlay so GL blends against the 3D
 * framebuffer -- matching PSX OT semi-trans behavior (radar, vision cones). */
typedef struct {
    float pos[2];      /* 0..320 x 0..224 (framebuffer pixel coords) */
    unsigned char rgba[4];
    int   abr;         /* bucket key: 0..3 */
} GL2DVert;

static GL2DVert *g_tri2d_buf   = NULL;
static size_t    g_tri2d_count = 0;
static size_t    g_tri2d_cap   = 0;

static GLuint g_tri2d_vao  = 0;
static GLuint g_tri2d_vbo  = 0;
static GLuint g_tri2d_prog = 0;

static const char *BLIT_VS =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID & 1) * 2, (gl_VertexID & 2));\n"
    "    vUV = p;                       // 0..2\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Samples the R16UI VRAM texture, decodes PSX pixel:
 *   bit 15 = semi-trans (ignored here -- pass through as opaque)
 *   bits 10-14 = B, 5-9 = G, 0-4 = R  (little-endian halfword)
 * In overlay mode (uDiscardZero != 0) pixel value 0x0000 is discarded so the
 * GL-rendered 3D scene shows through wherever the game has not drawn a 2D
 * primitive this frame.
 */
static const char *BLIT_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform usampler2D uVRAM;\n"
    "uniform vec4       uRegion;       // (x, y, w, h) in VRAM texels\n"
    "uniform int        uDiscardZero;  // 1 => PSX 0x0000 is transparent; 0 => opaque black\n"
    "out vec4 oColor;\n"
    "void main() {\n"
    "    vec2 uv = vUV;\n"
    "    if (uv.x > 1.0 || uv.y > 1.0) discard;\n"
    "    uv.y = 1.0 - uv.y;   // VRAM row 0 is image top; GL screen origin is bottom\n"
    "    ivec2 tc = ivec2(uRegion.xy + uv * uRegion.zw);\n"
    "    uint px = texelFetch(uVRAM, tc, 0).r;\n"
    "    if (uDiscardZero != 0 && px == 0u) discard;\n"
    "    float r = float(px & 0x1Fu) / 31.0;\n"
    "    float g = float((px >> 5) & 0x1Fu) / 31.0;\n"
    "    float b = float((px >> 10) & 0x1Fu) / 31.0;\n"
    "    oColor = vec4(r, g, b, 1.0);\n"
    "}\n";

/* 3D pass. Takes eye-space vertices + per-triangle projection distance and
 * produces clip-space positions directly; the GPU does perspective divide and
 * perspective-correct interpolation of UV and vertex color. */
static const char *TRI3D_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in float aDist;\n"
    "layout(location=2) in vec2 aUV;\n"
    "layout(location=3) in float aFaceZ;\n"
    "layout(location=4) in vec4 aCol;\n"
    "layout(location=5) in uvec4 aTex;   // tpage, clut, flags, _pad\n"
    "out vec2 vUV;\n"
    "out vec4 vCol;\n"
    "flat out uint vTPage;\n"
    "flat out uint vCLUT;\n"
    "flat out uint vFlags;\n"
    "uniform vec2 uHalfScreen;   // (160, 112) -- PSX logical half-screen\n"
    "uniform vec2 uNearFar;      // (near, far) in world units\n"
    "void main() {\n"
    "    float cz = aPos.z;\n"
    "    if (cz < 4.0) cz = 4.0;\n"
    "    float fz = aFaceZ;\n"
    "    if (fz < 4.0) fz = 4.0;\n"
    "    float n = uNearFar.x, f = uNearFar.y;\n"
    "    float A = (f + n) / (f - n);\n"
    "    float B = -2.0 * f * n / (f - n);\n"
    "    // We want z_ndc = (A*fz + B)/fz (constant per face) but w = cz (per-vertex\n"
    "    // for correct perspective divide on x/y). So z_clip = z_ndc * w.\n"
    "    float z_ndc = (A * fz + B) / fz;\n"
    "    gl_Position = vec4(\n"
    "        aPos.x * aDist / uHalfScreen.x,\n"
    "       -aPos.y * aDist / uHalfScreen.y,  // flip Y to GL up\n"
    "        z_ndc * cz,\n"
    "        cz);\n"
    "    vUV    = aUV;\n"
    "    vCol   = aCol;\n"
    "    vTPage = aTex.x;\n"
    "    vCLUT  = aTex.y;\n"
    "    vFlags = aTex.z;\n"
    "}\n";

static const char *TRI3D_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec4 vCol;\n"
    "flat in uint vTPage;\n"
    "flat in uint vCLUT;\n"
    "flat in uint vFlags;\n"
    "uniform usampler2D uVRAM;\n"
    "out vec4 oColor;\n"
    "uint fetchVRAM(int x, int y) { return texelFetch(uVRAM, ivec2(x, y), 0).r; }\n"
    "vec3 decodePSX(uint p) {\n"
    "    return vec3(\n"
    "        float(p & 0x1Fu) / 31.0,\n"
    "        float((p >> 5) & 0x1Fu) / 31.0,\n"
    "        float((p >> 10) & 0x1Fu) / 31.0);\n"
    "}\n"
    "void main() {\n"
    "    vec3 tex = vec3(1.0);\n"
    "    bool textured = (vFlags & 1u) != 0u;\n"
    "    if (textured) {\n"
    "        uint tp = (vTPage >> 7u) & 3u;\n"
    "        int base_x = int(vTPage & 0xFu) * 64;\n"
    "        int base_y = int((vTPage >> 4u) & 1u) * 256;\n"
    "        if ((vTPage & 0x800u) != 0u) base_y += 512;\n"
    "        int clut_x = int(vCLUT & 0x3Fu) * 16;\n"
    "        int clut_y = int((vCLUT >> 6u) & 0x1FFu);\n"
    "        int u = int(clamp(vUV.x, 0.0, 255.0));\n"
    "        int v = int(clamp(vUV.y, 0.0, 255.0));\n"
    "        uint texel;\n"
    "        if (tp == 0u) {\n"
    "            uint word = fetchVRAM(base_x + (u >> 2), base_y + v);\n"
    "            int idx = int((word >> uint((u & 3) * 4)) & 0xFu);\n"
    "            texel = fetchVRAM(clut_x + idx, clut_y);\n"
    "        } else if (tp == 1u) {\n"
    "            uint word = fetchVRAM(base_x + (u >> 1), base_y + v);\n"
    "            int idx = ((u & 1) == 1) ? int((word >> 8u) & 0xFFu) : int(word & 0xFFu);\n"
    "            texel = fetchVRAM(clut_x + idx, clut_y);\n"
    "        } else {\n"
    "            texel = fetchVRAM(base_x + u, base_y + v);\n"
    "        }\n"
    "        if (texel == 0u) discard;   // PSX palette index 0 / direct 0 => transparent\n"
    "        tex = decodePSX(texel);\n"
    "    }\n"
    "    // Match the port software rasterizer (vram.c draw_flat_tri):\n"
    "    //   textured:   out_channel = texel * vert / 128, clamp 31. vCol = vert/255,\n"
    "    //               so tex * (vCol * 2) gives correct neutral at vert=128.\n"
    "    //   untextured: base color is 0x4210 = (16/31 per ch) modulated by vert/128,\n"
    "    //               which simplifies to vert/255 (identity normalized).\n"
    "    vec3 modulated = textured ? (tex * (vCol.rgb * 2.0)) : vCol.rgb;\n"
    "    oColor = vec4(clamp(modulated, 0.0, 1.0), 1.0);\n"
    "}\n";

/* 2D semi-trans pass -- simple pixel-space shader. */
static const char *TRI2D_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;    // 0..320 x 0..224\n"
    "layout(location=1) in vec4 aCol;\n"
    "out vec4 vCol;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos.x / 160.0 - 1.0,\n"
    "                       1.0 - aPos.y / 112.0,\n"
    "                       0.0, 1.0);\n"
    "    vCol = aCol;\n"
    "}\n";

static const char *TRI2D_FS =
    "#version 330 core\n"
    "in vec4 vCol;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vec4(vCol.rgb, 1.0); }\n";

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "[gl] shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "[gl] program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

int gl_renderer_enabled(void)
{
    return g_enabled;
}

SDL_GLContext gl_renderer_context(void)
{
    return g_ctx;
}

int gl_renderer_init(void *window_)
{
    struct SDL_Window *window = (struct SDL_Window *)window_;
    const char *env = getenv("PORT_GL");
    if (!env || strcmp(env, "1") != 0) {
        g_enabled = 0;
        return 0;  /* GL disabled -- not an error. */
    }

    g_window = window;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_ctx = SDL_GL_CreateContext(window);
    if (!g_ctx) {
        fprintf(stderr, "[gl] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(window, g_ctx);
    SDL_GL_SetSwapInterval(1);  /* vsync */

    printf("[gl] context ready -- %s / %s\n",
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    /* Empty VAO -- the blit shader generates its own quad from gl_VertexID. */
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);

    /* R16UI texture mirroring vram[][]. */
    glGenTextures(1, &g_vram_tex);
    glBindTexture(GL_TEXTURE_2D, g_vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);

    /* 2D drawn-mask texture (port_zbuf mirror). */
    glGenTextures(1, &g_zbuf_tex);
    glBindTexture(GL_TEXTURE_2D, g_zbuf_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 320, 224, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);

    g_blit_prog = link_program(BLIT_VS, BLIT_FS);
    if (!g_blit_prog) {
        SDL_GL_DeleteContext(g_ctx);
        g_ctx = NULL;
        return -1;
    }
    g_blit_u_region = glGetUniformLocation(g_blit_prog, "uRegion");

    /* Bind sampler units once. uVRAM on unit 0, uZBuf on unit 1. */
    glUseProgram(g_blit_prog);
    glUniform1i(glGetUniformLocation(g_blit_prog, "uVRAM"), 0);
    glUniform1i(glGetUniformLocation(g_blit_prog, "uZBuf"), 1);
    glUseProgram(0);

    /* 3D pipeline. */
    g_tri3d_prog = link_program(TRI3D_VS, TRI3D_FS);
    if (!g_tri3d_prog) {
        SDL_GL_DeleteContext(g_ctx);
        g_ctx = NULL;
        return -1;
    }
    g_tri3d_u_half_screen = glGetUniformLocation(g_tri3d_prog, "uHalfScreen");
    g_tri3d_u_near_far    = glGetUniformLocation(g_tri3d_prog, "uNearFar");
    glUseProgram(g_tri3d_prog);
    GLint u_tex3d = glGetUniformLocation(g_tri3d_prog, "uVRAM");
    glUniform1i(u_tex3d, 0);
    glUseProgram(0);

    glGenVertexArrays(1, &g_tri3d_vao);
    glGenBuffers(1, &g_tri3d_vbo);
    glBindVertexArray(g_tri3d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_tri3d_vbo);

    GLsizei s = sizeof(GL3DVert);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, dist));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, face_z));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, (void *)offsetof(GL3DVert, rgba));
    glEnableVertexAttribArray(5);
    /* tpage, clut, flags, _pad -- four u16 as uvec4 */
    glVertexAttribIPointer(5, 4, GL_UNSIGNED_SHORT, s, (void *)offsetof(GL3DVert, tpage));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* 2D semi-trans pipeline. */
    g_tri2d_prog = link_program(TRI2D_VS, TRI2D_FS);
    if (!g_tri2d_prog) {
        SDL_GL_DeleteContext(g_ctx);
        g_ctx = NULL;
        return -1;
    }
    glGenVertexArrays(1, &g_tri2d_vao);
    glGenBuffers(1, &g_tri2d_vbo);
    glBindVertexArray(g_tri2d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_tri2d_vbo);
    GLsizei s2 = sizeof(GL2DVert);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s2, (void *)offsetof(GL2DVert, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, s2, (void *)offsetof(GL2DVert, rgba));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* First frame: upload everything. */
    g_dirty_y0 = 0;
    g_dirty_y1 = 512;

    g_enabled = 1;
    return 0;
}

void gl_renderer_shutdown(void)
{
    if (!g_enabled) return;
    if (g_tri3d_prog) glDeleteProgram(g_tri3d_prog);
    if (g_tri3d_vbo)  glDeleteBuffers(1, &g_tri3d_vbo);
    if (g_tri3d_vao)  glDeleteVertexArrays(1, &g_tri3d_vao);
    free(g_tri3d_buf);
    g_tri3d_buf = NULL;
    g_tri3d_cap = g_tri3d_count = 0;
    if (g_tri2d_prog) glDeleteProgram(g_tri2d_prog);
    if (g_tri2d_vbo)  glDeleteBuffers(1, &g_tri2d_vbo);
    if (g_tri2d_vao)  glDeleteVertexArrays(1, &g_tri2d_vao);
    free(g_tri2d_buf);
    g_tri2d_buf = NULL;
    g_tri2d_cap = g_tri2d_count = 0;
    if (g_blit_prog) glDeleteProgram(g_blit_prog);
    if (g_vram_tex)  glDeleteTextures(1, &g_vram_tex);
    if (g_zbuf_tex)  glDeleteTextures(1, &g_zbuf_tex);
    if (g_vbo)       glDeleteBuffers(1, &g_vbo);
    if (g_vao)       glDeleteVertexArrays(1, &g_vao);
    if (g_ctx)       SDL_GL_DeleteContext(g_ctx);
    g_enabled = 0;
}

void gl_renderer_set_clear_color(int r8, int g8, int b8)
{
    g_clear_rgb[0] = (float)r8 / 255.0f;
    g_clear_rgb[1] = (float)g8 / 255.0f;
    g_clear_rgb[2] = (float)b8 / 255.0f;
}

void gl_renderer_begin_3d(void) { if (g_enabled) g_tri3d_count = 0; }
void gl_renderer_begin_2d(void) { if (g_enabled) g_tri2d_count = 0; }
void gl_renderer_set_codec_mode(int on) { g_codec_mode = on ? 1 : 0; }

static void tri3d_reserve(size_t extra)
{
    if (g_tri3d_count + extra <= g_tri3d_cap) return;
    size_t ncap = g_tri3d_cap ? g_tri3d_cap * 2 : 4096;
    while (ncap < g_tri3d_count + extra) ncap *= 2;
    g_tri3d_buf = (GL3DVert *)realloc(g_tri3d_buf, ncap * sizeof(GL3DVert));
    g_tri3d_cap = ncap;
}

static void pack_vert(GL3DVert *v,
    const int xyz[3], const int uv[2], const unsigned char rgb[3],
    int dist, int face_z,
    unsigned short tpage, unsigned short clut, unsigned short flags)
{
    v->pos[0] = (float)xyz[0];
    v->pos[1] = (float)xyz[1];
    v->pos[2] = (float)xyz[2];
    v->dist   = (float)dist;
    v->uv[0]  = (float)uv[0];
    v->uv[1]  = (float)uv[1];
    v->face_z = (float)face_z;
    v->rgba[0] = rgb[0];
    v->rgba[1] = rgb[1];
    v->rgba[2] = rgb[2];
    v->rgba[3] = 255;
    v->tpage = tpage;
    v->clut  = clut;
    v->flags = flags;
    v->_pad  = 0;
}

void gl_submit_tri3d(
    const int a[3], const int b[3], const int c[3],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    int dist, int face_z,
    unsigned short tpage, unsigned short clut, unsigned short flags)
{
    if (!g_enabled) return;
    tri3d_reserve(3);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], a, uv_a, col_a, dist, face_z, tpage, clut, flags);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], b, uv_b, col_b, dist, face_z, tpage, clut, flags);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], c, uv_c, col_c, dist, face_z, tpage, clut, flags);
}

static void tri2d_reserve(size_t extra)
{
    if (g_tri2d_count + extra <= g_tri2d_cap) return;
    size_t ncap = g_tri2d_cap ? g_tri2d_cap * 2 : 1024;
    while (ncap < g_tri2d_count + extra) ncap *= 2;
    g_tri2d_buf = (GL2DVert *)realloc(g_tri2d_buf, ncap * sizeof(GL2DVert));
    g_tri2d_cap = ncap;
}

static void pack_vert2d(GL2DVert *v, const int xy[2], const unsigned char rgb[3], int abr)
{
    v->pos[0] = (float)xy[0];
    v->pos[1] = (float)xy[1];
    v->rgba[0] = rgb[0];
    v->rgba[1] = rgb[1];
    v->rgba[2] = rgb[2];
    v->rgba[3] = 255;
    v->abr = abr;
}

void gl_submit_tri2d_semitrans(
    const int a[2], const int b[2], const int c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    int abr)
{
    if (!g_enabled) return;
    if (abr < 0 || abr > 3) abr = 0;
    tri2d_reserve(3);
    pack_vert2d(&g_tri2d_buf[g_tri2d_count++], a, col_a, abr);
    pack_vert2d(&g_tri2d_buf[g_tri2d_count++], b, col_b, abr);
    pack_vert2d(&g_tri2d_buf[g_tri2d_count++], c, col_c, abr);
}

/* Configure GL blend state for a given PSX ABR mode, matching the port
 * software rasterizer in vram.c (draw_flat_tri):
 *   0: (B + F) / 2  -- average
 *   1: B + F        -- additive
 *   2: B - F        -- subtractive
 *   3: B + F/4      -- quarter additive
 */
static void apply_abr(int abr)
{
    switch (abr) {
    case 0:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
        glBlendColor(0.5f, 0.5f, 0.5f, 0.5f);
        break;
    case 1:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case 2:
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case 3:
    default:
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
        glBlendColor(0.25f, 0.25f, 0.25f, 0.25f);
        break;
    }
}

/* Flush all queued 2D semi-trans triangles, grouped by ABR bucket. Called
 * between the 3D pass and the VRAM overlay so blending reads the 3D FB. */
static void flush_tri2d(void)
{
    if (g_tri2d_count == 0) return;

    glBindVertexArray(g_tri2d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_tri2d_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(g_tri2d_count * sizeof(GL2DVert)),
                 g_tri2d_buf, GL_STREAM_DRAW);

    glUseProgram(g_tri2d_prog);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    /* Walk submissions and flush runs of identical ABR. Preserves OT order. */
    size_t i = 0;
    while (i < g_tri2d_count) {
        int abr = g_tri2d_buf[i].abr;
        size_t run_start = i;
        while (i < g_tri2d_count && g_tri2d_buf[i].abr == abr) i++;
        apply_abr(abr);
        glDrawArrays(GL_TRIANGLES, (GLint)run_start, (GLsizei)(i - run_start));
    }

    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);     /* restore default */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    /* Do NOT reset g_tri2d_count -- see gl_renderer_present comment. */
}

void gl_renderer_mark_vram_dirty(int y0, int y1)
{
    if (!g_enabled) return;
    if (y0 < 0)   y0 = 0;
    if (y1 > 512) y1 = 512;
    if (y0 >= y1) return;
    if (g_dirty_y0 >= g_dirty_y1) {  /* empty */
        g_dirty_y0 = y0;
        g_dirty_y1 = y1;
    } else {
        if (y0 < g_dirty_y0) g_dirty_y0 = y0;
        if (y1 > g_dirty_y1) g_dirty_y1 = y1;
    }
}

void gl_renderer_begin_frame(void)
{
    if (!g_enabled) return;
    /* Phase 1: nothing -- software rasterizer writes into vram[][]. */
}

static void upload_dirty_vram_rows(void)
{
    if (g_dirty_y0 >= g_dirty_y1) return;
    glBindTexture(GL_TEXTURE_2D, g_vram_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, g_dirty_y0,
                    1024, g_dirty_y1 - g_dirty_y0,
                    GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                    &vram[g_dirty_y0][0]);
    g_dirty_y0 = 512;
    g_dirty_y1 = 0;
}

void gl_renderer_present(void)
{
    if (!g_enabled) return;

    /* The game just finished writing 2D primitives into vram[][0..223]; push
       dirty rows to the GPU. We unconditionally mark framebuffer rows dirty
       because the 2D OT walker doesn't track them per-pixel yet. */
    gl_renderer_mark_vram_dirty(0, 224);
    upload_dirty_vram_rows();

    int fb_w = 0, fb_h = 0;
    SDL_GL_GetDrawableSize(g_window, &fb_w, &fb_h);
    if (fb_w <= 0 || fb_h <= 0) { fb_w = 640; fb_h = 448; }

    /* Aspect-correct 320:224 inside the window. */
    const float target = 320.0f / 224.0f;
    float wf = (float)fb_w;
    float hf = (float)fb_h;
    int vp_w = fb_w, vp_h = fb_h, vp_x = 0, vp_y = 0;
    if (wf / hf > target) {
        vp_w = (int)(hf * target + 0.5f);
        vp_x = (fb_w - vp_w) / 2;
    } else {
        vp_h = (int)(wf / target + 0.5f);
        vp_y = (fb_h - vp_h) / 2;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);

    /* Phase 1: black letterbox bars. */
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Game content area. */
    glViewport(vp_x, vp_y, vp_w, vp_h);

    /* Clear with the PSX framebuffer background color (stored by
       port_ClearImage when GL is enabled) + depth buffer. */
    glClearColor(g_clear_rgb[0], g_clear_rgb[1], g_clear_rgb[2], 1.0f);
    glEnable(GL_DEPTH_TEST);
    /* LEQUAL + flat face_z gives painter's-algorithm semantics: later-submitted
       faces at the same centroid depth win, matching PSX OT-sort behavior. */
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Debug VRAM view: skip 3D, just dump VRAM as before. */
    extern int port_vram_debug_view(void);
    int debug_view = port_vram_debug_view();

    /* --- 3D pass -------------------------------------------------------- */
    if (!debug_view && g_tri3d_count > 0) {
        glBindVertexArray(g_tri3d_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_tri3d_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(g_tri3d_count * sizeof(GL3DVert)),
                     g_tri3d_buf, GL_STREAM_DRAW);

        glUseProgram(g_tri3d_prog);
        glUniform2f(g_tri3d_u_half_screen, 160.0f, 112.0f);
        glUniform2f(g_tri3d_u_near_far, 4.0f, 32768.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_vram_tex);

        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);  /* port_RenderObjects already does backface cull */

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_tri3d_count);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    /* --- 2D semi-trans pass -------------------------------------------- */
    /* Blends radar darkening / vision cones / smoke fade against the 3D FB. */
    if (!debug_view) flush_tri2d();

    /* --- 2D overlay ----------------------------------------------------- */
    /* Normally the overlay treats PSX pixel 0x0000 as transparent so the 3D
       scene shows through. During codec (DG_FrameRate==2) there is no 3D,
       the game clears the framebuffer to opaque black (value 0), and we must
       NOT treat that as transparent -- otherwise black codec backdrops
       render as the letterbox/clear color. g_codec_mode switches that off. */
    glDisable(GL_DEPTH_TEST);
    glUseProgram(g_blit_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vram_tex);
    if (debug_view) {
        glUniform4f(g_blit_u_region, 0.0f, 0.0f, 1024.0f, 512.0f);
        glUniform1i(glGetUniformLocation(g_blit_prog, "uDiscardZero"), 0);
    } else {
        glUniform4f(g_blit_u_region, 0.0f, 0.0f, 320.0f, 224.0f);
        glUniform1i(glGetUniformLocation(g_blit_prog, "uDiscardZero"),
                    g_codec_mode ? 0 : 1);
    }
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);

    /* Note: we do NOT reset g_tri3d_count / g_tri2d_count here. The game logic
       runs at 30 Hz but rendering runs at 60 Hz, so "idle" render frames must
       redraw the same accumulated content. The buffers are reset when the
       game logic starts a new tick (port_RenderObjects -> gl_renderer_begin_3d,
       port_DrawOTag -> gl_renderer_begin_2d). */

    /* SwapWindow happens in port_render after ImGui draws. */
}
