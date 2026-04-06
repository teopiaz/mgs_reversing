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
void imgui_toggle_camera(void);

/* Camera override — set by imgui, read by port_RenderObjects */
extern int imgui_cam_override;
extern int imgui_cam_eye_inv_t[3];
extern int imgui_cam_clip_dist;
extern short imgui_cam_eye_inv_m[3][3];

#ifdef __cplusplus
}
#endif

#endif
