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
    while (g_running)
    {
        port_poll_events();
        port_update_pad();
        game_tick();
        port_render();
    }

    imgui_shutdown();
    port_shutdown();
    return 0;
}
