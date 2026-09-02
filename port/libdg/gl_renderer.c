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

/* macOS ships GL 4.1 Core via the OpenGL framework. On Linux, glcorearb.h
   provides all GL Core Profile declarations (functions + constants) without
   needing a loader — Mesa's libGL exports them all. Plain <GL/gl.h> only
   covers GL 1.x, so modern calls like glCreateShader silently fail via
   implicit function declarations. */
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
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
    unsigned short flags; /* b0 textured, b1 semi-trans, b2-3 ABR, b4 per-pixel lit,
                             b5 sample-previous-framebuffer (Stealth/Optical Camo:
                             set by gl_submit_tri3d when tpage points into the PSX
                             displayed-framebuffer region; the FS branches to
                             sample uPrevFB), b6 no-cull */
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
static GLint  g_tri3d_u_ortho       = -1;
static GLint  g_tri3d_u_ortho_lrbt  = -1;

/* Editor wireframe overlay (world-space lines). Reuses g_tri3d_prog. */
static GL3DVert *g_line3d_buf   = NULL;
static size_t    g_line3d_count = 0;
static size_t    g_line3d_cap   = 0;
static GLuint    g_line3d_vao   = 0;
static GLuint    g_line3d_vbo   = 0;

/* Stored by port_ClearImage; applied as glClearColor in gl_renderer_present. */
static float g_clear_rgb[3] = {0.0f, 0.0f, 0.0f};
static int   g_codec_mode   = 0;

/* Hi-res internal FBO. All GL rendering targets this; we blit it upscaled to
 * the window at the end of present via glBlitFramebuffer (linear filter).
 *
 * Phase 2 of the editor's Hammer-style overhaul introduces multiple
 * viewports — slot 0 is the legacy 3D textured view (used by the live
 * game and the editor's 3D pane); slots 1..3 are the editor's Top /
 * Front / Side ortho wireframe panes. Each has its own FBO + color +
 * depth at potentially different sizes. The "active" slot is the one
 * subsequent render calls target — switched via
 * gl_renderer_set_active_viewport(). The g_fbo/g_fbo_color/etc. globals
 * mirror the active slot so existing code that references them keeps
 * working unchanged. */
#define GL_MAX_VIEWPORTS 4
typedef struct { GLuint fbo, color, depth; int w, h; } GLFbo;
static GLFbo g_viewports[GL_MAX_VIEWPORTS] = {0};
static int   g_active_vp = 0;

static int    g_scale     = 4;
static int    g_widescreen = 0;            /* 0 = 4:3 (320w), 1 = 16:9 Hor+ (400w) */
static int    g_render_w   = 320;          /* internal render width, follows g_widescreen */
static int    g_fbo_w     = 320 * 4;
static int    g_fbo_h     = 224 * 4;
static GLuint g_fbo       = 0;
static GLuint g_fbo_color = 0;
static GLuint g_fbo_depth = 0;

/* Captured previous-frame color at FBO resolution. Updated at the end of
   every gl_renderer_present() by copying g_fbo_color. PSX framebuffer-readback
   effects (NewBlur, NewBlurPure — the blur / "ghost" actors) sample VRAM at
   the displayed framebuffer region; the GL backend routes those sampling
   ops to this texture in the 2D fragment shader.  Created/destroyed alongside
   g_fbo_color; resized in lockstep with it. */
static GLuint g_prev_fb_tex = 0;

/* Per-viewport projection/render mode. uOrtho=1 switches the vertex
 * shader to orthographic projection; uWireframe=1 makes the rasterizer
 * draw outlines only and the fragment shader emit a flat colour. */
static int    g_vp_ortho[GL_MAX_VIEWPORTS]      = {0};
static float  g_vp_ortho_lrbt[GL_MAX_VIEWPORTS][4] = {{0}};
static int    g_vp_wireframe[GL_MAX_VIEWPORTS]  = {0};

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

/* NewBlur / NewBlurPure intensity controls. The 2D fb-readback shader
 * scales the prev-frame tap by vCol*port_blur_strength and emits a
 * soft-gated src.alpha (brightness-driven) into a dedicated fb-readback
 * batch that uses glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
 * Dim/uninit prev-fb samples contribute 0 -> no darkening on cutscene
 * fade-ins from black (the d00a opening case); bright samples blend
 * at 50% -> the PSX ghost-trail. */
int   port_blur_enabled       = 1;
float port_blur_strength      = 1.4f;
float gl_debug_clear_rgb[3]   = {1.0f, 0.0f, 1.0f};  /* magenta default */

/* 2D primitive pipeline. Handles all screen-space primitives (TILE, POLY_F*,
 * POLY_G*, POLY_FT*, POLY_GT*, SPRT*). Flags encode textured / semi-trans /
 * ABR mode so one batch can cover every primitive shape, switching state in
 * runs at flush time. Lines use a parallel VBO with the same vertex format. */
typedef struct {
    float pos[2];             /* 0..320 x 0..224 (framebuffer pixel coords) */
    float uv[2];              /* 0..255 typical; ignored when !textured */
    unsigned char rgba[4];
    unsigned short tpage;     /* PSX tpage */
    unsigned short clut;      /* PSX CLUT */
    unsigned short flags;     /* bit 0 textured, bit 1 semi-trans, bits 2..3 ABR,
                                 bit 5 sample-previous-framebuffer (route to
                                 g_prev_fb_tex instead of CLUT path; set by
                                 gl_submit_tri2d when tpage points into the
                                 PSX displayed-framebuffer region). */
    unsigned short depth_flag;/* 0 = foreground (z=0.0, on top of 3D),
                                 non-zero = background (z=0.999, behind 3D
                                 via depth test; used for skybox tiles). */
    short          clip[4];   /* (x0, y0, x1, y1) drawing-area clip in PSX
                                 buffer-local coords. The FS discards fragments
                                 outside this rect, reproducing the PSX GPU's
                                 E3/E4 drawing-area clipping. Used by the radar
                                 (DRAWENV 235..303 x 16..67) so distant-enemy
                                 dots + vision cones don't leak past the
                                 radar circle. */
} GL2DVert;

/* Set by port_DrawOTag when walker enters a buffer slot that belongs to
 * channel 0's own OT (not chanl 1/2 that got linked into it). Chanl 0 is
 * where world/background prims like the sphere skybox live. pack_vert2d
 * copies this flag into the vertex so the 2D shader can push them to
 * z=0.999 and the depth test against 3D's z-buffer makes them hide behind
 * stage geometry. */
unsigned short port_2d_depth_flag = 0;

/* Foreground 2D (the normal buffer: HUD, menu, subtitles). Rendered after
 * 3D so it always sits on top. */
static GL2DVert *g_tri2d_buf    = NULL;
static size_t    g_tri2d_count  = 0;
static size_t    g_tri2d_cap    = 0;
static GL2DVert *g_line2d_buf   = NULL;
static size_t    g_line2d_count = 0;
static size_t    g_line2d_cap   = 0;

/* Background 2D (world-chanl OT prims -- notably the sphere skybox). These
 * are drawn BEFORE 3D geometry so the 3D pass can paint over them with its
 * depth-tested fragments, yielding proper sky-behind-scene ordering. */
