#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <SDL.h>
#include "imgui_debug.h"

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
#define WINDOW_SCALE  3

static SDL_Window   *g_window;
static SDL_Renderer *g_renderer;
static bool          g_running;

static int port_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    g_window = SDL_CreateWindow(
        "Metal Gear Solid",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);

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
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }

    SDL_RenderSetLogicalSize(g_renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

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
    game_init();

    printf("port: entering main loop\n");

    extern void port_update_pad(void);
    extern void port_open_controller(void);
    port_open_controller();

    g_running = true;
    {
        Uint64 frame_count = 0;
        Uint64 t_poll = 0, t_pad = 0, t_tick = 0, t_render = 0;
        Uint64 freq = SDL_GetPerformanceFrequency();
        int perf_enabled = 0;

        while (g_running)
        {
            Uint64 t0 = SDL_GetPerformanceCounter();
            port_poll_events();
            Uint64 t1 = SDL_GetPerformanceCounter();
            port_update_pad();
            Uint64 t2 = SDL_GetPerformanceCounter();
            game_tick();
            Uint64 t3 = SDL_GetPerformanceCounter();
            port_render();
            Uint64 t4 = SDL_GetPerformanceCounter();

            /* Start profiling once a gameplay stage with 3D objects is loaded */
            {
                extern int port_get_objs_count(void);
                int oc = port_get_objs_count();
                if (!perf_enabled && oc > 5) {
                    perf_enabled = 1;
                    frame_count = 0;
                    t_poll = t_pad = t_tick = t_render = 0;
                    printf("[perf] profiling started (objs=%d)\n", oc);
                }
            }

            if (perf_enabled)
            {
                t_poll += t1 - t0;
                t_pad += t2 - t1;
                t_tick += t3 - t2;
                t_render += t4 - t3;
                frame_count++;

                if (frame_count % 60 == 0)
                {
                    double ms_poll = (double)t_poll * 1000.0 / (double)freq / 60.0;
                    double ms_pad = (double)t_pad * 1000.0 / (double)freq / 60.0;
                    double ms_tick = (double)t_tick * 1000.0 / (double)freq / 60.0;
                    double ms_render = (double)t_render * 1000.0 / (double)freq / 60.0;
                    printf("[perf] poll=%.2fms pad=%.2fms tick=%.2fms render=%.2fms total=%.2fms (%.1f fps)\n",
                           ms_poll, ms_pad, ms_tick, ms_render,
                           ms_poll + ms_pad + ms_tick + ms_render,
                           1000.0 / (ms_poll + ms_pad + ms_tick + ms_render));
                    t_poll = t_pad = t_tick = t_render = 0;
                }
            }
        }
    }

    imgui_shutdown();
    port_shutdown();
    return 0;
}
