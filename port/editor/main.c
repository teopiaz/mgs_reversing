/* MGS Stage Editor — standalone read-only viewer for stage data.
   Boots a minimal subset of the engine (memory + GV + FS + DG + HZD daemons),
   loads stage assets via FS_LoadStageRequest, and renders the map's KMD model
   plus an HZD wireframe overlay using the existing port GL renderer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include "libdg/gl_renderer.h"
#include "editor.h"
#include "ed_dmo.h"

#define WIN_W 1280
#define WIN_H 720

static SDL_Window *g_window;
extern int g_running;
static int g_mouse_dx = 0, g_mouse_dy = 0;
static int g_rmb_held = 0;

/* From port_memory.c / libfs.c / libgv / libdg / libhzd */
extern int  port_init_memory(void);
extern void InitGeom(void);
extern void GV_StartDaemon(void);
extern void DG_StartDaemon(void);
extern void HZD_StartDaemon(void);
extern void FS_StartDaemon(void);
extern void *FS_LoadStageRequest(const char *dirname);
extern const char *port_iso_override;
extern int iso_path_looks_like_image(const char *);
extern int GM_LoadComplete;
extern int port_force_gouraud_neutral;

static int editor_init_window(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL |
                   SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    g_window = SDL_CreateWindow(
        "MGS Stage Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, flags);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    /* Force-enable the GL backend in gl_renderer (it reads PORT_GL env). */
    setenv("PORT_GL", "1", 1);

    if (gl_renderer_init(g_window) < 0 || !gl_renderer_enabled()) {
        fprintf(stderr, "editor: GL init failed\n");
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    return 0;
}

extern void spu_emu_init(void);
extern void sd_init(void);
extern int  sd_mem_alloc(void);

static void editor_engine_init(void)
{
    /* Bring up the bare minimum to load stage assets. Order matches main_game.c
       game_init(); we omit GCL/GM daemons (not needed for asset load).

       Sound init is required because FS_LoadStageRequest processes the
       stage's '.wvx' wave-data tag by calling SpuWrite — without SPU emu
       initialized this would crash or hang. We don't actually play any
       audio; we just want the load path to complete. */
    port_init_memory();
    InitGeom();

    spu_emu_init();
    sd_mem_alloc();
    sd_init();

    GV_StartDaemon();
    FS_StartDaemon();
    DG_StartDaemon();
    HZD_StartDaemon();

    /* Editor is unlit. Tells libdg_stub.o (if it ever runs) to use neutral 128
       vertex colors, but we do our own rendering so this is mostly defensive. */
    port_force_gouraud_neutral = 1;
}

static void editor_poll(void)
{
    g_mouse_dx = 0;
    g_mouse_dy = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ed_ui_process_event(&e);
        switch (e.type) {
        case SDL_QUIT:
            g_running = 0;
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) g_running = 0;
            if (e.key.keysym.sym == SDLK_F11 ||
                (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                Uint32 f = SDL_GetWindowFlags(g_window);
                SDL_SetWindowFullscreen(g_window,
                    (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            if (e.key.keysym.sym == SDLK_HOME) ed_camera_default();
            break;
        case SDL_MOUSEBUTTONDOWN:
            /* Once the 3D View is a docked ImGui window, ed_ui_wants_mouse()
             * is always true while the cursor is over it, which would block
             * camera RMB-drag. Allow it when the 3D View pane is the hovered
             * one — ed_ui_3dview_active() is set by ImGui's IsWindowHovered
             * the previous frame, which is fine for a click that's already
             * inside the panel. */
            if (e.button.button == SDL_BUTTON_RIGHT &&
                (!ed_ui_wants_mouse() || ed_ui_3dview_active()))
                g_rmb_held = 1;
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_RIGHT) g_rmb_held = 0;
            break;
        case SDL_MOUSEMOTION:
            g_mouse_dx += e.motion.xrel;
            g_mouse_dy += e.motion.yrel;
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* CLI: --iso <path>, --stage <name>, or positional disc image. */
    const char *start_stage = "s01a";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            port_iso_override = argv[++i];
        } else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) {
            start_stage = argv[++i];
        } else if (iso_path_looks_like_image(argv[i])) {
            port_iso_override = argv[i];
        }
    }
    if (port_iso_override)
        printf("editor: disc image = %s\n", port_iso_override);

    if (editor_init_window() < 0) return 1;
    ed_ui_init(g_window);

    /* Hammer-style docking: the rendered FBO is shown via ImGui::Image
     * inside a dockable "3D View" panel, so suppress the FBO->window
     * blit (it would just paint behind the dockspace gutters). */
    gl_renderer_set_present_to_window(0);

    editor_engine_init();
    GM_LoadComplete = 1;
    if (ed_load_stage(start_stage) != 0) {
        fprintf(stderr, "editor: failed to load stage '%s'\n", start_stage);
        /* not fatal — let the user pick another from the inspector */
    }

    /* Headless-A/B helper: PORT_AUTOPLAY_DEMO=1 hits Play on the loaded
     * stage's default .dmo right after load, so a CI/script can run the
     * editor without a human clicking the button. PORT_AUTOPLAY_DEMO_FILE
     * additionally bypasses the GCL state-machine and spawns the demo
     * streaming actor (DM_ThreadFile) for the given .dmo directly —
     * needed for stages like d00a where the demo only fires under
     * specific GCL var conditions the editor doesn't reach. */
    {
        const char *e = getenv("PORT_AUTOPLAY_DEMO");
        if (e && atoi(e) > 0) ed_demo_play();
        const char *f = getenv("PORT_AUTOPLAY_DEMO_FILE");
        if (f && *f) {
            extern int DM_ThreadFile(int flag, char *filename);
            char buf[256]; snprintf(buf, sizeof(buf), "%s", f);
            int ok = DM_ThreadFile(1 /* -e flag */, buf);
            printf("editor: PORT_AUTOPLAY_DEMO_FILE='%s' DM_ThreadFile=%d\n", f, ok);
        }
    }

    ed_camera_default();
    ed_camera_ortho_default(&g_top_cam,   ED_ORTHO_TOP);
    ed_camera_ortho_default(&g_front_cam, ED_ORTHO_FRONT);
    ed_camera_ortho_default(&g_side_cam,  ED_ORTHO_SIDE);

    g_running = 1;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();

    while (g_running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float  dt  = (float)((double)(now - prev) / (double)freq);
        if (dt > 0.1f) dt = 0.1f;
        prev = now;

        editor_poll();
        /* Demo runtime tick (no-op when transport is STOPPED or PAUSED).
         * Done before the camera/UI step so the camera-snapshot exposed
         * by ed_demo.c is fresh when the Demo tab renders. */
        ed_demo_tick();
        /* Camera input is gated on the panel under the cursor:
         *   3D pane  → fly-style perspective camera (g_cam, ed_camera_update)
         *   Top/Front/Side → ortho cam pan + zoom (ed_camera_ortho_*)
         * The active-pane flags are updated by ed_ui_draw on the previous
         * frame, so they lag by 1 frame — fine for integrated dt input. */
        if (ed_ui_3dview_active())
            ed_camera_update(dt, g_mouse_dx, g_mouse_dy, g_rmb_held);

        ed_ui_new_frame();
        ed_ui_draw();
        gl_renderer_begin_frame();

        /* --- Render every viewport whose panel is visible. ImGui has by
         *     now told us each panel's content-region size via ed_ui's
         *     calls to gl_renderer_resize_viewport(idx, w, h). We re-walk
         *     the same scene 4 times, switching FBO + projection mode for
         *     each. Cheap: ~5k tris × 4 = 20k tris/frame, well within
         *     budget on any GPU. */
        struct { int idx; int is_ortho; EdOrthoCam *cam; } passes[] = {
            { GL_VIEWPORT_3D,    0, NULL          },
            { GL_VIEWPORT_TOP,   1, &g_top_cam    },
            { GL_VIEWPORT_FRONT, 1, &g_front_cam  },
            { GL_VIEWPORT_SIDE,  1, &g_side_cam   },
        };
        for (int p = 0; p < 4; p++) {
            unsigned int tex = gl_renderer_get_viewport_color(passes[p].idx);
            if (!tex) continue;          /* panel not yet sized → skip */
            gl_renderer_set_active_viewport(passes[p].idx);
            gl_renderer_begin_3d();
            if (passes[p].is_ortho) {
                /* Compute LRBT from the cam + current viewport size, then
                 * tell the renderer about ortho + wireframe mode. */
                /* viewport w/h were stored by gl_renderer_resize_viewport. */
                extern int gl_renderer_get_viewport_w(int idx);
                extern int gl_renderer_get_viewport_h(int idx);
                /* Inline accessors not available — use a small helper here. */
                int vw, vh;
                vw = vh = 0;
                /* We just need w/h; read them via the color texture's size:
                 * since the editor's panel sizing already called resize, both
                 * are set. We expose getters below. */
                extern int gl_renderer_get_active_viewport(void);
                (void)gl_renderer_get_active_viewport;
                /* Use the cam's stored content size via getters added in
                 * gl_renderer.c. */
                gl_renderer_get_viewport_size(passes[p].idx, &vw, &vh);
                float lrbt[4];
                ed_camera_ortho_compute(passes[p].cam, vw, vh, lrbt);
                gl_renderer_set_viewport_ortho(passes[p].idx, 1,
                                               lrbt[0], lrbt[1], lrbt[2], lrbt[3]);
                gl_renderer_set_viewport_wireframe(passes[p].idx, 1);
                ed_render_frame_ortho(passes[p].cam, vw, vh);
            } else {
                gl_renderer_set_viewport_ortho(passes[p].idx, 0, 0, 0, 0, 0);
                gl_renderer_set_viewport_wireframe(passes[p].idx, 0);
                /* During demo playback, switch the 3D pane to the engine's
                 * runtime camera + actor render path so the cinema, demo
                 * dolls, particle emitters etc. render with their live
                 * animations. The Top/Front/Side ortho panes stay on the
                 * editor's static walker — they're for spatial layout
                 * inspection, not playback. */
                if (g_demo_loaded && passes[p].idx == GL_VIEWPORT_3D)
                    ed_render_frame_demo();
                else
                    ed_render_frame();
            }
            gl_renderer_present();
        }
        /* Restore active viewport to slot 0 so debug HUDs / VRAM viewer that
         * read g_fbo_color via the legacy alias keep showing the 3D view. */
        gl_renderer_set_active_viewport(GL_VIEWPORT_3D);

        /* Clear the default framebuffer to dock-gutter dark grey. ImGui's
         * dockspace + windows paint on top; the 3D View panel samples the
         * FBO via ImGui::Image so the rendered scene shows there. */
        {
            int fb_w = 0, fb_h = 0;
            SDL_GL_GetDrawableSize(g_window, &fb_w, &fb_h);
            glViewport(0, 0, fb_w, fb_h);
            glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        ed_ui_render();
        SDL_GL_SwapWindow(g_window);
    }

    ed_ui_shutdown();
    gl_renderer_shutdown();
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