static GL2DVert *g_tri2d_bg_buf   = NULL;
static size_t    g_tri2d_bg_count = 0;
static size_t    g_tri2d_bg_cap   = 0;

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
    "uniform int  uOrtho;                // 0 = perspective, 1 = orthographic\n"
    "uniform vec4 uOrthoLRBT;            // (left, right, bottom, top) in eye-space units\n"
    "void main() {\n"
    "  if (uOrtho != 0) {\n"
    "    /* Orthographic: aPos is already eye-space; map LRBT to NDC linearly.\n"
    "       Y is flipped (PSX +Y down convention shared with the persp path). */\n"
    "    float l = uOrthoLRBT.x, r = uOrthoLRBT.y;\n"
    "    float b = uOrthoLRBT.z, t = uOrthoLRBT.w;\n"
    "    float nx = (aPos.x - l) / (r - l) * 2.0 - 1.0;\n"
    "    float ny = (aPos.y - b) / (t - b) * 2.0 - 1.0;\n"
    "    /* Squash Z into [-1,1] using uNearFar. eye Z >= 0 means in front. */\n"
    "    float n  = uNearFar.x, f = uNearFar.y;\n"
    "    float nz = (aPos.z - n) / (f - n) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(nx, -ny, nz, 1.0);\n"
    "  } else {\n"
    "    /* Clip-space w = true eye-space z (NOT clamped). This lets the GPU's\n"
    "       near-plane clipping discard/clip geometry at or behind the camera.\n"
    "       The old 'if (cz < 4) cz = 4' floored w>=4, so behind-camera verts\n"
    "       (eye z < 0) were pulled in front and splattered across the view in\n"
    "       first-person. Depth still uses the face centroid (z_ndc) so painter\n"
    "       ordering is unchanged; only verts with eye z < 4 (very close or\n"
    "       behind) change behaviour — far geometry (z >= 4) is identical. */\n"
    "    float fz = aFaceZ;\n"
    "    if (fz < 4.0) fz = 4.0;\n"
    "    float n = uNearFar.x, f = uNearFar.y;\n"
    "    float A = (f + n) / (f - n);\n"
    "    float B = -2.0 * f * n / (f - n);\n"
    "    float z_ndc = (A * fz + B) / fz;\n"
    "    float w = aPos.z;\n"
    "    gl_Position = vec4(\n"
    "        aPos.x * aDist / uHalfScreen.x,\n"
    "       -aPos.y * aDist / uHalfScreen.y,\n"
    "        z_ndc * w,\n"
    "        w);\n"
    "  }\n"
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
    "uniform sampler2D  uPrevFB;     // captured previous frame (Stealth)\n"
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
    "    bool textured    = (vFlags & 1u)  != 0u && (uNoTextures == 0);\n"
    "    bool fb_readback = (vFlags & 32u) != 0u && (uNoTextures == 0);\n"
    "    bool per_pixel   = (vFlags & 16u) != 0u;\n"
    "    if (fb_readback) {\n"
    "        // Stealth / Optical Camo. The 3D run-batcher snapshots the FBO\n"
    "        // into uPrevFB right before this run draws (after level geo,\n"
    "        // before Snake) so we sample the scene-without-Snake -- no\n"
    "        // accumulation. PSX kogaku2 uses a (3/4)*x + 160 horizontal\n"
    "        // remap that compresses the sampled UV toward screen centre,\n"
    "        // producing the iconic 'lensed' refraction. Replicated here\n"
    "        // by squeezing uv.x by 25% around the centre.\n"
    "        vec2 ts = vec2(textureSize(uPrevFB, 0));\n"
    "        vec2 uv = gl_FragCoord.xy / ts;\n"
    "        uv.x = 0.5 + (uv.x - 0.5) * 0.75;\n"
    "        vec3 prev = texture(uPrevFB, uv).rgb;\n"
    "        vec3 tint = vCol.rgb * 2.0;   // 128 = neutral, SNAKE_COLOR = 1.0,1.25,0.75\n"
    "        oColor = vec4(clamp(prev * tint, 0.0, 1.0), 1.0);\n"
    "        return;\n"
    "    } else if (textured) {\n"
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
    "    bool sampled = textured || fb_readback;\n"
    "    vec3 rgb = sampled ? tex * shade : (per_pixel ? shade : vCol.rgb);\n"
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
    "layout(location=3) in uvec4 aTex;   // tpage, clut, flags, depth_flag\n"
    "layout(location=4) in ivec4 aClip;  // PSX drawing-area clip (x0,y0,x1,y1)\n"
    "uniform float uXScale;              // 1.0 in 4:3, 320/render_w in widescreen\n"
    "uniform vec2 uNearFar;              // (near, far) -- matches the 3D pass\n"
    "out vec2 vUV;\n"
    "out vec4 vCol;\n"
    "flat out uint vTPage;\n"
    "flat out uint vCLUT;\n"
    "flat out uint vFlags;\n"
    "flat out ivec4 vClip;\n"
    "void main() {\n"
    "    /* NDC depth from the PSX OT slot encoded in aTex.w (depth_flag):\n"
    "         0 / 1 -> front (z=-1), always on top (HUD/menu/skybox-bg pass);\n"
    "         >=2   -> world VFX at OT slot (aTex.w-2); eye_z = slot<<8, mapped\n"
    "                  through the SAME near/far curve as the 3D pass so the\n"
    "                  fragment depth-tests against 3D geometry and gets\n"
    "                  occluded by closer surfaces. */\n"
    "    float z2d;\n"
    "    if (aTex.w <= 1u) {\n"
    "        z2d = -1.0;\n"
    "    } else {\n"
    "        float n = uNearFar.x, f = uNearFar.y;\n"
    "        float Z = float((aTex.w - 2u) << 8u);\n"
    "        if (Z < n) Z = n;\n"
    "        float A = (f + n) / (f - n);\n"
    "        float B = -2.0 * f * n / (f - n);\n"
    "        z2d = (A * Z + B) / Z;\n"
    "    }\n"
    "    gl_Position = vec4((aPos.x / 160.0 - 1.0) * uXScale,\n"
    "                       1.0 - aPos.y / 112.0,\n"
    "                       z2d, 1.0);\n"
    "    vUV = aUV;\n"
    "    vCol = aCol;\n"
    "    vTPage = aTex.x;\n"
    "    vCLUT  = aTex.y;\n"
    "    vFlags = aTex.z;\n"
    "    vClip  = aClip;\n"
    "}\n";

static const char *TRI2D_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec4 vCol;\n"
    "flat in uint vTPage;\n"
    "flat in uint vCLUT;\n"
    "flat in uint vFlags;\n"
    "flat in ivec4 vClip;\n"
    "uniform usampler2D uVRAM;\n"
    "uniform sampler2D  uPrevFB;     // RGBA8 capture of the previous frame's FBO\n"
    "uniform int uNoTextures;\n"
    "uniform float uXScale;\n"
    "uniform float uBlurStrength;    // 0 = disabled, default 1.4 (PSX-exact = 2.0)\n"
    "out vec4 oColor;\n"
    "uint fetchVRAM(int x, int y) { return texelFetch(uVRAM, ivec2(x, y), 0).r; }\n"
    "vec3 decodePSX(uint p) {\n"
    "    return vec3(\n"
    "        float(p & 0x1Fu) / 31.0,\n"
    "        float((p >> 5) & 0x1Fu) / 31.0,\n"
    "        float((p >> 10) & 0x1Fu) / 31.0);\n"
    "}\n"
    "void main() {\n"
    "    bool textured     = (vFlags & 1u)  != 0u && (uNoTextures == 0);\n"
    "    bool fb_readback  = (vFlags & 32u) != 0u && (uNoTextures == 0)\n"
    "                        && uBlurStrength > 0.0;\n"
    "    // PSX drawing-area clip (E3/E4). Recover PSX coords from gl_FragCoord\n"
    "    // using the inverse of the vert-shader projection; radar enemy dots\n"
    "    // / cones outside the 69x52 clip rect are dropped here.\n"
    "    // Skipped for fb-readback (blur) prims so widescreen edge fragments\n"
    "    // aren't discarded when the blur quad is stretched past PSX 0..319.\n"
    "    vec2 fboSize = vec2(textureSize(uPrevFB, 0));\n"
    "    if (!fb_readback) {\n"
    "        float psx_x = 160.0 * ((2.0 * gl_FragCoord.x / fboSize.x - 1.0) / uXScale + 1.0);\n"
    "        float psx_y = 224.0 * (1.0 - gl_FragCoord.y / fboSize.y);\n"
    "        if (psx_x < float(vClip.x) || psx_x > float(vClip.z) + 1.0 ||\n"
    "            psx_y < float(vClip.y) || psx_y > float(vClip.w) + 1.0) {\n"
    "            discard;\n"
    "        }\n"
    "    }\n"
    "    vec3 out_rgb;\n"
    "    if (fb_readback) {\n"
    "        // PSX framebuffer-readback effect (NewBlur, NewBlurPure). The\n"
    "        // tpage's base coords point into the displayed framebuffer; sample\n"
    "        // the previous-frame capture instead of doing a CLUT lookup.\n"
    "        int bx = int(vTPage & 0xFu) * 64;\n"
    "        int by = int((vTPage >> 4u) & 1u) * 256;\n"
    "        if ((vTPage & 0x800u) != 0u) by += 512;\n"
    "        int u = int(clamp(vUV.x, 0.0, 255.0));\n"
    "        int v = int(clamp(vUV.y, 0.0, 255.0));\n"
    "        // The PSX double-buffer pair lives at x=[0..319] and [320..639];\n"
    "        // both hold the same display surface, just at different VRAM\n"
    "        // offsets. mod 320 collapses to the buffer-local x. Y is shared.\n"
    "        // Map PSX x via the vert shader's uXScale so widescreen sampling\n"
    "        // lands on the central 320-wide region, not the pillarbox.\n"
    "        float psx_fx = float((bx + u) % 320);\n"
    "        float fx = ((psx_fx / 160.0 - 1.0) * uXScale + 1.0) * 0.5;\n"
    "        float fy = float(by + v)         / 224.0;\n"
    "        // FBO row 0 is GL-bottom (the 2D VS flips Y at projection time);\n"
    "        // glCopyTexSubImage2D preserved that orientation. Flip back so\n"
    "        // PSX-pixel-row 0 reads the GL-top texel.\n"
    "        fy = 1.0 - fy;\n"
    "        vec3 tex = texture(uPrevFB, vec2(fx, fy)).rgb;\n"
    "        // uBlurStrength is bound from port_blur_strength (imgui slider).\n"
    "        out_rgb = clamp(tex * (vCol.rgb * uBlurStrength), 0.0, 1.0);\n"
    "        // Soft alpha-gate vs prev-fb brightness -- see tri2d.frag.\n"
    "        // Drives src.alpha for the fb-readback batch's BlendFunc\n"
    "        // (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) so a dim sample\n"
    "        // contributes 0 (no darkening) and a normal sample blends 50%.\n"
    "        float bright = max(max(tex.r, tex.g), tex.b);\n"
    "        oColor = vec4(out_rgb, smoothstep(0.0, 0.06, bright) * 0.5);\n"
    "        return;\n"
    "    } else if (textured) {\n"
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

