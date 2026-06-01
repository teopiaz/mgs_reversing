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

/* NewBlur / NewBlurPure intensity controls -- imgui Renderer tab toggle
 * + slider. When disabled, the 2D fb-readback prim falls through to a
 * flat vCol (no prev-frame sample). When enabled, the strength multiplier
 * scales the prev-frame tap before the semi-trans blend (default 1.4). */
extern int   port_blur_enabled;
extern float port_blur_strength;

/* Dynamic shadow mapping (port-only). Casters opt in via DG_FLAG_SHADOW
 * on their DG_OBJS (libdg_stub.c auto-flags Snake's body); receivers
 * are DG_FLAG_SHADE geometry. Both pieces live in libdg_stub.c — the
 * renderer just consumes a per-frame view via gl_renderer_set_shadow_view.
 *   port_shadow_strength  (0..1): how dark a shadow gets, 0 = off
 *   port_shadow_bias              : depth-test bias to suppress acne
 *   port_shadow_radius            : world-units half-size of the ortho frustum */
extern int   port_shadow_enabled;
extern float port_shadow_strength;
extern float port_shadow_bias;
extern float port_shadow_radius;
extern float port_shadow_depth_half;
extern int   port_shadow_debug;     /* 0=off, 1=receivers solid red, 2=sc.xy+frustum, 3=z vs blocker, 4=shadow term as gray */

/* Manual shadow-light direction override. When override == 0, the
 * shadow camera tracks the stage's DG_LightMatrix.m[0] (which may be
 * near-horizontal and produce thin floor shadows). When 1, the shadow
 * camera uses port_shadow_light_override_dir as the world-space
 * TOWARD-light direction (i.e., from Snake toward the light source).
 * libdg_stub.c normalises on its way through gl_renderer_set_shadow_view. */
extern int   port_shadow_light_override;
extern float port_shadow_light_override_dir[3];

/* Push Snake's eye-space position and the eye-space main light direction
 * for this frame. Call from port_RenderObjects after the chanl
 * transforms have been computed. Skipping the call (or no caster verts
 * submitted) makes the shadow pass a no-op for the frame. */
void gl_renderer_set_shadow_view(float snake_x_eye, float snake_y_eye, float snake_z_eye,
                                 float light_x_eye, float light_y_eye, float light_z_eye);

/* Read back the shadow map's GL texture name + last-frame statistics so
 * the imgui Shadow debug tab can render it and report whether the pass
 * actually fired. `view_valid` mirrors gl_renderer's internal flag — 1
 * means a Snake-eye + light-eye pair was pushed this frame; 0 means the
 * shadow pass was skipped (no caster, or no view). */
typedef struct {
    unsigned int tex_id;             /* GL_TEXTURE_2D name; 0 if uninit */
    int          size;               /* texture dim, square (e.g. 1024) */
    int          caster_vert_count;  /* last frame's caster vertex count */
    int          view_valid;         /* did libdg_stub.c push a view? */
    float        snake_eye[3];       /* what was last pushed */
    float        light_eye[3];
    float        matrix[16];         /* light_proj * light_view, col-major */
} GLShadowDebug;
void gl_renderer_get_shadow_debug(GLShadowDebug *out);

/* Read a region of the hi-res FBO back into a host RGB buffer. Coordinates
 * are in PSX framebuffer pixels (0..320 x 0..224). The function multiplies
 * by the current scale and reads `psx_w * scale` x `psx_h * scale` RGB
 * triples into `out_rgb`. Buffer must be sized for that. Returns 1 on
 * success, 0 if disabled or FBO not initialised. Used by the photo
 * exporter to capture the actual rendered scene at native resolution
 * (StoreImage from emulated VRAM only sees CPU rasterizer output, not
 * the GL pipeline). */
int gl_renderer_read_psx_region(int psx_x, int psx_y, int psx_w, int psx_h,
                                unsigned char *out_rgb,
                                int *out_w, int *out_h);

/* Hot-reload the shader programs from their on-disk source files
 * (port/libdg/shaders/{blit,tri3d,tri2d}.{vert,frag}). Useful for iterating
 * on GLSL without rebuilding. Returns 1 if every program reloaded
 * successfully, 0 if any compile/link failed (in which case the previous
 * programs are kept). Safe to call between frames; bound to a debug-pane
 * button in imgui_debug.cpp. */
int gl_renderer_reload_shaders(void);

