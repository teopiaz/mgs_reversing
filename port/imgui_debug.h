#ifndef IMGUI_DEBUG_H
#define IMGUI_DEBUG_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void imgui_init(SDL_Window *window, SDL_Renderer *renderer);
void imgui_shutdown(void);
void imgui_process_event(SDL_Event *event);
void imgui_render(SDL_Renderer *renderer);
void imgui_toggle_actors(void);

#ifdef __cplusplus
}
#endif

#endif