static GLuint compile_shader(GLenum type, const char *src, const char *src_label)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "[gl] shader compile failed (%s):\n%s\n",
                src_label ? src_label : "<embedded>", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src,
                           const char *vs_label, const char *fs_label)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src, vs_label);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, fs_label);
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
        fprintf(stderr, "[gl] program link failed (%s + %s):\n%s\n",
                vs_label ? vs_label : "<embedded>",
                fs_label ? fs_label : "<embedded>", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* Read an entire file into a malloc'd null-terminated buffer. Returns NULL
   on any failure. Caller frees. Searches a small set of candidate paths so
   the binary works whether you run it from port/ or the repo root. */
static char *load_text_file(const char *rel_path)
{
    const char *candidates[] = {
        rel_path,                       /* cwd is port/  (normal: ./mgs ISO/...) */
        NULL, NULL,                     /* filled below if we can synthesise more */
    };
    char p1[1024], p2[1024];
    snprintf(p1, sizeof p1, "port/%s", rel_path);  /* cwd is repo root */
    snprintf(p2, sizeof p2, "../port/%s", rel_path); /* cwd is repo/build etc. */
    candidates[1] = p1;
    candidates[2] = p2;

    FILE *fp = NULL;
    const char *used = NULL;
    for (size_t i = 0; i < sizeof(candidates)/sizeof(*candidates); i++) {
        if (!candidates[i]) continue;
        fp = fopen(candidates[i], "rb");
        if (fp) { used = candidates[i]; break; }
    }
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0 || n > 64 * 1024) { fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    buf[got] = '\0';
    fprintf(stderr, "[gl] loaded shader %s (%ld bytes)\n", used, n);
    return buf;
}

/* Compile + link a shader pair. Prefers on-disk files (so the GLSL has
   syntax highlighting, real line numbers, and can be hot-reloaded via the
   imgui Reload button) and falls back to the embedded fallbacks below if
   the files aren't present. Returns 0 on failure (caller should keep the
   previous program around). */
static GLuint load_program(const char *vs_path, const char *fs_path,
                           const char *vs_fallback, const char *fs_fallback)
{
    char *vs_disk = load_text_file(vs_path);
    char *fs_disk = load_text_file(fs_path);
    const char *vs_src = vs_disk ? vs_disk : vs_fallback;
    const char *fs_src = fs_disk ? fs_disk : fs_fallback;
    const char *vs_label = vs_disk ? vs_path : NULL;
    const char *fs_label = fs_disk ? fs_path : NULL;

    GLuint p = link_program(vs_src, fs_src, vs_label, fs_label);
    free(vs_disk);
    free(fs_disk);
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

    g_blit_prog = load_program("libdg/shaders/blit.vert",
                               "libdg/shaders/blit.frag",
                               BLIT_VS, BLIT_FS);
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
    g_tri3d_prog = load_program("libdg/shaders/tri3d.vert",
                                "libdg/shaders/tri3d.frag",
                                TRI3D_VS, TRI3D_FS);
    if (!g_tri3d_prog) {
        SDL_GL_DeleteContext(g_ctx);
        g_ctx = NULL;
        return -1;
    }
    g_tri3d_u_half_screen = glGetUniformLocation(g_tri3d_prog, "uHalfScreen");
    g_tri3d_u_near_far    = glGetUniformLocation(g_tri3d_prog, "uNearFar");
    g_tri3d_u_ortho       = glGetUniformLocation(g_tri3d_prog, "uOrtho");
    g_tri3d_u_ortho_lrbt  = glGetUniformLocation(g_tri3d_prog, "uOrthoLRBT");
    glUseProgram(g_tri3d_prog);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uVRAM"),   0);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uPrevFB"), 2);
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

    /* line3d: separate VAO/VBO, same vertex format, drawn with GL_LINES. */
    glGenVertexArrays(1, &g_line3d_vao);
    glGenBuffers(1, &g_line3d_vbo);
    glBindVertexArray(g_line3d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line3d_vbo);
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
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, s, (void *)offsetof(GL3DVert, normal));
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
    g_tri2d_prog = load_program("libdg/shaders/tri2d.vert",
                                "libdg/shaders/tri2d.frag",
                                TRI2D_VS, TRI2D_FS);
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
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_SHORT, s2, (void *)offsetof(GL2DVert, clip));

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
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_SHORT, s2, (void *)offsetof(GL2DVert, clip));

    /* Sampler unit for 2D textured prims. */
    glUseProgram(g_tri2d_prog);
    glUniform1i(glGetUniformLocation(g_tri2d_prog, "uVRAM"),   0);
    glUniform1i(glGetUniformLocation(g_tri2d_prog, "uPrevFB"), 2);
    glUniform1f(glGetUniformLocation(g_tri2d_prog, "uXScale"), 1.0f);
    glUniform2f(glGetUniformLocation(g_tri2d_prog, "uNearFar"), 4.0f, 32768.0f);
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
        g_fbo_w = g_render_w * n;
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
        /* Previous-frame capture target — same dims as g_fbo_color. */
        glGenTextures(1, &g_prev_fb_tex);
        glBindTexture(GL_TEXTURE_2D, g_prev_fb_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        printf("[gl] FBO %dx%d (PORT_GL_SCALE=%d)\n", g_fbo_w, g_fbo_h, n);

        /* Slot 0 = the legacy 3D textured viewport. Slots 1..3 (Top /
         * Front / Side ortho wireframe panes in the editor) are created
         * lazily on first resize to keep startup cheap when running the
         * live game (which only needs slot 0). */
        g_viewports[0].fbo   = g_fbo;
        g_viewports[0].color = g_fbo_color;
        g_viewports[0].depth = g_fbo_depth;
        g_viewports[0].w     = g_fbo_w;
        g_viewports[0].h     = g_fbo_h;
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
    if (g_line3d_vbo) glDeleteBuffers(1, &g_line3d_vbo);
    if (g_line3d_vao) glDeleteVertexArrays(1, &g_line3d_vao);
    free(g_tri3d_buf);
    free(g_line3d_buf);
    g_tri3d_buf = NULL;
    g_line3d_buf = NULL;
    g_tri3d_cap = g_tri3d_count = 0;
    g_line3d_cap = g_line3d_count = 0;
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
    if (g_fbo_depth)   glDeleteRenderbuffers(1, &g_fbo_depth);
    if (g_fbo_color)   glDeleteTextures(1, &g_fbo_color);
    if (g_prev_fb_tex) glDeleteTextures(1, &g_prev_fb_tex);
    if (g_fbo)         glDeleteFramebuffers(1, &g_fbo);
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

int gl_renderer_read_psx_region(int psx_x, int psx_y, int psx_w, int psx_h,
                                unsigned char *out_rgb,
                                int *out_w, int *out_h)
{
    if (!g_enabled || !g_fbo || !out_rgb) return 0;
    if (psx_w <= 0 || psx_h <= 0) return 0;

    int x = psx_x * g_scale;
    int w = psx_w * g_scale;
    int h = psx_h * g_scale;
    /* PSX origin is top-left, GL origin is bottom-left — flip y. */
    int y_top = psx_y * g_scale;
    int y_gl  = g_fbo_h - y_top - h;
    if (y_gl < 0) y_gl = 0;

    GLint prev_fbo = 0, prev_pack = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y_gl, w, h, GL_RGB, GL_UNSIGNED_BYTE, out_rgb);
    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_fbo);

    /* Flip vertically into place — glReadPixels returns bottom-up rows. */
    int stride = w * 3;
    unsigned char tmp[stride];
    for (int yy = 0; yy < h / 2; yy++) {
        unsigned char *row_a = out_rgb + yy * stride;
        unsigned char *row_b = out_rgb + (h - 1 - yy) * stride;
        memcpy(tmp,   row_a, stride);
        memcpy(row_a, row_b, stride);
        memcpy(row_b, tmp,   stride);
    }

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return 1;
}

