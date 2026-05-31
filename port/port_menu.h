/*
 * port_menu.h — pre-game splash + main menu + options + controls UI.
 *
 * Drives a small state machine that runs BEFORE the PSX game loop starts:
 *   SPLASH (1.5s)  →  MAIN  →  OPTIONS / CONTROLS  →  GAME (exit menu)
 *
 * The caller (main.c) loops port_menu_frame() each tick until
 * port_menu_state() == PORT_MENU_GAME (or the user quits). The menu
 * mutates g_port_config and writes ./port_config.ini on "Save & Apply".
 */
#ifndef PORT_MENU_H
#define PORT_MENU_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_MENU_SPLASH,   /* logo + version, auto-advance after 1.5s */
    PORT_MENU_MAIN,     /* Start / Options / Controls / Exit */
    PORT_MENU_OPTIONS,  /* video + audio */
    PORT_MENU_CONTROLS, /* keyboard + gamepad bindings */
    PORT_MENU_GAME,     /* exit menu, run the game */
    PORT_MENU_QUIT      /* user clicked Exit; main loop should terminate */
} PortMenuState;

/* Reset to initial state (SPLASH). Call once before the menu loop. */
void port_menu_init(void);

/* Render one frame of whichever page is active. Handles the splash
 * auto-advance internally. Renderer may be NULL when GL backend is used. */
void port_menu_frame(SDL_Renderer *renderer);

/* Forward SDL events that may rebind a key in CONTROLS state. Safe to
 * call unconditionally — does nothing when capture is inactive. */
void port_menu_handle_event(const SDL_Event *event);

/* Current state. Caller exits its menu loop when this returns
 * PORT_MENU_GAME or PORT_MENU_QUIT. */
int port_menu_state(void);

#ifdef __cplusplus
}
#endif
#endif /* PORT_MENU_H */
