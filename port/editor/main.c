/* MGS Stage Editor — standalone read-only viewer for stage data.
   Boots a minimal subset of the engine (memory + GV + FS + DG + HZD daemons),
   loads stage assets via FS_LoadStageRequest, and renders the map's KMD model
   plus an HZD wireframe overlay using the existing port GL renderer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL.h>

#include "libdg/gl_renderer.h"
#include "editor.h"

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
            if (e.button.button == SDL_BUTTON_RIGHT && !ed_ui_wants_mouse())
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

    editor_engine_init();
    GM_LoadComplete = 1;
    if (ed_load_stage(start_stage) != 0) {
        fprintf(stderr, "editor: failed to load stage '%s'\n", start_stage);
        /* not fatal — let the user pick another from the inspector */
    }

    ed_camera_default();

    g_running = 1;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();

    while (g_running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float  dt  = (float)((double)(now - prev) / (double)freq);
        if (dt > 0.1f) dt = 0.1f;
        prev = now;

        editor_poll();
        if (!ed_ui_wants_keyboard())
            ed_camera_update(dt, g_mouse_dx, g_mouse_dy, g_rmb_held);

        ed_ui_new_frame();
        ed_ui_draw();

        gl_renderer_begin_frame();
        gl_renderer_begin_3d();

        ed_render_frame();   /* KMD map + HZD overlay + actor markers */

        gl_renderer_present();
        ed_ui_render();
        SDL_GL_SwapWindow(g_window);
    }

    ed_ui_shutdown();
    gl_renderer_shutdown();
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