int gl_renderer_reload_shaders(void)
{
    if (!g_enabled) return 0;

    /* Try each program independently. If any fails, keep the previous one. */
    GLuint new_blit = load_program("libdg/shaders/blit.vert",
                                   "libdg/shaders/blit.frag",
                                   BLIT_VS, BLIT_FS);
    GLuint new_tri3d = load_program("libdg/shaders/tri3d.vert",
                                    "libdg/shaders/tri3d.frag",
                                    TRI3D_VS, TRI3D_FS);
    GLuint new_tri2d = load_program("libdg/shaders/tri2d.vert",
                                    "libdg/shaders/tri2d.frag",
                                    TRI2D_VS, TRI2D_FS);

    int ok = (new_blit && new_tri3d && new_tri2d);
    if (!ok) {
        if (new_blit)  glDeleteProgram(new_blit);
        if (new_tri3d) glDeleteProgram(new_tri3d);
        if (new_tri2d) glDeleteProgram(new_tri2d);
        fprintf(stderr, "[gl] shader reload FAILED -- keeping previous programs\n");
        return 0;
    }

    /* Swap in the new programs and re-fetch every cached uniform location
       (locations are per-program; the new program assigns its own). Sampler
       unit bindings are also program state -- re-apply them. */
    if (g_blit_prog)  glDeleteProgram(g_blit_prog);
    if (g_tri3d_prog) glDeleteProgram(g_tri3d_prog);
    if (g_tri2d_prog) glDeleteProgram(g_tri2d_prog);
    g_blit_prog  = new_blit;
    g_tri3d_prog = new_tri3d;
    g_tri2d_prog = new_tri2d;

    g_blit_u_region = glGetUniformLocation(g_blit_prog, "uRegion");
    glUseProgram(g_blit_prog);
    glUniform1i(glGetUniformLocation(g_blit_prog, "uVRAM"), 0);
    glUniform1i(glGetUniformLocation(g_blit_prog, "uZBuf"), 1);

    g_tri3d_u_half_screen = glGetUniformLocation(g_tri3d_prog, "uHalfScreen");
    g_tri3d_u_near_far    = glGetUniformLocation(g_tri3d_prog, "uNearFar");
    g_tri3d_u_ortho       = glGetUniformLocation(g_tri3d_prog, "uOrtho");
    g_tri3d_u_ortho_lrbt  = glGetUniformLocation(g_tri3d_prog, "uOrthoLRBT");
    glUseProgram(g_tri3d_prog);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uVRAM"),   0);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uPrevFB"), 2);

    glUseProgram(g_tri2d_prog);
    glUniform1i(glGetUniformLocation(g_tri2d_prog, "uVRAM"),   0);
    glUniform1i(glGetUniformLocation(g_tri2d_prog, "uPrevFB"), 2);
    glUniform1f(glGetUniformLocation(g_tri2d_prog, "uXScale"), 1.0f);
    glUniform2f(glGetUniformLocation(g_tri2d_prog, "uNearFar"), 4.0f, 32768.0f);
    glUseProgram(0);

    fprintf(stderr, "[gl] shader reload OK\n");
    return 1;
}

