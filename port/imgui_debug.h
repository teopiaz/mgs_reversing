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
void imgui_toggle_debug(void);  /* new unified hotkey (F1) */

/* Camera override — set by imgui, read by port_RenderObjects */
extern int imgui_cam_override;
extern int imgui_cam_eye_inv_t[3];
extern int imgui_cam_clip_dist;
extern short imgui_cam_eye_inv_m[3][3];

/* Lighting mode toggle (0 = Gouraud, 1 = per-pixel PSX NCS).
 * Read at face-submission time in port_RenderChanl. */
extern int imgui_per_pixel_light;

#ifdef __cplusplus
}
#endif

#endif
