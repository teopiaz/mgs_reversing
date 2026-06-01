/* Editor stubs.
   The editor links against most port objects except main.o, main_game.o
   and imgui_debug.o. This file provides the small set of symbols those
   excluded files would otherwise supply. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* From imgui_debug.cpp — referenced by libdg_stub.o and libgv code. */
int   imgui_per_pixel_light = 0;
int   imgui_cam_override    = 0;
int   imgui_cam_eye_inv_t[3] = {0};
int   imgui_cam_clip_dist   = 320;
short imgui_cam_eye_inv_m[3][3] = {{0}};

/* libdg_stub.o turns off lighting when this is non-zero — the editor wants
   pure texture sampling (no per-vertex modulation). */
extern int port_force_gouraud_neutral;

/* From main.c */
const char *port_argv0 = NULL;

/* Game-loop debug toggles that imgui_debug.cpp would set. */
int  g_running = 1;

/* port_open_controller, port_update_pad, port_vram_toggle_debug and
   port_apply_deferred_clear are all defined elsewhere in the port objects
   we link (mts.o / vram.o / gpu_stubs.o). The editor doesn't redefine them. */

/* Editor entry sets this to 1 once a stage is loaded so libdg_stub's
   "skip rendering during stage transitions" gate passes. */
int GM_LoadComplete = 0;
