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
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

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
    unsigned char rgba[4];/* Gouraud fallback color (used when !per-pixel) */
    unsigned short tpage;
    unsigned short clut;
    unsigned short flags; /* b0 textured, b1 semi-trans, b2-3 ABR, b4 per-pixel lit */
    unsigned short _pad;
    /* Per-pixel lighting (PSX NCS formula). All stored as floats in
       "fixed-point /4096" convention -- see PSX 4.12 SVECTOR/MATRIX. */
    float    normal[3];       /* vertex normal (SVECTOR / 4096) */
    float    lightDir[9];     /* 3x3 light direction matrix (row-major) */
    float    lightColor[9];   /* 3x3 color contribution matrix (row-major) */
    float    ambient[3];      /* background / ambient color in [0,1] */
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

/* Hi-res internal FBO. All GL rendering targets this; we blit it upscaled to
 * the window at the end of present via glBlitFramebuffer (linear filter). */
static int    g_scale     = 4;
static int    g_fbo_w     = 320 * 4;
static int    g_fbo_h     = 224 * 4;
static GLuint g_fbo       = 0;
static GLuint g_fbo_color = 0;
static GLuint g_fbo_depth = 0;

/* Debug visualisation flags -- read by gl_renderer_present / shaders. */
int   gl_debug_wireframe      = 0;
int   gl_debug_no_textures    = 0;
int   gl_debug_skip_3d        = 0;
int   gl_debug_skip_2d        = 0;
int   gl_debug_skip_lines     = 0;
int   gl_debug_blit_nearest   = 0;
int   gl_debug_no_cull        = 0;
int   gl_debug_cull_cw        = 1;  /* 1 = CW front (default, matches PSX), 0 = CCW */
int   gl_debug_face_id        = 0;
int   gl_debug_show_normals   = 0;
int   gl_debug_clear_override = 0;
float gl_debug_clear_rgb[3]   = {1.0f, 0.0f, 1.0f};  /* magenta default */

/* 2D primitive pipeline. Handles all screen-space primitives (TILE, POLY_F*,
 * POLY_G*, POLY_FT*, POLY_GT*, SPRT*). Flags encode textured / semi-trans /
 * ABR mode so one batch can cover every primitive shape, switching state in
 * runs at flush time. Lines use a parallel VBO with the same vertex format. */
typedef struct {
    float pos[2];         /* 0..320 x 0..224 (framebuffer pixel coords) */
    float uv[2];          /* 0..255 typical; ignored when !textured */
    unsigned char rgba[4];
    unsigned short tpage; /* PSX tpage */
    unsigned short clut;  /* PSX CLUT */
    unsigned short flags; /* bit 0 textured, bit 1 semi-trans, bits 2..3 ABR */
    unsigned short _pad;
} GL2DVert;

static GL2DVert *g_tri2d_buf    = NULL;
static size_t    g_tri2d_count  = 0;
static size_t    g_tri2d_cap    = 0;
static GL2DVert *g_line2d_buf   = NULL;
static size_t    g_line2d_count = 0;
static size_t    g_line2d_cap   = 0;

