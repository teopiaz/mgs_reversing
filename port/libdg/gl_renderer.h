#ifndef PORT_GL_RENDERER_H
#define PORT_GL_RENDERER_H

#include <stdint.h>

/* We use opaque pointers in the public API so this header compiles in C files
   that don't include <SDL.h>. Callers pass their SDL_Window / SDL_GLContext
   through as void*; SDL's SDL_GLContext is already typedef'd to void* so this
   is a no-op cast. */
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the GL backend is enabled (PORT_GL env var == "1" at init). */
int gl_renderer_enabled(void);

/* Phase 1: creates an SDL GL context on the given window, loads shaders,
   allocates a 1024x512 R16UI texture mirroring the CPU vram[][] array, and
   a quad VAO. Returns 0 on success, -1 on failure (falls back to SDL path).
   `window` is an SDL_Window * (passed as opaque void* to keep this header SDL-free). */
int gl_renderer_init(void *window);

void gl_renderer_shutdown(void);

/* Called once per frame before game draws into vram[][]. In Phase 1 this is
   a no-op. Later phases will bind the hi-res FBO here. */
void gl_renderer_begin_frame(void);

/* Uploads dirty VRAM rows to the GPU, issues the blit draw call, swaps the
   window. Replaces the SDL_Renderer-based port_vram_display() path. */
void gl_renderer_present(void);

/* Mark a row range of vram[][] as dirty so gl_renderer_present re-uploads it.
   Called by port_LoadImage / port_StoreImage / port_ClearImage / port_MoveImage
   and by the software rasterizer when it writes framebuffer pixels. */
void gl_renderer_mark_vram_dirty(int y0, int y1);

/* Return the SDL GL context for ImGui backend init (cast to SDL_GLContext).
   NULL if disabled. */
void *gl_renderer_context(void);

/* ---- Phase 3: 3D pipeline ------------------------------------------------ */

/* Background color used as GL clear color each frame. Called by
   port_ClearImage when a framebuffer-region clear arrives in GL mode. */
void gl_renderer_set_clear_color(int r8, int g8, int b8);

/* Reset the accumulated 3D / 2D-semitrans triangle buffers. The game logic
 * runs at 30 Hz while rendering runs at 60 Hz, so if we cleared inside
 * gl_renderer_present every second frame would redraw an empty scene (=
 * brightness flicker). Instead port_RenderObjects / port_DrawOTag call these
 * at the START of a game tick, so "idle" render frames between ticks redraw
 * the same content stably. */
void gl_renderer_begin_3d(void);
void gl_renderer_begin_2d(void);

/* --- Debug visualisation flags (read at draw time in gl_renderer_present).
 *     Defaults: 0 = off (normal rendering). Flipped via ImGui debug menu. */
extern int gl_debug_wireframe;      /* glPolygonMode(GL_LINE) for 3D + 2D tris */
extern int gl_debug_no_textures;    /* fragment shader skips texture sample */
extern int gl_debug_skip_3d;        /* skip the entire 3D pass */
extern int gl_debug_skip_2d;        /* skip the 2D triangle pass */
extern int gl_debug_skip_lines;     /* skip the 2D line pass */
extern int gl_debug_blit_nearest;   /* FBO->window: GL_NEAREST instead of GL_LINEAR */
extern int gl_debug_no_cull;        /* skip CPU backface cull (sees all tris) */
extern int gl_debug_cull_cw;        /* 0 = CCW front face, 1 = CW (flip cull) */
extern int gl_debug_face_id;        /* color each 3D tri by gl_PrimitiveID hash */
extern int gl_debug_show_normals;   /* output interpolated normal as RGB */
extern int gl_debug_clear_override; /* 1 = use gl_debug_clear_rgb instead of game */
extern float gl_debug_clear_rgb[3]; /* override clear color, 0..1 per channel */

/* Recreate the hi-res FBO at a new PORT_GL_SCALE. Safe to call between frames
 * (typically from the debug menu). n clamped to [1, 8]. */
void gl_renderer_set_scale(int n);

/* Current internal-resolution scale (1..8). Matches PORT_GL_SCALE. */
int  gl_renderer_get_scale(void);

/* 1 => overlay draws opaquely (no vram==0 discard). Set for codec / any full
 * -screen 2D scene where the game clears to an intended-visible color that
 * may coincide with PSX "transparent" (0x0000 = black). */
void gl_renderer_set_codec_mode(int on);

/* Opaque per-object lighting state. Pointers reference short[3] / short[9] /
 * int[3] arrays in PSX 4.12 fixed-point (light matrices) or raw (ambient 0..255).
 * All can be NULL when flags doesn't set bit 4 (per-pixel lighting off). */
typedef struct {
    const short *light_dir;    /* MATRIX.m[][], 3x3 row-major shorts, /4096 */
    const short *light_color;  /* MATRIX.m[][], 3x3 row-major shorts, /4096 */
    const int   *ambient;      /* 3 ints, /255 for 0..1 */
} GLLight;

/* Submit a triangle in eye space (post camera * world transform).
 *   eye_xyz_*: 3 components, fixed-point 4.12 world units, +Z away from camera.
 *   uv_*:      PSX texture coords in 0..255 (per-component, matches KMD UVs).
 *   col_*:     PSX vertex RGB 0..255, 128 = neutral modulation (Gouraud fallback).
 *   normal_*:  per-vertex normals (short[3], 4.12 fixed-point). NULL if unlit.
 *   light:     per-DG_OBJS lighting state, NULL if unlit.
 *   dist:      chanl->clip_distance (PSX H register).
 *   face_z:    flat face centroid cz for painter's depth.
 *   tpage,clut: PSX texture state.
 *   flags:     b0 textured, b1 semi-trans, b2-3 ABR mode, b4 per-pixel lit. */
void gl_submit_tri3d(
    const int eye_xyz_a[3], const int eye_xyz_b[3], const int eye_xyz_c[3],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    const short *normal_a, const short *normal_b, const short *normal_c,
    const GLLight *light,
    int dist, int face_z,
    unsigned short tpage, unsigned short clut, unsigned short flags);

/* Submit a screen-space 2D triangle through the GL batcher.
 *   xy_*:   pixel coords in 0..319 x 0..223 framebuffer space.
 *   uv_*:   PSX texel coords 0..255 (ignored when flags bit 0 clear).
 *   col_*:  PSX RGB 0..255 per vertex (128 = neutral for textured modulation).
 *   tpage,clut: PSX GPU texture state (ignored when !textured).
 *   flags:  bit 0 = textured, bit 1 = semi-trans, bits 2..3 = PSX ABR mode. */
void gl_submit_tri2d(
    const int xy_a[2], const int xy_b[2], const int xy_c[2],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    unsigned short tpage, unsigned short clut, unsigned short flags);

/* Back-compat helper: untextured semi-transparent triangle. Equivalent to
 * gl_submit_tri2d with NULL UVs, flags = 2 | ((abr & 3) << 2). */
void gl_submit_tri2d_semitrans(
    const int xy_a[2], const int xy_b[2], const int xy_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    int abr);

/* Submit a 2D line (GL_LINES primitive). Same flags layout as gl_submit_tri2d
 * (typically not textured; semi-trans + abr honored). */
void gl_submit_line(
    const int xy_a[2], const int xy_b[2],
    const unsigned char col_a[3], const unsigned char col_b[3],
    unsigned short flags);

#ifdef __cplusplus
}
#endif

#endif