/* Recreate the hi-res FBO at a new PORT_GL_SCALE. Safe to call between frames
 * (typically from the debug menu). n clamped to [1, 8]. */
void gl_renderer_set_scale(int n);

/* Current internal-resolution scale (1..8). Matches PORT_GL_SCALE. */
int  gl_renderer_get_scale(void);

/* 16:9 Hor+ widescreen toggle. When on, internal render width becomes 400
 * (up from 320) so the 3D scene has wider horizontal FOV; 2D/HUD stays
 * pillar-boxed at 4:3 in the center of the wider frame. */
void gl_renderer_set_widescreen(int on);
int  gl_renderer_get_widescreen(void);

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

/* World-space (post-camera-transform) line, for editor wireframe overlays.
 * Eye-space coords match gl_submit_tri3d. dist is the same H register value.
 * Color is per-vertex RGB; alpha forced to 255. The line draws after the 3D
 * tri pass with depth test still enabled so it occludes correctly. */
void gl_submit_line3d(
    const int eye_a[3], const int eye_b[3],
    const unsigned char col_a[3], const unsigned char col_b[3],
    int dist);

/* Editor stats overlay. Returns the per-frame submitted vertex counts
 * (triangles are 3 verts each; lines 2 each). Cleared each frame at
 * gl_renderer_begin_3d. */
void gl_renderer_stats(int *out_tri_verts, int *out_line_verts);

/* Save the hi-res FBO contents as a P6 PPM at `path`. Returns 0 on success.
 * Lives next to the renderer because we need the FBO color attachment + its
 * dimensions, which are private to gl_renderer.c. */
int gl_renderer_save_ppm(const char *path);

/* --- Editor docking integration ------------------------------------------ */

/* Resize the hi-res FBO to (w, h). Safe to call between frames. Re-allocates
 * the color texture and depth renderbuffer if the size changed. The editor
 * uses this to match the FBO to the dockable "3D View" panel's content
 * region. Both dims clamped to >= 1. */
void gl_renderer_resize_fbo(int w, int h);

/* Returns the GL texture name (uint cast to uintptr_t for ImGui::Image). The
 * texture is updated in-place every call to gl_renderer_present(). 0 if the
 * renderer hasn't initialised yet. */
unsigned int gl_renderer_get_fbo_color(void);

/* Suppress the FBO->window blit at the end of gl_renderer_present(). The
 * editor's docking layout shows the FBO via ImGui::Image instead, so the
 * full-window blit would just paint behind the dockspace gutters. Defaults
 * to 0 (blit on); the editor sets it to 1 at startup. */
void gl_renderer_set_present_to_window(int on);

/* --- Multi-viewport rendering (Phase 2 of editor overhaul) ---------------
 *
 * Slot 0 is the legacy 3D textured viewport (and the live game's only
 * viewport). Slots 1..3 are the editor's Top / Front / Side ortho
 * wireframe panes. Each slot owns its own FBO + color attachment + depth
 * renderbuffer at potentially different sizes.
 *
 * Render flow per frame for a viewport:
 *   gl_renderer_resize_viewport(idx, w, h);
 *   gl_renderer_set_active_viewport(idx);
 *   gl_renderer_set_viewport_ortho(idx, on, l, r, b, t);   (set up once)
 *   gl_renderer_set_viewport_wireframe(idx, on);           (set up once)
 *   gl_renderer_begin_3d();
 *   ... ed_render_frame() / scene submission ...
 *   gl_renderer_present();
 *
 * The editor calls ImGui::Image(gl_renderer_get_viewport_color(idx), ...)
 * inside the dockable panel for each viewport. */
#define GL_VIEWPORT_3D       0
#define GL_VIEWPORT_TOP      1
#define GL_VIEWPORT_FRONT    2
#define GL_VIEWPORT_SIDE     3

void gl_renderer_set_active_viewport(int idx);
int  gl_renderer_get_active_viewport(void);
void gl_renderer_resize_viewport(int idx, int w, int h);
void gl_renderer_get_viewport_size(int idx, int *out_w, int *out_h);
unsigned int gl_renderer_get_viewport_color(int idx);
void gl_renderer_set_viewport_ortho(int idx, int on,
                                    float l, float r, float b, float t);
void gl_renderer_set_viewport_wireframe(int idx, int on);

#ifdef __cplusplus
}
#endif

#endif
