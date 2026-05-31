#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <SDL.h>
#include "imgui_debug.h"
#include "test_server.h"
#include "libdg/gl_renderer.h"
#include "port_config.h"
#include "port_menu.h"

#include <sys/ucontext.h>

static void crash_handler_si(int sig, siginfo_t *info, void *ucontext)
{
    void *bt[30];
    int n = backtrace(bt, 30);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    if (info) {
        fprintf(stderr, "  si_addr=%p si_code=%d\n", info->si_addr, info->si_code);
    }
#if defined(__APPLE__) && defined(__aarch64__)
    if (ucontext) {
        ucontext_t *uc = (ucontext_t *)ucontext;
        fprintf(stderr, "  pc=%llx lr=%llx sp=%llx fp=%llx\n",
                (unsigned long long)uc->uc_mcontext->__ss.__pc,
                (unsigned long long)uc->uc_mcontext->__ss.__lr,
                (unsigned long long)uc->uc_mcontext->__ss.__sp,
                (unsigned long long)uc->uc_mcontext->__ss.__fp);
        fprintf(stderr, "  x0=%llx x1=%llx x2=%llx x3=%llx\n",
                (unsigned long long)uc->uc_mcontext->__ss.__x[0],
                (unsigned long long)uc->uc_mcontext->__ss.__x[1],
                (unsigned long long)uc->uc_mcontext->__ss.__x[2],
                (unsigned long long)uc->uc_mcontext->__ss.__x[3]);
    }
#endif
    backtrace_symbols_fd(bt, n, 2);
    _exit(1);
}

static void port_install_crash_handler(void)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler_si;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 224
#define WINDOW_SCALE  4

static SDL_Window   *g_window;
static SDL_Renderer *g_renderer;
bool                 g_running;  /* non-static: test_server.c may set it false to quit */
const char          *port_argv0 = NULL;  /* used by imgui Restart button */

/* Auto-load hook: when PORT_AUTOLOAD_STAGE=<name> is set, watch for the
   engine to settle on the select menu (load complete, current stage ==
   "select") and then synthesise the same load that the imgui "Custom
   stage launcher" button would do. Used for headless smoke tests. */
static void port_autoload_tick(void)
{
    static int   armed = 0;          /* 0=idle, 1=armed and waiting, 2=fired */
    static int   fire_at_frame = 0;
    static int   frame = 0;
    frame++;

    if (armed >= 2) return;

    const char *env = getenv("PORT_AUTOLOAD_STAGE");
    if (!env || !*env) return;

    extern char  port_current_stage[16];
    extern int   GM_LoadComplete;
    extern int   GV_StrCode(const char *);
    extern void  GM_SetArea(int hash, const char *name);
    extern int   GM_LoadRequest;
    extern short linkvarbuf[];

    if (armed == 0
        && strcmp(port_current_stage, "select") == 0
        && GM_LoadComplete) {
        armed = 1;
        fire_at_frame = frame + 120;     /* ~2s settle time */
        printf("[port] auto-load armed for '%s' at frame %d (current=%d)\n",
               env, fire_at_frame, frame);
    }
    if (armed == 1 && frame >= fire_at_frame) {
        armed = 2;
        int hash = GV_StrCode(env);
        linkvarbuf[6]  = (short)hash;                  /* GM_CurrentStageFlag */
        linkvarbuf[7]  = (short)GV_StrCode("main");    /* GM_CurrentMapFlag */
        linkvarbuf[8]  = 0;                             /* GM_SnakePosX */
        linkvarbuf[9]  = 0;                             /* GM_SnakePosY */
        linkvarbuf[10] = 8000;                          /* GM_SnakePosZ */
        GM_SetArea(hash, env);
        GM_LoadRequest = 0x91;
        printf("[port] auto-load: firing load '%s' (frame %d)\n", env, frame);
    }
}