static GLuint g_tri2d_vao   = 0;
static GLuint g_tri2d_vbo   = 0;
static GLuint g_line2d_vao  = 0;
static GLuint g_line2d_vbo  = 0;
static GLuint g_tri2d_prog  = 0;

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
    "layout(location=0)  in vec3  aPos;\n"
    "layout(location=1)  in float aDist;\n"
    "layout(location=2)  in vec2  aUV;\n"
    "layout(location=3)  in float aFaceZ;\n"
    "layout(location=4)  in vec4  aCol;\n"
    "layout(location=5)  in uvec4 aTex;         // tpage, clut, flags, _pad\n"
    "layout(location=6)  in vec3  aNormal;      // per-vertex normal (4.12 / 4096)\n"
    "layout(location=7)  in mat3  aLightDir;    // takes slots 7,8,9\n"
    "layout(location=10) in mat3  aLightColor;  // slots 10,11,12\n"
    "layout(location=13) in vec3  aAmbient;\n"
    "out vec2 vUV;\n"
    "out vec4 vCol;\n"
    "out vec3 vNormal;                   // smooth-interpolated across tri\n"
    "flat out uint vTPage;\n"
    "flat out uint vCLUT;\n"
    "flat out uint vFlags;\n"
    "flat out mat3 vLightDir;            // constant per tri\n"
    "flat out mat3 vLightColor;\n"
    "flat out vec3 vAmbient;\n"
    "uniform vec2 uHalfScreen;           // (160, 112)\n"
    "uniform vec2 uNearFar;              // (near, far)\n"
    "void main() {\n"
    "    float cz = aPos.z;\n"
    "    if (cz < 4.0) cz = 4.0;\n"
    "    float fz = aFaceZ;\n"
    "    if (fz < 4.0) fz = 4.0;\n"
    "    float n = uNearFar.x, f = uNearFar.y;\n"
    "    float A = (f + n) / (f - n);\n"
    "    float B = -2.0 * f * n / (f - n);\n"
    "    float z_ndc = (A * fz + B) / fz;\n"
    "    gl_Position = vec4(\n"
    "        aPos.x * aDist / uHalfScreen.x,\n"
    "       -aPos.y * aDist / uHalfScreen.y,\n"
    "        z_ndc * cz,\n"
    "        cz);\n"
    "    vUV         = aUV;\n"
    "    vCol        = aCol;\n"
    "    vTPage      = aTex.x;\n"
    "    vCLUT       = aTex.y;\n"
    "    vFlags      = aTex.z;\n"
    "    vNormal     = aNormal;\n"
    "    vLightDir   = aLightDir;\n"
    "    vLightColor = aLightColor;\n"
    "    vAmbient    = aAmbient;\n"
    "}\n";

static const char *TRI3D_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec4 vCol;\n"
    "in vec3 vNormal;\n"
    "flat in uint vTPage;\n"
    "flat in uint vCLUT;\n"
    "flat in uint vFlags;\n"
    "flat in mat3 vLightDir;\n"
    "flat in mat3 vLightColor;\n"
    "flat in vec3 vAmbient;\n"
    "uniform usampler2D uVRAM;\n"
    "uniform int uNoTextures;    // debug: 1 => skip texture sample\n"
    "uniform int uFaceId;        // debug: 1 => color tri by gl_PrimitiveID\n"
    "uniform int uShowNormals;   // debug: 1 => output normal.xyz*0.5+0.5\n"
    "out vec4 oColor;\n"
    "uint fetchVRAM(int x, int y) { return texelFetch(uVRAM, ivec2(x, y), 0).r; }\n"
    "vec3 decodePSX(uint p) {\n"
    "    return vec3(\n"
    "        float(p & 0x1Fu) / 31.0,\n"
    "        float((p >> 5) & 0x1Fu) / 31.0,\n"
    "        float((p >> 10) & 0x1Fu) / 31.0);\n"
    "}\n"
    "// Cheap hash -> RGB. Used by the face-ID debug visualisation.\n"
    "vec3 hashId(int i) {\n"
    "    uint u = uint(i) * 2654435761u;\n"
    "    return vec3(\n"
    "        float((u      ) & 0xFFu) / 255.0,\n"
    "        float((u >>  8) & 0xFFu) / 255.0,\n"
    "        float((u >> 16) & 0xFFu) / 255.0);\n"
    "}\n"
    "void main() {\n"
    "    // Debug overrides take precedence over the normal lit path.\n"
    "    if (uShowNormals != 0) {\n"
    "        vec3 n = normalize(vNormal);\n"
    "        oColor = vec4(n * 0.5 + 0.5, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (uFaceId != 0) {\n"
    "        oColor = vec4(hashId(gl_PrimitiveID), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 tex = vec3(1.0);\n"
    "    bool textured = (vFlags & 1u) != 0u && (uNoTextures == 0);\n"
    "    bool per_pixel = (vFlags & 16u) != 0u;\n"
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
    "        if (texel == 0u) discard;\n"
    "        tex = decodePSX(texel);\n"
    "    }\n"
    "\n"
    "    // Pick the shading source. The Gouraud vertex color vCol is linear-\n"
    "    // interpolated across the triangle; per-pixel recomputes the PSX NCS\n"
    "    // formula at every fragment using the interpolated normal.\n"
    "    //   PSX GTE:  IR  = LightDir * Normal       (clamp >= 0)\n"
    "    //             RGB = LightColor * IR + Ambient\n"
    "    // Both matrices and normals are 4.12 fixed-point (/4096 in our floats).\n"
    "    // The Gouraud branch supplies the caller-computed shade pipeline result\n"
    "    // in vCol, normalized so 0.5 (=128/255) is neutral-bright.\n"
    "    vec3 shade;\n"
    "    if (per_pixel) {\n"
    "        vec3 n = normalize(vNormal);\n"
    "        // PSX MATRIX is row-major (shorts[0..8] = m[0][0..2], m[1][0..2], ...)\n"
    "        // while GLSL mat3 is column-major, so our uploaded mat3 holds the\n"
    "        // transpose of the PSX matrix. To compute PSX_mat * v we use\n"
    "        // `v * M` (left-multiply by row vector), which in GLSL equals\n"
    "        // (M^T * v). Applied to our transposed M that recovers PSX_mat*v.\n"
    "        vec3 ir = max(n * vLightDir, 0.0);\n"
    "        // Guaranteed floor so back-facing fragments aren't pitch-black when\n"
    "        // an object has no DG_FLAG_AMBIENT (typical for prop geometry).\n"
    "        vec3 amb = max(vAmbient, vec3(0.15));\n"
    "        shade = clamp(ir * vLightColor + amb, 0.0, 1.0);\n"
    "    } else {\n"
    "        // Gouraud: match the software path (tex * vCol * 2, neutral at 0.5)\n"
    "        shade = vCol.rgb * 2.0;\n"
    "    }\n"
    "    vec3 rgb = textured ? tex * shade : (per_pixel ? shade : vCol.rgb);\n"
    "    oColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);\n"
    "}\n";