void gl_renderer_set_scale(int n)
{
    if (!g_enabled) return;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    if (n == g_scale) return;

    /* Rebuild the color texture + depth renderbuffer at the new size. */
    g_scale = n;
    g_fbo_w = g_render_w * n;
    g_fbo_h = 224 * n;

    glBindTexture(GL_TEXTURE_2D, g_fbo_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, g_fbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          g_fbo_w, g_fbo_h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (g_prev_fb_tex) {
        glBindTexture(GL_TEXTURE_2D, g_prev_fb_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    printf("[gl] FBO resized to %dx%d (scale=%d)\n", g_fbo_w, g_fbo_h, g_scale);
}

/* Lazily allocate FBO + color + depth for viewport idx if needed. */
static void ensure_viewport(int idx, int w, int h)
{
    if (idx < 0 || idx >= GL_MAX_VIEWPORTS) return;
    GLFbo *vp = &g_viewports[idx];
    if (vp->fbo == 0) {
        glGenFramebuffers(1, &vp->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, vp->fbo);
        glGenTextures(1, &vp->color);
        glBindTexture(GL_TEXTURE_2D, vp->color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, vp->color, 0);
        glGenRenderbuffers(1, &vp->depth);
        glBindRenderbuffer(GL_RENDERBUFFER, vp->depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, vp->depth);
        vp->w = w; vp->h = h;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    /* Already allocated — just resize if dims differ. */
    if (w != vp->w || h != vp->h) {
        glBindTexture(GL_TEXTURE_2D, vp->color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindRenderbuffer(GL_RENDERBUFFER, vp->depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        vp->w = w; vp->h = h;
    }
}

/* Switch which viewport subsequent rendering targets. The legacy globals
 * g_fbo / g_fbo_color / g_fbo_w / g_fbo_h are mirrored from the slot so
 * existing code paths continue to work. */
void gl_renderer_set_active_viewport(int idx)
{
    if (!g_enabled) return;
    if (idx < 0 || idx >= GL_MAX_VIEWPORTS) return;
    g_active_vp = idx;
    GLFbo *vp = &g_viewports[idx];
    g_fbo       = vp->fbo;
    g_fbo_color = vp->color;
    g_fbo_depth = vp->depth;
    g_fbo_w     = vp->w;
    g_fbo_h     = vp->h;
}

int gl_renderer_get_active_viewport(void) { return g_active_vp; }

void gl_renderer_resize_viewport(int idx, int w, int h)
{
    if (!g_enabled) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    ensure_viewport(idx, w, h);
    if (idx == g_active_vp) {
        g_fbo       = g_viewports[idx].fbo;
        g_fbo_color = g_viewports[idx].color;
        g_fbo_depth = g_viewports[idx].depth;
        g_fbo_w     = g_viewports[idx].w;
        g_fbo_h     = g_viewports[idx].h;
    }
}

unsigned int gl_renderer_get_viewport_color(int idx)
{
    if (!g_enabled || idx < 0 || idx >= GL_MAX_VIEWPORTS) return 0;
    return g_viewports[idx].color;
}

void gl_renderer_get_viewport_size(int idx, int *out_w, int *out_h)
{
    if (idx < 0 || idx >= GL_MAX_VIEWPORTS) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    if (out_w) *out_w = g_viewports[idx].w;
    if (out_h) *out_h = g_viewports[idx].h;
}

void gl_renderer_set_viewport_ortho(int idx, int on,
                                    float l, float r, float b, float t)
{
    if (idx < 0 || idx >= GL_MAX_VIEWPORTS) return;
    g_vp_ortho[idx] = on ? 1 : 0;
    g_vp_ortho_lrbt[idx][0] = l;
    g_vp_ortho_lrbt[idx][1] = r;
    g_vp_ortho_lrbt[idx][2] = b;
    g_vp_ortho_lrbt[idx][3] = t;
}

void gl_renderer_set_viewport_wireframe(int idx, int on)
{
    if (idx < 0 || idx >= GL_MAX_VIEWPORTS) return;
    g_vp_wireframe[idx] = on ? 1 : 0;
}

/* Legacy single-FBO API — operates on viewport 0. */
void gl_renderer_resize_fbo(int w, int h)
{
    gl_renderer_resize_viewport(0, w, h);
}

unsigned int gl_renderer_get_fbo_color(void)
{
    return g_enabled ? g_viewports[0].color : 0;
}

static int g_present_to_window = 1;
void gl_renderer_set_present_to_window(int on) { g_present_to_window = on ? 1 : 0; }

int gl_renderer_get_widescreen(void) { return g_widescreen; }

void gl_renderer_set_widescreen(int on)
{
    if (!g_enabled) return;
    int want = on ? 1 : 0;
    if (want == g_widescreen) return;

    g_widescreen = want;
    g_render_w   = want ? 400 : 320;
    g_fbo_w      = g_render_w * g_scale;

    glBindTexture(GL_TEXTURE_2D, g_fbo_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, g_fbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          g_fbo_w, g_fbo_h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    /* Keep the prev-frame snapshot texture matched to the FBO size,
       otherwise glCopyTexSubImage2D errors with GL_INVALID_VALUE in
       widescreen and the Stealth/Optical-Camo FS samples stale data. */
    if (g_prev_fb_tex) {
        glBindTexture(GL_TEXTURE_2D, g_prev_fb_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_w, g_fbo_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    printf("[gl] widescreen=%d, FBO %dx%d\n", g_widescreen, g_fbo_w, g_fbo_h);
}

void gl_renderer_begin_3d(void) {
    if (!g_enabled) return;
    g_tri3d_count = 0;
    g_line3d_count = 0;
}

void gl_renderer_stats(int *out_tri_verts, int *out_line_verts) {
    if (out_tri_verts)  *out_tri_verts  = (int)g_tri3d_count;
    if (out_line_verts) *out_line_verts = (int)g_line3d_count;
}

int gl_renderer_save_ppm(const char *path)
{
    if (!g_enabled || !path || !g_fbo) return -1;
    int w = g_fbo_w, h = g_fbo_h;
    unsigned char *buf = (unsigned char *)malloc((size_t)w * h * 3);
    if (!buf) return -1;

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    /* GL gives us bottom-up rows; flip vertically while writing. */
    for (int y = h - 1; y >= 0; y--)
        fwrite(buf + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    free(buf);
    return 0;
}
void gl_renderer_begin_2d(void) {
    if (!g_enabled) return;
    g_tri2d_count    = 0;
    g_tri2d_bg_count = 0;
    g_line2d_count   = 0;
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

/* --- PSX framebuffer-readback detection -------------------------------- */
/*
 * NewBlur / NewBlurPure (source/okajima/blur*.c) and kogaku2 Stealth /
 * Optical Camo (source/equip/kogaku2.c) all texture geometry with the
 * framebuffer itself by pointing a primitive's tpage into the PSX display
 * region. The GL backend routes those prims to a different shader branch
 * (vertex flag bit 5) that samples either the captured previous-frame
 * texture (2D blur) or a mid-frame FBO snapshot (3D stealth) instead of
 * the normal CLUT lookup.
 *
 * Detection rule: tp == 2 (16-bit direct), base_y == 0, base_x < 640 --
 * exactly where MGS's double-buffered display lives. Real texture atlases
 * on every disc stage are at y >= 256, so the filter is unambiguous.
 *
 * Centralised here so both gl_submit_tri2d and gl_submit_tri3d use the
 * same rule -- previously inlined twice, easy to drift.
 */
#define PORT_VERT_FLAG_FB_READBACK  0x20u

static inline int port_is_fb_readback_tpage(unsigned short tpage)
{
    int tp = (tpage >> 7) & 3;
    int by = ((tpage >> 4) & 1) * 256;
    if (tpage & 0x800) by += 512;
    int bx = (tpage & 0xF) * 64;

    /* Shape test only. It is necessary but NOT sufficient: the same shape
       describes ordinary 15-bit textures low in VRAM, so the 2D caller adds
       a blur-actor check on top. The 3D caller (Stealth / Optical Camo) must
       NOT add that check -- it has no blur actor. */
    return (tp == 2 && by == 0 && bx < 640);
}

/* Decode tpage to its (base_x, base_y) corner -- used by the first-5-hit
   stderr traces below so the message says "base=(128,0)" etc. */
static inline void port_fb_readback_tpage_base(unsigned short tpage,
                                               int *out_bx, int *out_by)
{
    int by = ((tpage >> 4) & 1) * 256;
    if (tpage & 0x800) by += 512;
    *out_bx = (tpage & 0xF) * 64;
    *out_by = by;
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

    /* Stealth / Optical Camo (source/equip/kogaku2.c) rewrites Snake's
       POLY_GT4 packs to a framebuffer tpage + screen-space UVs. The same
       fb-readback detection runs in gl_submit_tri2d for blur (POLY_FT4 /
       SPRT). See port_is_fb_readback_tpage() above for the rule. */
    if ((flags & 0x1u) && port_is_fb_readback_tpage(tpage)) {
        flags |= PORT_VERT_FLAG_FB_READBACK;
        static int s_logged_3d = 0;
        if (s_logged_3d < 5) {
            int bx, by; port_fb_readback_tpage_base(tpage, &bx, &by);
            fprintf(stderr,
                "[gl] fb-readback 3D prim: tpage=0x%04x base=(%d,%d) "
                "(Stealth / Optical Camo firing)\n", tpage, bx, by);
            s_logged_3d++;
        }
    }

    tri3d_reserve(3);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], a, uv_a, col_a, na, light, dist, face_z, tpage, clut, flags);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], b, uv_b, col_b, nb, light, dist, face_z, tpage, clut, flags);
    pack_vert(&g_tri3d_buf[g_tri3d_count++], c, uv_c, col_c, nc, light, dist, face_z, tpage, clut, flags);
}

static void line3d_reserve(size_t extra)
{
    if (g_line3d_count + extra <= g_line3d_cap) return;
    size_t ncap = g_line3d_cap ? g_line3d_cap * 2 : 1024;
    while (ncap < g_line3d_count + extra) ncap *= 2;
    g_line3d_buf = (GL3DVert *)realloc(g_line3d_buf, ncap * sizeof(GL3DVert));
    g_line3d_cap = ncap;
}

void gl_submit_line3d(
    const int eye_a[3], const int eye_b[3],
    const unsigned char col_a[3], const unsigned char col_b[3],
    int dist)
{
    if (!g_enabled) return;
    line3d_reserve(2);
    int uv0[2] = {0, 0};
    /* Untextured (flags=0); face_z = avg eye-z so it sorts roughly correctly
       even though depth-test handles it on the GPU. */
    int face_z = (eye_a[2] + eye_b[2]) / 2;
    pack_vert(&g_line3d_buf[g_line3d_count++], eye_a, uv0, col_a, NULL, NULL,
              dist, face_z, 0, 0, 0);
    pack_vert(&g_line3d_buf[g_line3d_count++], eye_b, uv0, col_b, NULL, NULL,
              dist, face_z, 0, 0, 0);
}

static void tri2d_bg_reserve(size_t extra)
{
    if (g_tri2d_bg_count + extra <= g_tri2d_bg_cap) return;
    size_t ncap = g_tri2d_bg_cap ? g_tri2d_bg_cap * 2 : 1024;
    while (ncap < g_tri2d_bg_count + extra) ncap *= 2;
    g_tri2d_bg_buf = (GL2DVert *)realloc(g_tri2d_bg_buf, ncap * sizeof(GL2DVert));
    g_tri2d_bg_cap = ncap;
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
    /* Carry the OT-slot depth class (set by port_DrawOTag) into the vertex so
       the 2D shader can depth-test world VFX while keeping HUD/overlay on top. */
    v->depth_flag = port_2d_depth_flag;
    /* Snapshot the current PSX drawing-area clip (set by E3/E4 GPU commands).
       The FS uses this to discard fragments outside the rect, e.g. radar
       enemy dots / vision cones whose PSX coords land past the radar's
       69x52 clip area. */
    extern int clip_x0, clip_y0, clip_x1, clip_y1;
    v->clip[0] = (short)clip_x0;
    v->clip[1] = (short)clip_y0;
    v->clip[2] = (short)clip_x1;
    v->clip[3] = (short)clip_y1;
}

void gl_submit_tri2d(
    const int a[2], const int b[2], const int c[2],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    unsigned short tpage, unsigned short clut, unsigned short flags)
{
    if (!g_enabled) return;

    /* 2D blur (NewBlur / NewBlurPure) -- the same tpage shape as the 3D
       Stealth path in gl_submit_tri3d, but here the shape alone is not
       enough. The codec's panel art and its scrolling scanline bar are also
       15-bit textures low in VRAM, and routing them through the
       prev-framebuffer path made them display the previous frame, painting
       the codec subtitle a second time into the middle of the panel. Require
       a blur actor to actually be alive, the way the widescreen side-bar fill
       keys off port_gmsight_active(). The 3D Stealth caller above is
       deliberately left ungated: it has no blur actor. */
    extern int port_blur_actor_active(void);
    if ((flags & 0x1u) && port_blur_actor_active() &&
        port_is_fb_readback_tpage(tpage)) {
        flags |= PORT_VERT_FLAG_FB_READBACK;
        static int s_logged_2d = 0;
        if (s_logged_2d < 5) {
            int bx, by; port_fb_readback_tpage_base(tpage, &bx, &by);
            fprintf(stderr,
                "[gl] fb-readback prim: tpage=0x%04x base=(%d,%d) "
                "(NewBlur / NewBlurPure firing)\n", tpage, bx, by);
            s_logged_2d++;
        }
    }

    /* PSX GPU silently rejects polygons whose vertex deltas exceed the
       drawing-area limits (1023 horizontal, 511 vertical). Our transformed
       2D prims come from the emulated GTE, which clamps vertices behind the
       near plane to SZ=0 -> do_perspective divides by 1 and produces massive
       screen coords that wrap in 16-bit. Reproducing the hardware reject is
       the simplest way to drop these degenerate triangles instead of
       stretching them across the screen. */
    int minx, maxx, miny, maxy;
    {
        minx = a[0]; maxx = a[0]; miny = a[1]; maxy = a[1];
        if (b[0] < minx) minx = b[0]; if (b[0] > maxx) maxx = b[0];
        if (c[0] < minx) minx = c[0]; if (c[0] > maxx) maxx = c[0];
        if (b[1] < miny) miny = b[1]; if (b[1] > maxy) maxy = b[1];
        if (c[1] < miny) miny = c[1]; if (c[1] > maxy) maxy = c[1];
        if ((maxx - minx) > 1023 || (maxy - miny) > 511) return;
    }

    /* Widescreen letterbox-bar extension.
     *
     * The cinema actor (source/takabe/cinema.c) renders the cutscene black
     * bars as two horizontal strips covering the PSX 320-wide region: top
     * (x=0..320, y=0..24) and bottom (x=0..320, y=184..224). In widescreen
     * they pillarbox to the central 80%, leaving the 3D scene visible in
     * the four corner extras during demos (e.g. d00a).
     *
     * Detect these bars by shape (full PSX width, narrow strip at top or
     * bottom) and color (all vertices grayscale — RGB all equal). The
     * opaque-letterbox phase uses TILE prims with RGB(0,0,0); the fade-in
     * / fade-out phase uses POLY_G4 with RGB(col,col,col) under subtractive
     * blend. Both pass the grayscale check, so the extension covers the
     * full fade arc, not just the steady-state.
     *
     * The fix is to widen the vertex x-range from [0..320] to
     * [-extra .. 320+extra], where extra = (render_w-320)/2 = 40 px PSX in
     * Hor+ widescreen. The standard 2D projection (uXScale = 320/render_w)
     * then maps these to NDC [-1..1] = the full FBO width, so the bars
     * cover the widescreen extras with the same color/blend they already
     * have. The FS clip-rect test happens to use vClip = the live PSX
     * drawing area (typically 0..319) — extended fragments at psx_x < 0
     * or > 319 would be discarded. So we also widen vClip on these verts.
     *
     * UVs aren't touched: TILEs don't sample a texture, and the POLY_G4
     * is flat-colored (UV is unused in the FS for the textured=false
     * path). */
    int xa = a[0], xb = b[0], xc = c[0];
    short clip_w[4] = { 0, 0, 0, 0 };          /* zeroed = use live clip */
    int   override_clip = 0;
    if (g_widescreen
        && minx <= 4 && maxx >= 316
        && (maxy - miny) <= 50
        && (maxy <= 50 || miny >= 174)
        && col_a[0] == col_a[1] && col_a[1] == col_a[2]
        && col_b[0] == col_b[1] && col_b[1] == col_b[2]
        && col_c[0] == col_c[1] && col_c[1] == col_c[2])
    {
        int extra = (g_render_w - 320) / 2;
        if (xa <= 4)   xa = -extra; else if (xa >= 316) xa = 320 + extra;
        if (xb <= 4)   xb = -extra; else if (xb >= 316) xb = 320 + extra;
        if (xc <= 4)   xc = -extra; else if (xc >= 316) xc = 320 + extra;
        /* Override vClip to span the widened range so the FS doesn't
         * discard widescreen-extra fragments. The PSX drawing-area was
         * (0..319, 0..223); we extend horizontally to keep the same
         * vertical bound. */
        clip_w[0] = (short)(-extra);
        clip_w[1] = 0;
        clip_w[2] = (short)(320 + extra - 1);
        clip_w[3] = 223;
        override_clip = 1;
    }
    const int aa[2] = { xa, a[1] };
    const int bb[2] = { xb, b[1] };
    const int cc[2] = { xc, c[1] };

    /* Route world-chanl 2D prims (notably the sphere skybox) to a separate
     * buffer that gets flushed BEFORE 3D geometry. This gives real skybox
     * semantics -- 3D is drawn over the sky with the normal depth test --
     * without any per-vertex z trickery. Only the deep skybox slots (flag==1)
     * go here; shallow world VFX (flag>=2) stay in the fg buffer and are
     * depth-tested via their encoded OT slot. */
    GL2DVert *dst0, *dst1, *dst2;
    if (port_2d_depth_flag == 1) {
        tri2d_bg_reserve(3);
        dst0 = &g_tri2d_bg_buf[g_tri2d_bg_count++];
        dst1 = &g_tri2d_bg_buf[g_tri2d_bg_count++];
        dst2 = &g_tri2d_bg_buf[g_tri2d_bg_count++];
    } else {
        tri2d_reserve(3);
        dst0 = &g_tri2d_buf[g_tri2d_count++];
        dst1 = &g_tri2d_buf[g_tri2d_count++];
        dst2 = &g_tri2d_buf[g_tri2d_count++];
    }
    pack_vert2d(dst0, aa, uv_a, col_a, tpage, clut, flags);
    pack_vert2d(dst1, bb, uv_b, col_b, tpage, clut, flags);
    pack_vert2d(dst2, cc, uv_c, col_c, tpage, clut, flags);
    if (override_clip) {
        /* Replace pack_vert2d's snapshot of clip_x0..clip_y1 (which is
         * still the PSX 0..319 / 0..223 drawing area) with the widened
         * rect; otherwise the FS would discard the widescreen-extra
         * fragments since their recovered psx_x lies outside 0..319. */
        for (int i = 0; i < 4; i++) {
            dst0->clip[i] = clip_w[i];
            dst1->clip[i] = clip_w[i];
            dst2->clip[i] = clip_w[i];
        }
    }
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
                         size_t count, GLenum prim_type, size_t stride,
                         int depth_test)
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
    glUniform1f(glGetUniformLocation(g_tri2d_prog, "uXScale"),
                320.0f / (float)g_render_w);
    glUniform1f(glGetUniformLocation(g_tri2d_prog, "uBlurStrength"),
                port_blur_enabled ? port_blur_strength : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vram_tex);
    /* Previous-frame capture on unit 2 for tris with the fb-readback flag. */
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_prev_fb_tex);
    glActiveTexture(GL_TEXTURE0);
    /* Foreground tris depth-test against the 3D scene (so world VFX get
       occluded) but don't WRITE depth -- HUD/overlay encode z=-1 and always
       pass, and 2D-vs-2D order stays painter's (OT order). */
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    size_t i = 0;
    while (i < count) {
        unsigned short f0 = buf[i].flags;
        int semi0 = (f0 >> 1) & 1;
        int abr0  = (f0 >> 2) & 3;
        int fb0   = (f0 >> 5) & 1;
        size_t run_start = i;
        size_t j = i;
        while (j + stride <= count) {
            unsigned short f = buf[j].flags;
            int s = (f >> 1) & 1;
            int a = (f >> 2) & 3;
            int fb = (f >> 5) & 1;
            if (s != semi0 || (s && a != abr0) || fb != fb0) break;
            j += stride;
        }
        if (fb0) {
            /* NewBlur / NewBlurPure: per-fragment src.alpha gates the blend,
             * so dim/uninitialised prev-fb samples contribute zero (no scene
             * darkening at d00a start) and normal samples produce the 50%
             * ghost trail. Shader sets src.alpha = smoothstep(0..0.05, prev
             * brightness) * 0.5. */
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            /* Widescreen: stretch the blur quad to the full FBO instead of
             * the pillarboxed 320-wide centre. uXScale=1.0 maps PSX
             * [0..320] verts to NDC [-1, 1] (= full FBO width), and the
             * FS samples uPrevFB at fx = psx_x/320 → samples the whole
             * widescreen frame. Only when blur is actually enabled; if
             * port_blur_enabled is off the FS treats these prims as
             * textured (no fb-readback branch) so the stretched geometry
             * would just expose VRAM garbage. The FS clip-rect test is
             * skipped for fb-readback so widescreen edges aren't culled. */
            if (port_blur_enabled) {
                glUniform1f(glGetUniformLocation(g_tri2d_prog, "uXScale"), 1.0f);
            }
        } else if (semi0) {
            glEnable(GL_BLEND);
            apply_abr(abr0);
        } else {
            glDisable(GL_BLEND);
        }
        glDrawArrays(prim_type, (GLint)run_start, (GLsizei)(j - run_start));
        /* Restore pillarboxed uXScale after a stretched fb-readback batch so
         * subsequent non-readback runs in this flush draw at the proper
         * size. (No-op in 4:3 where uXScale was already 1.0.) */
        if (fb0 && port_blur_enabled) {
            glUniform1f(glGetUniformLocation(g_tri2d_prog, "uXScale"),
                        320.0f / (float)g_render_w);
        }
        i = j;
    }

    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    if (depth_test) {           /* leave state as other passes expect */
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    /* Do NOT reset counts -- see gl_renderer_present comment. */
}

static void flush_tri2d(void)
{
    flush_2d_buf(g_tri2d_vao, g_tri2d_vbo, g_tri2d_buf, g_tri2d_count,
                 GL_TRIANGLES, 3, 1 /* depth-test world VFX vs 3D */);
}

static void flush_tri2d_bg(void)
{
    /* Background 2D (sphere skybox) -- reuse the fg VAO/VBO, just hand in
     * the bg buffer. Called from gl_renderer_present BEFORE the 3D pass. */
    flush_2d_buf(g_tri2d_vao, g_tri2d_vbo, g_tri2d_bg_buf, g_tri2d_bg_count,
                 GL_TRIANGLES, 3, 0);
}

static void flush_line2d(void)
{
    flush_2d_buf(g_line2d_vao, g_line2d_vbo, g_line2d_buf, g_line2d_count,
                 GL_LINES, 2, 0);
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

/* ---- gl_renderer_present and its passes -------------------------------- */
/*
 * Each frame's render does six discrete passes against the hi-res FBO:
 *
 *   1.  upload_dirty_vram_rows         -- VRAM CPU->GPU sync
 *   2.  begin_frame_clear              -- bind FBO, set viewport, clear
 *   3.  flush_tri2d_bg                 -- 2D background (sphere skybox)
 *   4.  pass_3d_world                  -- 3D pass with run-batcher and
 *                                          mid-frame fb-readback snapshot
 *   5.  pass_3d_lines                  -- editor wireframe overlay
 *   6a. flush_tri2d + flush_line2d     -- 2D foreground (HUD + menu)
 *   6b. pass_debug_vram                -- alternative VRAM overlay
 *   7.  capture_prev_fb                -- end-of-frame snapshot for blur
 *   8.  blit_fbo_to_window             -- final upscale to window
 *
 * Each pass is its own static function; gl_renderer_present is just the
 * orchestrator. Functions take the minimum context they need (debug_view
 * flag, viewport rect for the final blit) and pull everything else from
 * the module-scope globals. */

static void begin_frame_clear(void)
{
    /* Hi-res FBO at 320*scale x 224*scale. */
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
}

/* The big 3D pass: textured/Gouraud world geometry, run-batched by
   (semi_trans, abr, no_cull, fb_readback) so consecutive prims sharing
   the same bucket use one glDrawArrays. fb_readback (Stealth / Optical
   Camo) runs also trigger a mid-frame glCopyTexSubImage2D into g_prev_fb_tex
   right before they draw -- see port_is_fb_readback_tpage. */
static void pass_3d_world(int debug_view)
{
    /* In codec mode (DG_FrameRate == 2) the PSX pipeline disables 3D and the
       screen is meant to be pure 2D. Skip the 3D batch entirely so stale
       triangles from the pre-codec stage don't bleed through the codec UI. */
    if (debug_view || g_codec_mode || gl_debug_skip_3d || g_tri3d_count == 0)
        return;

    /* flush_tri2d_bg above ran flush_2d_buf, which disables depth test.
       Re-enable it here so the 3D pass actually depth-sorts. Without this
       the whole scene draws in submission order, which looks like geometry
       is flipped / missing. */
    glEnable(GL_DEPTH_TEST);

    /* PSX has no real far clip -- the original hardware uses the OT for
       ordering, not a z clip plane. GL will cull any vertex with
       z_ndc > 1 or z_ndc < -1, which hides distant stage geometry (e.g.
       the s01a docks, eye-z > ~32k) and makes the sphere skybox appear
       to sit in front of the world. GL_DEPTH_CLAMP clamps such vertices
       to the depth range instead of discarding the primitive, matching
       PSX semantics. */
    glEnable(GL_DEPTH_CLAMP);

    glBindVertexArray(g_tri3d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_tri3d_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(g_tri3d_count * sizeof(GL3DVert)),
                 g_tri3d_buf, GL_STREAM_DRAW);

    glUseProgram(g_tri3d_prog);
    glUniform2f(g_tri3d_u_half_screen, g_render_w / 2.0f, 112.0f);
    glUniform2f(g_tri3d_u_near_far, 4.0f, 32768.0f);
    /* Per-viewport projection: editor's ortho panes flip uOrtho on. */
    glUniform1i(g_tri3d_u_ortho, g_vp_ortho[g_active_vp]);
    glUniform4fv(g_tri3d_u_ortho_lrbt, 1, g_vp_ortho_lrbt[g_active_vp]);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uNoTextures"),
                (gl_debug_no_textures || g_vp_wireframe[g_active_vp]) ? 1 : 0);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uFaceId"),
                gl_debug_face_id ? 1 : 0);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uShowNormals"),
                gl_debug_show_normals ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vram_tex);
    /* Previous-frame capture on unit 2 for tris with the fb-readback flag
       (Stealth / Optical Camo -- source/equip/kogaku2.c). */
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_prev_fb_tex);
    glActiveTexture(GL_TEXTURE0);

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
       no_cull, fb_readback) bucket. Submission order is PSX-OT order
       (back-to-front) so a simple bucket-change detector preserves
       painter's semantics. Opaque runs: blend OFF, depth write ON.
       Semi-trans runs: blend via apply_abr, depth write OFF.
       fb_readback runs (Stealth / Optical Camo): mid-frame FBO snapshot
       into g_prev_fb_tex, then opaque write (the FS samples uPrevFB for
       the refraction tint). */
    size_t i = 0;
    while (i < g_tri3d_count) {
        unsigned short f0 = g_tri3d_buf[i].flags;
        int semi0   = (f0 >> 1) & 1;
        int abr0    = (f0 >> 2) & 3;
        int nocull0 = (f0 >> 6) & 1;
        int fb0     = (f0 >> 5) & 1;
        size_t run_start = i;
        /* 3 verts per triangle - stride through whole triangles. */
        size_t j = i;
        while (j + 3 <= g_tri3d_count) {
            unsigned short f = g_tri3d_buf[j].flags;
            int s  = (f >> 1) & 1;
            int a  = (f >> 2) & 3;
            int nc = (f >> 6) & 1;
            int fb = (f >> 5) & 1;
            if (s != semi0 || (s && a != abr0) || nc != nocull0 || fb != fb0) break;
            j += 3;
        }
        if (fb0) {
            /* Mid-frame snapshot. By construction the buffer ahead of
               this run is OT-earlier 3D (level geometry, props), drawn
               before Snake -- so copying the FBO now gives us a clean
               "scene without Snake" the FS can sample for refraction.
               This avoids the accumulation problem you'd get sampling
               the end-of-previous-frame capture (which contains Snake). */
            glActiveTexture(GL_TEXTURE2);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                0, 0, g_fbo_w, g_fbo_h);
            glActiveTexture(GL_TEXTURE0);
            glDisable(GL_BLEND);
            glDepthMask(GL_FALSE);   /* don't occlude later geometry */
        } else if (semi0) {
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
    glDisable(GL_DEPTH_CLAMP);
    glBlendEquation(GL_FUNC_ADD);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* Editor wireframe overlay: world-space lines drawn over the 3D pass.
   Used by ed_render.c for axis gizmos / bound visualisations. Only fires
   when the editor populated g_line3d_buf. */
static void pass_3d_lines(int debug_view)
{
    if (debug_view || g_codec_mode || g_line3d_count == 0) return;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_DEPTH_CLAMP);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    glBindVertexArray(g_line3d_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line3d_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(g_line3d_count * sizeof(GL3DVert)),
                 g_line3d_buf, GL_STREAM_DRAW);

    glUseProgram(g_tri3d_prog);
    glUniform2f(g_tri3d_u_half_screen, g_render_w / 2.0f, 112.0f);
    glUniform2f(g_tri3d_u_near_far, 4.0f, 32768.0f);
    glUniform1i(g_tri3d_u_ortho, g_vp_ortho[g_active_vp]);
    glUniform4fv(g_tri3d_u_ortho_lrbt, 1, g_vp_ortho_lrbt[g_active_vp]);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uNoTextures"), 1);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uFaceId"), 0);
    glUniform1i(glGetUniformLocation(g_tri3d_prog, "uShowNormals"), 0);

    glLineWidth(1.5f);
    glDrawArrays(GL_LINES, 0, (GLsizei)g_line3d_count);

    glDisable(GL_DEPTH_CLAMP);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* "Debug VRAM view" pass: when port_vram_debug_view() is on, dump the
   full 1024x512 VRAM texture to the FBO instead of the normal world. Lets
   us inspect texture layouts / CLUT positions live in the game window. */
static void pass_debug_vram(int debug_view)
{
    if (!debug_view) return;

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

/* End-of-frame snapshot of the rendered FBO into g_prev_fb_tex. PSX
   framebuffer-readback effects (NewBlur, NewBlurPure) source pixels from
   the *previously-displayed* framebuffer; the 2D shader's fb_readback
   branch reads from this texture. GPU-to-GPU copy, no readback stall. */
static void capture_prev_fb(int debug_view)
{
    if (!g_prev_fb_tex || debug_view) return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    glBindTexture(GL_TEXTURE_2D, g_prev_fb_tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_fbo_w, g_fbo_h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);  /* restore for blit below */
}

/* Final upscale: blit the hi-res FBO into the window with aspect-correct
   letterboxing. Editor docking mode skips this entirely (ImGui::Image
   shows the FBO inside a dockable panel instead). */
static void blit_fbo_to_window(int fb_w, int fb_h,
                               int vp_x, int vp_y, int vp_w, int vp_h)
{
    if (g_present_to_window) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0, 0, 0, 1);          /* letterbox bars */
        glClear(GL_COLOR_BUFFER_BIT);
        glBlitFramebuffer(0, 0, g_fbo_w, g_fbo_h,
                          vp_x, vp_y, vp_x + vp_w, vp_y + vp_h,
                          GL_COLOR_BUFFER_BIT,
                          gl_debug_blit_nearest ? GL_NEAREST : GL_LINEAR);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    /* Aspect-correct render_w:224 inside the window (4:3 normally, 16:9 when
       widescreen is enabled via gl_renderer_set_widescreen). */
    const float target = (float)g_render_w / 224.0f;
    int vp_w = fb_w, vp_h = fb_h, vp_x = 0, vp_y = 0;
    if ((float)fb_w / (float)fb_h > target) {
        vp_w = (int)((float)fb_h * target + 0.5f);
        vp_x = (fb_w - vp_w) / 2;
    } else {
        vp_h = (int)((float)fb_w / target + 0.5f);
        vp_y = (fb_h - vp_h) / 2;
    }

    begin_frame_clear();

    extern int port_vram_debug_view(void);
    const int debug_view   = port_vram_debug_view();
    const int vp_wireframe = gl_debug_wireframe || g_vp_wireframe[g_active_vp];

    if (vp_wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    /* Background 2D first (sphere skybox); 3D paints on top of it via the
       normal depth test, giving real "sky fills the frame, geometry occludes"
       semantics. */
    if (!debug_view && !g_codec_mode && !gl_debug_skip_2d)
        flush_tri2d_bg();

    pass_3d_world(debug_view);
    pass_3d_lines(debug_view);

    /* Foreground 2D (HUD, menu, subtitles). VRAM stays uploaded so the FS
       can sample PSX textures/CLUTs from it. */
    if (!debug_view) {
        if (!gl_debug_skip_2d)    flush_tri2d();
        if (!gl_debug_skip_lines) flush_line2d();
    }

    /* Widescreen gas-mask sides. The gas mask sight is built from PSX 2D
     * tiles covering the 320-wide PSX region with a binocular vignette;
     * in widescreen (render_w=400) those tiles only fill the central 80%,
     * leaving the 10% pillarbox on each side to leak the 3D scene through
     * the wider Hor+ FOV. We don't have widescreen mask art, so paint the
     * extras opaque black here -- the gas mask is a vision-restricting
     * overlay anyway, so blacking the peripheries matches its intent.
     * word_800BDCC0 is set/cleared by source/equip/gmsight.c (1 while
     * the gas-mask sight actor is alive, 0 otherwise). */
    if (g_widescreen && !debug_view) {
        extern int port_gmsight_active(void);
        if (port_gmsight_active()) {
            int bar_fbo_w = ((g_render_w - 320) / 2) * g_scale;
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, bar_fbo_w, g_fbo_h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(g_fbo_w - bar_fbo_w, 0, bar_fbo_w, g_fbo_h);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    /* Restore fill mode so the overlay/blit pass isn't wireframed. */
    if (vp_wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    pass_debug_vram(debug_view);

    /* Note: we do NOT reset g_tri3d_count / g_tri2d_count here. Game logic
       runs at 30Hz but rendering runs at 60Hz, so "idle" render frames must
       redraw the same accumulated content. The buffers reset when game logic
       starts a new tick (port_RenderObjects -> gl_renderer_begin_3d,
       port_DrawOTag -> gl_renderer_begin_2d). */

    capture_prev_fb(debug_view);
    blit_fbo_to_window(fb_w, fb_h, vp_x, vp_y, vp_w, vp_h);

    /* SwapWindow happens in port_render after ImGui draws. */
}