static int port_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Defaults first so the menu has something to read even when the
     * file is missing; then overlay any saved settings. SDL must be
     * initialized first because the defaults reference SDL_SCANCODE_*
     * via macro expansion. */
    port_config_set_defaults(&g_port_config);
    if (port_config_load(PORT_CONFIG_DEFAULT_PATH)) {
        printf("port: loaded config from %s\n", PORT_CONFIG_DEFAULT_PATH);
    } else {
        /* Write the defaults out so the user sees the file exist after
         * first launch and can hand-edit it. Skipped when the file
         * already exists but is empty/garbage — port_config_load already
         * returned 0 in that case, but we shouldn't clobber a file the
         * user might be in the middle of editing. */
        FILE *check = fopen(PORT_CONFIG_DEFAULT_PATH, "r");
        if (!check) {
            printf("port: no config file — writing defaults\n");
            port_config_save(PORT_CONFIG_DEFAULT_PATH);
        } else {
            fclose(check);
            printf("port: config file present but unparseable — using defaults\n");
        }
    }

    /* PORT_GL env var still works as an override, but config wins by default.
     * Env override exists because CI / replay scripts already set it. */
    const char *gl_env = getenv("PORT_GL");
    int want_gl = gl_env ? (strcmp(gl_env, "1") == 0)
                         : g_port_config.gl_enabled;

    /* gl_renderer_init() reads PORT_GL itself and early-exits when not "1".
     * Make sure the env var matches our final decision so the config-driven
     * path can enable GL without the user having to set the var manually. */
    setenv("PORT_GL", want_gl ? "1" : "0", 1);

    Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    if (g_port_config.fullscreen)
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (want_gl) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        win_flags |= SDL_WINDOW_OPENGL;
    } else {
        /* Bilinear filtering for texture upscaling (smoother than nearest-neighbor) */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    }

    g_window = SDL_CreateWindow(
        "Metal Gear Solid",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        g_port_config.window_w, g_port_config.window_h,
        win_flags);

    if (!g_window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    if (want_gl) {
        if (gl_renderer_init(g_window) < 0 || !gl_renderer_enabled()) {
            /* Recovery path: a too-aggressive saved config (e.g. 4K
             * fullscreen + 8× scale) can fail GL init and lock the user
             * out — they can't open the menu to fix it. Reset video
             * settings to known-safe defaults, persist them so the next
             * launch starts clean, and retry GL init with a small
             * windowed surface. The user lands in the menu and can dial
             * back up gradually. */
            fprintf(stderr, "port: GL init failed at %dx%d fs=%d scale=%d ws=%d — falling back to safe defaults.\n",
                    g_port_config.window_w, g_port_config.window_h,
                    g_port_config.fullscreen, g_port_config.gl_scale,
                    g_port_config.widescreen);
            SDL_DestroyWindow(g_window);

            g_port_config.window_w   = 1280;
            g_port_config.window_h   = 896;
            g_port_config.fullscreen = 0;
            g_port_config.gl_scale   = 4;
            g_port_config.widescreen = 0;
            port_config_save(PORT_CONFIG_DEFAULT_PATH);

            g_window = SDL_CreateWindow(
                "Metal Gear Solid",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                g_port_config.window_w, g_port_config.window_h,
                SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
            if (!g_window || gl_renderer_init(g_window) < 0 || !gl_renderer_enabled()) {
                fprintf(stderr, "port: GL init still failing after fallback. Set PORT_GL=0 to run with the SDL backend.\n");
                if (g_window) SDL_DestroyWindow(g_window);
                SDL_Quit();
                return -1;
            }
            printf("port: GL backend active (recovered with safe defaults)\n");
            return 0;
        }
        printf("port: GL backend active\n");
        return 0;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED |
        (g_port_config.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));

    if (!g_renderer)
    {
        /* Fallback to software renderer (for headless/offscreen environments) */
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (!g_renderer)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }

    /* Logical size keeps aspect ratio correct when window is resized */
    SDL_RenderSetLogicalSize(g_renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);

    printf("port: SDL initialized\n");
    return 0;
}

static void port_shutdown(void)
{
    if (gl_renderer_enabled())
    {
        gl_renderer_shutdown();
    }
    if (g_renderer)
    {
        SDL_DestroyRenderer(g_renderer);
    }
    if (g_window)
    {
        SDL_DestroyWindow(g_window);
    }
    SDL_Quit();
    printf("port: shutdown complete\n");
}

/* When true, port_poll_events forwards key/button events to port_menu_handle_event
 * (for capture-mode rebinds) and treats ESC as "back to main menu" instead of
 * quitting. Set during the pre-game menu loop in main(). */
static bool s_in_menu = false;

static void port_poll_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        imgui_process_event(&event);
        if (s_in_menu) port_menu_handle_event(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            g_running = false;
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE && !s_in_menu)
                g_running = false;
            if (event.key.keysym.sym == SDLK_TAB)
            {
                extern void port_vram_toggle_debug(void);
                port_vram_toggle_debug();
            }
            if (event.key.keysym.sym == SDLK_F1)
                imgui_toggle_debug();
            if (event.key.keysym.sym == SDLK_F5)
            {
                extern int gl_renderer_reload_shaders(void);
                gl_renderer_reload_shaders();
            }
            if (event.key.keysym.sym == SDLK_F11 ||
                (event.key.keysym.sym == SDLK_RETURN &&
                 (event.key.keysym.mod & KMOD_ALT)))
            {
                Uint32 flags = SDL_GetWindowFlags(g_window);
                SDL_SetWindowFullscreen(g_window,
                    (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
        {
            extern void port_open_controller(void);
            port_open_controller();
            break;
        }
        }
    }
}

/* VRAM display — implemented in port/libdg/vram.c */
extern void port_vram_init(SDL_Renderer *renderer);
extern void port_vram_display(void);

static void port_render(void)
{
    if (gl_renderer_enabled())
    {
        gl_renderer_present();
        imgui_render(NULL);
        SDL_GL_SwapWindow(g_window);
    }
    else
    {
        port_vram_display();
        imgui_render(g_renderer);
        SDL_RenderPresent(g_renderer);
    }

    /* Update window title with FPS every second */
    {
        static Uint32 last_fps_time = 0;
        static int fps_count = 0;
        fps_count++;
        Uint32 now = SDL_GetTicks();
        if (now - last_fps_time >= 1000)
        {
            char title[64];
            snprintf(title, sizeof(title), "Metal Gear Solid — %d fps%s",
                     fps_count, gl_renderer_enabled() ? " [GL]" : "");
            SDL_SetWindowTitle(g_window, title);
            fps_count = 0;
            last_fps_time = now;
        }
    }
}

/* From main_game.c */
extern void game_init(void);
extern void game_tick(void);

int main(int argc, char *argv[])
{
    port_argv0 = argv[0];   /* for the ImGui Restart button */

    /* Force line-buffered stdout so we can see output before the process ends */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Parse CLI. Accept `--iso <path>` or any positional arg that looks like
     * a disc image (.iso/.bin/.cue/.img). libfs.c reads port_iso_override on
     * FS_StartDaemon. */
    extern const char *port_iso_override;
    extern int         iso_path_looks_like_image(const char *);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            port_iso_override = argv[++i];
        } else if (iso_path_looks_like_image(argv[i])) {
            port_iso_override = argv[i];
        }
    }
    if (port_iso_override)
        printf("port: disc image = %s\n", port_iso_override);

    port_install_crash_handler();

    if (port_init() < 0)
    {
        return 1;
    }

    port_vram_init(g_renderer);  /* safe with NULL renderer; sets up vram[][] */
    imgui_init(g_window, g_renderer);  /* renderer==NULL => GL backend */
    TEST_HARNESS_init();

    extern void port_update_pad(void);
    extern void port_open_controller(void);
    port_open_controller();

    /* Apply saved widescreen / scale to the just-initialized GL renderer
     * before any game frames render. (game_init follows, so it sees the
     * final FBO size.) */
    if (gl_renderer_enabled()) {
        extern void gl_renderer_set_widescreen(int on);
        gl_renderer_set_widescreen(g_port_config.widescreen);
        /* FBO scale is set by the engine via gl_renderer_resize_fbo using
         * its own SCREEN_HEIGHT * scale math; we just publish g_port_config
         * for the imgui debug pane and respect PORT_GL_SCALE env var, which
         * existing infrastructure consumes during gl_renderer_init. */
    }

    /* ------- Pre-game menu loop -------
     * Skip entirely for automated runs. The menu blocks the game loop
     * until the user clicks Start, which would hang any replay / CI
     * script that drives the port via env vars or the test socket. */
    g_running = true;
    int skip_menu = 0;
    if (getenv("PORT_SKIP_MENU")        && getenv("PORT_SKIP_MENU")[0] == '1') skip_menu = 1;
    if (getenv("MGS_AUTO_INPUT"))       skip_menu = 1;   /* scripted button sequence */
    if (getenv("MGS_INPUT_REPLAY"))     skip_menu = 1;   /* replay log */
    if (getenv("MGS_INPUT_RECORD"))     skip_menu = 1;   /* record log */
    if (getenv("PORT_AUTOLOAD_STAGE"))  skip_menu = 1;   /* stage auto-jump */
    if (skip_menu) printf("port: skipping pre-game menu (automation env var)\n");

    s_in_menu = !skip_menu;
    port_menu_init();
    {
        const double FRAME_MS = 1000.0 / 60.0;
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 frame_start = SDL_GetPerformanceCounter();
        while (!skip_menu && g_running &&
               port_menu_state() != PORT_MENU_GAME &&
               port_menu_state() != PORT_MENU_QUIT) {
            port_poll_events();
            /* Clear-then-draw: menu owns the whole framebuffer. */
            if (gl_renderer_enabled()) {
                /* Reuse the normal present path so the GL state stays
                 * consistent. The engine hasn't issued any draws yet, so
                 * the framebuffer is effectively empty besides ImGui. */
                gl_renderer_present();
                port_menu_frame(NULL);
                SDL_GL_SwapWindow(g_window);
            } else {
                SDL_SetRenderDrawColor(g_renderer, 12, 14, 22, 255);
                SDL_RenderClear(g_renderer);
                port_menu_frame(g_renderer);
                SDL_RenderPresent(g_renderer);
            }
            /* 60fps cap. */
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsed = (double)(now - frame_start) * 1000.0 / (double)freq;
            if (FRAME_MS - elapsed > 1.0)
                SDL_Delay((Uint32)(FRAME_MS - elapsed));
            frame_start = SDL_GetPerformanceCounter();
        }
        s_in_menu = false;
        if (port_menu_state() == PORT_MENU_QUIT) g_running = false;
    }

    /* If the user quit from the menu, skip the game entirely. Still save
     * — they may have changed settings in Options/Controls before quitting. */
    if (!g_running) {
        port_config_save(PORT_CONFIG_DEFAULT_PATH);
        imgui_shutdown();
        port_shutdown();
        return 0;
    }

    /* ------- Game initialization (deferred until after menu) ------- */
    game_init();

    printf("port: entering main loop\n");
    {
        /* Display runs at 60fps, game logic + sound at 30fps.
           Input and rendering happen every frame; game_tick runs every other. */
        const double FRAME_TIME_MS = 1000.0 / 60.0;  /* 16.67ms per frame */
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 frame_start = SDL_GetPerformanceCounter();
        int frame_counter = 0;

        while (g_running)
        {
            port_poll_events();
            port_update_pad();

            /* Game logic + sound at 30fps (every other frame) */
            if ((frame_counter & 1) == 0) {
                game_tick();
                port_autoload_tick();
            }

            /* Rendering at 60fps (every frame) */
            port_render();
            TEST_HARNESS_tick();

            frame_counter++;

            /* Frame limiter: sleep until next 60fps boundary */
            {
                Uint64 frame_end = SDL_GetPerformanceCounter();
                double elapsed_ms = (double)(frame_end - frame_start) * 1000.0 / (double)freq;
                double remaining = FRAME_TIME_MS - elapsed_ms;
                if (remaining > 1.0)
                    SDL_Delay((Uint32)(remaining));
                frame_start = SDL_GetPerformanceCounter();
            }
        }
    }

    /* Auto-save the config on graceful exit so any settings the user
     * tweaked via the in-game ImGui debug pane (widescreen, scale, etc.)
     * stick across launches. Cheap — one fopen+fputs. */
    port_config_save(PORT_CONFIG_DEFAULT_PATH);

    imgui_shutdown();
    port_shutdown();
    return 0;
}