/* Unified 2D shader -- pixel-space triangles (and lines), optional texture
 * with CLUT decode (same logic as the 3D FS). Per-vertex color modulates
 * the texel when textured, or supplies the color directly when flat. */
static const char *TRI2D_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;    // 0..320 x 0..224\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aCol;\n"
    "layout(location=3) in uvec4 aTex;   // tpage, clut, flags, _pad\n"
    "out vec2 vUV;\n"
    "out vec4 vCol;\n"
    "flat out uint vTPage;\n"
    "flat out uint vCLUT;\n"
    "flat out uint vFlags;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos.x / 160.0 - 1.0,\n"
    "                       1.0 - aPos.y / 112.0,\n"
    "                       0.0, 1.0);\n"
    "    vUV = aUV;\n"
    "    vCol = aCol;\n"
    "    vTPage = aTex.x;\n"
    "    vCLUT  = aTex.y;\n"
    "    vFlags = aTex.z;\n"
    "}\n";

static const char *TRI2D_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec4 vCol;\n"
    "flat in uint vTPage;\n"
    "flat in uint vCLUT;\n"
    "flat in uint vFlags;\n"
    "uniform usampler2D uVRAM;\n"
    "uniform int uNoTextures;\n"
    "out vec4 oColor;\n"
    "uint fetchVRAM(int x, int y) { return texelFetch(uVRAM, ivec2(x, y), 0).r; }\n"
    "vec3 decodePSX(uint p) {\n"
    "    return vec3(\n"
    "        float(p & 0x1Fu) / 31.0,\n"
    "        float((p >> 5) & 0x1Fu) / 31.0,\n"
    "        float((p >> 10) & 0x1Fu) / 31.0);\n"
    "}\n"
    "void main() {\n"
    "    bool textured = (vFlags & 1u) != 0u && (uNoTextures == 0);\n"
    "    vec3 out_rgb;\n"
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
    "        if (texel == 0u) discard;   // PSX transparent\n"
    "        vec3 tex = decodePSX(texel);\n"
    "        out_rgb = clamp(tex * (vCol.rgb * 2.0), 0.0, 1.0);\n"
    "    } else {\n"
    "        out_rgb = vCol.rgb;\n"
    "    }\n"
    "    oColor = vec4(out_rgb, 1.0);\n"
    "}\n";

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
    glVertexAttribIPointer(5, 4, GL_UNSIGNED_SHORT, s, (void *)offsetof(GL3DVert, tpage));
    /* Per-pixel lighting attributes: normal + light matrices + ambient. */
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, normal));
    /* mat3 takes 3 consecutive attribute slots. */
    for (int c = 0; c < 3; c++) {
        glEnableVertexAttribArray(7 + c);
        glVertexAttribPointer(7 + c, 3, GL_FLOAT, GL_FALSE, s,
            (void *)(offsetof(GL3DVert, lightDir) + c * 3 * sizeof(float)));
        glEnableVertexAttribArray(10 + c);
        glVertexAttribPointer(10 + c, 3, GL_FLOAT, GL_FALSE, s,
            (void *)(offsetof(GL3DVert, lightColor) + c * 3 * sizeof(float)));
    }
    glEnableVertexAttribArray(13);
    glVertexAttribPointer(13, 3, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, ambient));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* 2D semi-trans pipeline. */
    g_tri2d_prog = link_program(TRI2D_VS, TRI2D_FS);
    if (!g_tri2d_prog) {
        SDL_GL_DeleteContext(g_ctx);
        g_ctx = NULL;
        return -1;
    }
    GLsizei s2 = sizeof(GL2DVert);

    glGenVertexArrays(1, &g_tri2d_vao);
    glGenBuffers(1, &g_tri2d_vbo);
    glBindVertexArray(g_tri2d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_tri2d_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s2, (void *)offsetof(GL2DVert, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s2, (void *)offsetof(GL2DVert, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, s2, (void *)offsetof(GL2DVert, rgba));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, s2, (void *)offsetof(GL2DVert, tpage));

    /* Separate VAO/VBO for lines -- same vertex format, GL_LINES primitive. */
    glGenVertexArrays(1, &g_line2d_vao);
    glGenBuffers(1, &g_line2d_vbo);
    glBindVertexArray(g_line2d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line2d_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s2, (void *)offsetof(GL2DVert, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s2, (void *)offsetof(GL2DVert, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, s2, (void *)offsetof(GL2DVert, rgba));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, s2, (void *)offsetof(GL2DVert, tpage));

    /* Sampler unit for 2D textured prims. */
    glUseProgram(g_tri2d_prog);
    glUniform1i(glGetUniformLocation(g_tri2d_prog, "uVRAM"), 0);
    glUseProgram(0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* First frame: upload everything. */
    g_dirty_y0 = 0;
    g_dirty_y1 = 512;

    /* --- Hi-res internal FBO ---------------------------------------------
       Every pass renders here at 320*N x 224*N, then we blit to the window
       with linear filter. PORT_GL_SCALE controls N (default 4). */
    {
        const char *sc = getenv("PORT_GL_SCALE");
        int n = sc ? atoi(sc) : 4;
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        g_scale = n;
        g_fbo_w = 320 * n;
        g_fbo_h = 224 * n;

        glGenFramebuffers(1, &g_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

        glGenTextures(1, &g_fbo_color);
        glBindTexture(GL_TEXTURE_2D, g_fbo_color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, g_fbo_color, 0);

        glGenRenderbuffers(1, &g_fbo_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, g_fbo_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              g_fbo_w, g_fbo_h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, g_fbo_depth);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[gl] FBO incomplete: 0x%x (scale=%d %dx%d)\n",
                    status, n, g_fbo_w, g_fbo_h);
            SDL_GL_DeleteContext(g_ctx);
            g_ctx = NULL;
            return -1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        printf("[gl] FBO %dx%d (PORT_GL_SCALE=%d)\n", g_fbo_w, g_fbo_h, n);
    }

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
    if (g_tri2d_prog)  glDeleteProgram(g_tri2d_prog);
    if (g_tri2d_vbo)   glDeleteBuffers(1, &g_tri2d_vbo);
    if (g_tri2d_vao)   glDeleteVertexArrays(1, &g_tri2d_vao);
    if (g_line2d_vbo)  glDeleteBuffers(1, &g_line2d_vbo);
    if (g_line2d_vao)  glDeleteVertexArrays(1, &g_line2d_vao);
    free(g_tri2d_buf);
    free(g_line2d_buf);
    g_tri2d_buf = g_line2d_buf = NULL;
    g_tri2d_cap = g_tri2d_count = 0;
    g_line2d_cap = g_line2d_count = 0;
    if (g_fbo_depth) glDeleteRenderbuffers(1, &g_fbo_depth);
    if (g_fbo_color) glDeleteTextures(1, &g_fbo_color);
    if (g_fbo)       glDeleteFramebuffers(1, &g_fbo);
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

int gl_renderer_get_scale(void) { return g_scale; }

void gl_renderer_set_scale(int n)
{
    if (!g_enabled) return;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    if (n == g_scale) return;

    /* Rebuild the color texture + depth renderbuffer at the new size. */
    g_scale = n;
    g_fbo_w = 320 * n;
    g_fbo_h = 224 * n;

    glBindTexture(GL_TEXTURE_2D, g_fbo_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, g_fbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          g_fbo_w, g_fbo_h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    printf("[gl] FBO resized to %dx%d (scale=%d)\n", g_fbo_w, g_fbo_h, g_scale);
}

void gl_renderer_begin_3d(void) { if (g_enabled) g_tri3d_count = 0; }
void gl_renderer_begin_2d(void) {
    if (!g_enabled) return;
    g_tri2d_count  = 0;
    g_line2d_count = 0;
}
void gl_renderer_set_codec_mode(int on) { g_codec_mode = on ? 1 : 0; }

static void tri3d_reserve(size_t extra)
{
    if (g_tri3d_count + extra <= g_tri3d_cap) return;
    size_t ncap = g_tri3d_cap ? g_tri3d_cap * 2 : 4096;
    while (ncap < g_tri3d_count + extra) ncap *= 2;
    g_tri3d_buf = (GL3DVert *)realloc(g_tri3d_buf, ncap * sizeof(GL3DVert));
    g_tri3d_cap = ncap;
}

static void pack_light(GL3DVert *v, const short *normal, const GLLight *light)
{
    /* Normal: 4.12 fixed-point short -> unit-ish float. */
    if (normal) {
        v->normal[0] = (float)normal[0] / 4096.0f;
        v->normal[1] = (float)normal[1] / 4096.0f;
        v->normal[2] = (float)normal[2] / 4096.0f;
    } else {
        v->normal[0] = v->normal[1] = 0.0f; v->normal[2] = 1.0f;
    }
    /* Light state: 9 shorts per matrix, /4096. Ambient: 3 ints, /255. */
    if (light && light->light_dir) {
        for (int i = 0; i < 9; i++) v->lightDir[i] = (float)light->light_dir[i] / 4096.0f;
    } else {
        /* Identity with only row 0 active = falls back to Lambert-ish. */
        for (int i = 0; i < 9; i++) v->lightDir[i] = 0.0f;
    }
    if (light && light->light_color) {
        for (int i = 0; i < 9; i++) v->lightColor[i] = (float)light->light_color[i] / 4096.0f;
    } else {
        for (int i = 0; i < 9; i++) v->lightColor[i] = 0.0f;
    }
    if (light && light->ambient) {
        v->ambient[0] = (float)light->ambient[0] / 255.0f;
        v->ambient[1] = (float)light->ambient[1] / 255.0f;
        v->ambient[2] = (float)light->ambient[2] / 255.0f;
    } else {
        v->ambient[0] = v->ambient[1] = v->ambient[2] = 0.0f;
    }
}

static void pack_vert(GL3DVert *v,
    const int xyz[3], const int uv[2], const unsigned char rgb[3],
    const short *normal, const GLLight *light,
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
    pack_light(v, normal, light);
}

void gl_submit_tri3d(
    const int a[3], const int b[3], const int c[3],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    const short *na, const short *nb, const short *nc,
    const GLLight *light,
    int dist, int face_z,
    unsigned short tpage, unsigned short clut, unsigned short flags)
{
    if (!g_enabled) return;
    tri3d_reserve(3);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], a, uv_a, col_a, na, light, dist, face_z, tpage, clut, flags);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], b, uv_b, col_b, nb, light, dist, face_z, tpage, clut, flags);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], c, uv_c, col_c, nc, light, dist, face_z, tpage, clut, flags);
}

