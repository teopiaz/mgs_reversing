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

/* 1 => overlay draws opaquely (no vram==0 discard). Set for codec / any full
 * -screen 2D scene where the game clears to an intended-visible color that
 * may coincide with PSX "transparent" (0x0000 = black). */
void gl_renderer_set_codec_mode(int on);

/* Submit a triangle in eye space (post camera * world transform).
 *   eye_xyz_*: 3 components, fixed-point 4.12 world units, +Z away from camera.
 *   uv_*:      PSX texture coords in 0..255 (per-component, matches KMD UVs).
 *   col_*:     PSX vertex RGB 0..255, 128 = neutral modulation.
 *   dist:      chanl->clip_distance (PSX H register).
 *   face_z:    flat depth used for ALL 3 vertices (PSX painter's-algorithm
 *              sort depth). Typically the centroid cz of the source face.
 *   tpage,clut,flags: PSX GPU texture state. flags bit 0 = textured. */
void gl_submit_tri3d(
    const int eye_xyz_a[3], const int eye_xyz_b[3], const int eye_xyz_c[3],
    const int uv_a[2], const int uv_b[2], const int uv_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    int dist, int face_z,
    unsigned short tpage, unsigned short clut, unsigned short flags);

/* Submit a screen-space semi-transparent triangle. Draws between the 3D pass
 * and the VRAM overlay so it blends against the GL 3D framebuffer — the right
 * layering for PSX OT semi-trans primitives (radar darken, vision cones, etc).
 *   xy_*:  pixel coords in 0..319 x 0..223 framebuffer space.
 *   col_*: PSX RGB 0..255 per vertex.
 *   abr:   PSX blend mode 0=(B+F)/2, 1=B+F, 2=B-F, 3=B+F/4. */
void gl_submit_tri2d_semitrans(
    const int xy_a[2], const int xy_b[2], const int xy_c[2],
    const unsigned char col_a[3], const unsigned char col_b[3], const unsigned char col_c[3],
    int abr);

#ifdef __cplusplus
}
#endif

#endif
