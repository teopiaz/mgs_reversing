#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <SDL.h>
#include "imgui_debug.h"
#include "test_server.h"

static void crash_handler(int sig)
{
    void *bt[30];
    int n = backtrace(bt, 30);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(1);
}

static void port_install_crash_handler(void)
{
    signal(SIGBUS, crash_handler);
    signal(SIGSEGV, crash_handler);
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 224
#define WINDOW_SCALE  4

static SDL_Window   *g_window;
static SDL_Renderer *g_renderer;
bool                 g_running;  /* non-static: test_server.c may set it false to quit */

static int port_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Bilinear filtering for texture upscaling (smoother than nearest-neighbor) */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    g_window = SDL_CreateWindow(
        "Metal Gear Solid",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);

    if (!g_window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

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

static void port_poll_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        imgui_process_event(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            g_running = false;
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE)
                g_running = false;
            if (event.key.keysym.sym == SDLK_TAB)
            {
                extern void port_vram_toggle_debug(void);
                port_vram_toggle_debug();
            }
            if (event.key.keysym.sym == SDLK_p)
                imgui_toggle_actors();
            if (event.key.keysym.sym == SDLK_F2)
                imgui_toggle_camera();
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
    port_vram_display();
    imgui_render(g_renderer);
    SDL_RenderPresent(g_renderer);

    /* Update window title with FPS every second */
    {
        static Uint32 last_fps_time = 0;
        static int fps_count = 0;
        fps_count++;
        Uint32 now = SDL_GetTicks();
        if (now - last_fps_time >= 1000)
        {
            char title[64];
            snprintf(title, sizeof(title), "Metal Gear Solid — %d fps", fps_count);
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
    (void)argc;
    (void)argv;

    /* Force line-buffered stdout so we can see output before the process ends */
    setvbuf(stdout, NULL, _IOLBF, 0);

    port_install_crash_handler();

    if (port_init() < 0)
    {
        return 1;
    }

    port_vram_init(g_renderer);
    imgui_init(g_window, g_renderer);
    TEST_HARNESS_init();
    game_init();

    printf("port: entering main loop\n");

    extern void port_update_pad(void);
    extern void port_open_controller(void);
    port_open_controller();

    g_running = true;
    {
        /* Frame timing: 30fps cap for both logic and display */
        const double FRAME_TIME_MS = 1000.0 / 60.0;  /* 33.33ms per frame */
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 frame_start = SDL_GetPerformanceCounter();

        while (g_running)
        {
            port_poll_events();
            port_update_pad();
            game_tick();
            port_render();
            TEST_HARNESS_tick();

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

    imgui_shutdown();
    port_shutdown();
    return 0;
}