static void tri2d_reserve(size_t extra)
{
    if (g_tri2d_count + extra <= g_tri2d_cap) return;
    size_t ncap = g_tri2d_cap ? g_tri2d_cap * 2 : 1024;
    while (ncap < g_tri2d_count + extra) ncap *= 2;
    g_tri2d_buf = (GL2DVert *)realloc(g_tri2d_buf, ncap * sizeof(GL2DVert));
    g_tri2d_cap = ncap;
}

static void line2d_reserve(size_t extra)
{
    if (g_line2d_count + extra <= g_line2d_cap) return;
    size_t ncap = g_line2d_cap ? g_line2d_cap * 2 : 256;
    while (ncap < g_line2d_count + extra) ncap *= 2;
    g_line2d_buf = (GL2DVert *)realloc(g_line2d_buf, ncap * sizeof(GL2DVert));
    g_line2d_cap = ncap;
}

static void pack_vert2d(GL2DVert *v,
    const int xy[2], const int uv[2], const unsigned char rgb[3],
    unsigned short tpage, unsigned short clut, unsigned short flags)
{
    v->pos[0] = (float)xy[0];
    v->pos[1] = (float)xy[1];
    v->uv[0]  = uv ? (float)uv[0] : 0.0f;
    v->uv[1]  = uv ? (float)uv[1] : 0.0f;
    v->rgba[0] = rgb[0];
    v->rgba[1] = rgb[1];
    v->rgba[2] = rgb[2];
    v->rgba[3] = 255;
    v->tpage = tpage;
    v->clut  = clut;
    v->flags = flags;
    v->_pad  = 0;
}

void gl_submit_tri2d(
    const int a[2], const int b[2], const int c[2],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    unsigned short tpage, unsigned short clut, unsigned short flags)
{
    if (!g_enabled) return;
    tri2d_reserve(3);
    pack_vert2d(&g_tri2d_buf[g_tri2d_count++], a, uv_a, col_a, tpage, clut, flags);
    pack_vert2d(&g_tri2d_buf[g_tri2d_count++], b, uv_b, col_b, tpage, clut, flags);
    pack_vert2d(&g_tri2d_buf[g_tri2d_count++], c, uv_c, col_c, tpage, clut, flags);
}

/* Back-compat wrapper for existing semi-trans-only callers in vram.c. */
void gl_submit_tri2d_semitrans(
    const int a[2], const int b[2], const int c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    int abr)
{
    if (abr < 0 || abr > 3) abr = 0;
    unsigned short flags = 2 | ((abr & 3) << 2);   /* semi-trans, no texture */
    gl_submit_tri2d(a, b, c, NULL, NULL, NULL, col_a, col_b, col_c, 0, 0, flags);
}

void gl_submit_line(
    const int a[2], const int b[2],
    const unsigned char col_a[3], const unsigned char col_b[3],
    unsigned short flags)
{
    if (!g_enabled) return;
    line2d_reserve(2);
    pack_vert2d(&g_line2d_buf[g_line2d_count++], a, NULL, col_a, 0, 0, flags);
    pack_vert2d(&g_line2d_buf[g_line2d_count++], b, NULL, col_b, 0, 0, flags);
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

/* Walks a buffer of GL2DVerts submitted in PSX-OT order (back-to-front) and
 * issues draws in runs that share (semi_trans, abr). prim_type is GL_TRIANGLES
 * or GL_LINES; stride is the number of verts per primitive (3 or 2). */
static void flush_2d_buf(GLuint vao, GLuint vbo, GL2DVert *buf,
                         size_t count, GLenum prim_type, size_t stride)
{
    if (count == 0) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(count * sizeof(GL2DVert)),
                 buf, GL_STREAM_DRAW);

    glUseProgram(g_tri2d_prog);
    glUniform1i(glGetUniformLocation(g_tri2d_prog, "uNoTextures"),
                gl_debug_no_textures ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vram_tex);
    glDisable(GL_DEPTH_TEST);

    size_t i = 0;
    while (i < count) {
        unsigned short f0 = buf[i].flags;
        int semi0 = (f0 >> 1) & 1;
        int abr0  = (f0 >> 2) & 3;
        size_t run_start = i;
        size_t j = i;
        while (j + stride <= count) {
            unsigned short f = buf[j].flags;
            int s = (f >> 1) & 1;
            int a = (f >> 2) & 3;
            if (s != semi0 || (s && a != abr0)) break;
            j += stride;
        }
        if (semi0) {
            glEnable(GL_BLEND);
            apply_abr(abr0);
        } else {
            glDisable(GL_BLEND);
        }
        glDrawArrays(prim_type, (GLint)run_start, (GLsizei)(j - run_start));
        i = j;
    }

    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    /* Do NOT reset counts -- see gl_renderer_present comment. */
}

static void flush_tri2d(void)
{
    flush_2d_buf(g_tri2d_vao, g_tri2d_vbo, g_tri2d_buf, g_tri2d_count,
                 GL_TRIANGLES, 3);
}

static void flush_line2d(void)
{
    flush_2d_buf(g_line2d_vao, g_line2d_vbo, g_line2d_buf, g_line2d_count,
                 GL_LINES, 2);
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

    /* Render everything into the hi-res FBO at 320*scale x 224*scale. */
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, g_fbo_w, g_fbo_h);

    /* Clear with the PSX framebuffer background color (stored by
       port_ClearImage when GL is enabled) + depth buffer.

       Codec mode: PSX doesn't clear the framebuffer each frame, which is
       how FadeCodecScreen's semi-transparent TILE accumulates to near-black
       over several frames. If we clear every frame the fade never builds.
       Preserve color; always clear depth (safe since we skip 3D anyway). */
    glEnable(GL_DEPTH_TEST);
    /* LEQUAL + flat face_z gives painter's-algorithm semantics: later-submitted
       faces at the same centroid depth win, matching PSX OT-sort behavior. */
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    if (gl_debug_clear_override) {
        /* Debug override: bright color makes gaps / unpainted regions obvious. */
        glClearColor(gl_debug_clear_rgb[0], gl_debug_clear_rgb[1],
                     gl_debug_clear_rgb[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else if (g_codec_mode) {
        /* Codec: see comment in the earlier fix -- must be black. */
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        glClearColor(g_clear_rgb[0], g_clear_rgb[1], g_clear_rgb[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    /* Debug VRAM view: skip 3D, just dump VRAM as before. */
    extern int port_vram_debug_view(void);
    int debug_view = port_vram_debug_view();

    /* Apply wireframe polygon mode if the debug flag is set. Cleared later. */
    if (gl_debug_wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    /* --- 3D pass -------------------------------------------------------- */
    /* In codec mode (DG_FrameRate == 2) the PSX pipeline disables 3D and the
       screen is meant to be pure 2D. Skip the 3D batch entirely so stale
       triangles from the pre-codec stage don't bleed through the codec UI. */
    if (!debug_view && !g_codec_mode && !gl_debug_skip_3d && g_tri3d_count > 0) {
        glBindVertexArray(g_tri3d_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_tri3d_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(g_tri3d_count * sizeof(GL3DVert)),
                     g_tri3d_buf, GL_STREAM_DRAW);

        glUseProgram(g_tri3d_prog);
        glUniform2f(g_tri3d_u_half_screen, 160.0f, 112.0f);
        glUniform2f(g_tri3d_u_near_far, 4.0f, 32768.0f);
        glUniform1i(glGetUniformLocation(g_tri3d_prog, "uNoTextures"),
                    gl_debug_no_textures ? 1 : 0);
        glUniform1i(glGetUniformLocation(g_tri3d_prog, "uFaceId"),
                    gl_debug_face_id ? 1 : 0);
        glUniform1i(glGetUniformLocation(g_tri3d_prog, "uShowNormals"),
                    gl_debug_show_normals ? 1 : 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_vram_tex);

        /* GPU backface cull. Our vertex shader flips Y (PSX screen is Y-down,
           GL NDC is Y-up), which inverts 2D winding, so PSX-front (CW in
           Y-down / NCLIP area > 0) becomes GL-CCW after projection. Default
           front-face GL_CCW + cull GL_BACK reproduces PSX NCLIP exactly and
           handles mirror matrices for free (GPU sees the real post-projection
           winding). Per-tri "no cull" flag (bit 6) disables for characters /
           DG_MODEL_BOTHFACE. */
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(gl_debug_cull_cw ? GL_CW : GL_CCW);

        /* Walk the 3D buffer in runs sharing the same (semi_trans, abr,
           no_cull) bucket. Submission order is PSX-OT order (back-to-front)
           so a simple bucket-change detector preserves painter's semantics.
           Opaque runs: blend OFF, depth write ON. Semi-trans runs: blend
           via apply_abr, depth write OFF (depth test still on). */
        size_t i = 0;
        while (i < g_tri3d_count) {
            unsigned short f0 = g_tri3d_buf[i].flags;
            int semi0   = (f0 >> 1) & 1;
            int abr0    = (f0 >> 2) & 3;
            int nocull0 = (f0 >> 6) & 1;
            size_t run_start = i;
            /* 3 verts per triangle — stride through whole triangles. */
            size_t j = i;
            while (j + 3 <= g_tri3d_count) {
                unsigned short f = g_tri3d_buf[j].flags;
                int s = (f >> 1) & 1;
                int a = (f >> 2) & 3;
                int nc = (f >> 6) & 1;
                if (s != semi0 || (s && a != abr0) || nc != nocull0) break;
                j += 3;
            }
            if (semi0) {
                glEnable(GL_BLEND);
                glDepthMask(GL_FALSE);
                apply_abr(abr0);
            } else {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
            if (nocull0) glDisable(GL_CULL_FACE);
            else         glEnable(GL_CULL_FACE);
            glDrawArrays(GL_TRIANGLES, (GLint)run_start, (GLsizei)(j - run_start));
            i = j;
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glBlendEquation(GL_FUNC_ADD);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    /* --- 2D pass (triangles + lines) ----------------------------------- */
    /* All 2D primitives now go through GL directly. VRAM is still uploaded
       so the shader can sample PSX textures/CLUTs from it, but we no longer
       blit the whole framebuffer region as a compositor. */
    if (!debug_view) {
        if (!gl_debug_skip_2d)    flush_tri2d();
        if (!gl_debug_skip_lines) flush_line2d();
    }

    /* Restore fill polygon mode so the overlay/blit pass isn't wireframed. */
    if (gl_debug_wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    /* --- VRAM debug overlay (only when explicitly enabled) ------------- */
    if (debug_view) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(g_blit_prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_vram_tex);
        glUniform4f(g_blit_u_region, 0.0f, 0.0f, 1024.0f, 512.0f);
        glUniform1i(glGetUniformLocation(g_blit_prog, "uDiscardZero"), 0);
        glBindVertexArray(g_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    /* Note: we do NOT reset g_tri3d_count / g_tri2d_count here. The game logic
       runs at 30 Hz but rendering runs at 60 Hz, so "idle" render frames must
       redraw the same accumulated content. The buffers are reset when the
       game logic starts a new tick (port_RenderObjects -> gl_renderer_begin_3d,
       port_DrawOTag -> gl_renderer_begin_2d). */

    /* --- Upscale FBO -> window ----------------------------------------- */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0, 0, 0, 1);          /* letterbox bars */
    glClear(GL_COLOR_BUFFER_BIT);
    glBlitFramebuffer(0, 0, g_fbo_w, g_fbo_h,
                      vp_x, vp_y, vp_x + vp_w, vp_y + vp_h,
                      GL_COLOR_BUFFER_BIT,
                      gl_debug_blit_nearest ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* SwapWindow happens in port_render after ImGui draws. */
}
