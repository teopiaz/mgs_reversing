/* Free-fly editor camera.
   PSX coordinate convention: +X right, +Y down, +Z away from camera.

   We do all math in floats, then build an SVECTOR eye + center pair and let
   DG_LookAt (source/libdg/display.c) bake the actual eye_inv matrix. This
   guarantees we match the convention the in-game camera and renderer use,
   instead of trying to reproduce it (the basis handedness was easy to get
   wrong: see initial mirror / upside-down debugging on this branch). */

#include <math.h>
#include <stdint.h>
#include <SDL.h>

#include "libgte.h"
#include "libdg/libdg.h"
#include "editor.h"

EdCamera g_cam;
extern void DG_LookAt(DG_CHANL *chanl, SVECTOR *eye, SVECTOR *center, int clip_distance);

void ed_camera_default(void)
{
    g_cam.pos[0] = 0.0f;
    g_cam.pos[1] = 20000.0f;
    g_cam.pos[2] = 0.0f;
    g_cam.yaw    = 0.0f;
    g_cam.pitch  = -1.5f;
    g_cam.fov_scale = 1.0f;
}

static void cam_basis(float fwd[3], float right[3], float down[3])
{
    float cy = cosf(g_cam.yaw),   sy = sinf(g_cam.yaw);
    float cp = cosf(g_cam.pitch), sp = sinf(g_cam.pitch);
    /* Forward: looking direction. Yaw rotates around Y, pitch around X. */
    fwd[0] =  sy * cp;
    fwd[1] =  sp;          /* +Y is down — positive pitch tilts camera down */
    fwd[2] =  cy * cp;
    /* Right: yaw-only rotation of (1,0,0). */
    right[0] =  cy;
    right[1] =  0.0f;
    right[2] = -sy;
    /* PSX has +Y down on screen, so the view-matrix row 1 must be the
       camera-DOWN basis, not -UP. down = forward × right. Using right ×
       forward (the right-handed "up") produced a reflection that fed
       inverted winding to the GPU and culled all front-faces — the world
       rendered mirrored. */
    down[0] = fwd[1]*right[2] - fwd[2]*right[1];
    down[1] = fwd[2]*right[0] - fwd[0]*right[2];
    down[2] = fwd[0]*right[1] - fwd[1]*right[0];
}

void ed_camera_update(float dt, int mouse_dx, int mouse_dy, int rmb_held)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 4000.0f;       /* world units / second */
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) speed *= 4.0f;
    if (keys[SDL_SCANCODE_LCTRL]  || keys[SDL_SCANCODE_RCTRL])  speed *= 0.25f;

    if (rmb_held) {
        const float sens = 0.0035f;
        g_cam.yaw   += (float)mouse_dx * sens;
        g_cam.pitch += (float)mouse_dy * sens;
        if (g_cam.pitch >  1.55f) g_cam.pitch =  1.55f;
        if (g_cam.pitch < -1.55f) g_cam.pitch = -1.55f;
    }

    /* Keyboard arrow rotation (no mouse needed) */
    const float rot = 1.5f * dt;
    if (keys[SDL_SCANCODE_LEFT])  g_cam.yaw   -= rot;
    if (keys[SDL_SCANCODE_RIGHT]) g_cam.yaw   += rot;
    if (keys[SDL_SCANCODE_UP])    g_cam.pitch -= rot;
    if (keys[SDL_SCANCODE_DOWN])  g_cam.pitch += rot;

    float fwd[3], right[3], down[3];
    cam_basis(fwd, right, down);

    float move[3] = {0,0,0};
    if (keys[SDL_SCANCODE_W]) { move[0]+=fwd[0];   move[1]+=fwd[1];   move[2]+=fwd[2]; }
    if (keys[SDL_SCANCODE_S]) { move[0]-=fwd[0];   move[1]-=fwd[1];   move[2]-=fwd[2]; }
    if (keys[SDL_SCANCODE_A]) { move[0]-=right[0]; move[1]-=right[1]; move[2]-=right[2]; }
    if (keys[SDL_SCANCODE_D]) { move[0]+=right[0]; move[1]+=right[1]; move[2]+=right[2]; }
    /* +Y is down: Q goes up (negative Y), E goes down. */
    if (keys[SDL_SCANCODE_Q]) { move[1] -= 1.0f; }
    if (keys[SDL_SCANCODE_E]) { move[1] += 1.0f; }

    g_cam.pos[0] += move[0] * speed * dt;
    g_cam.pos[1] += move[1] * speed * dt;
    g_cam.pos[2] += move[2] * speed * dt;
}

void ed_camera_focus(int wx, int wy, int wz, float distance)
{
    float fwd[3], right[3], down[3];
    cam_basis(fwd, right, down);
    g_cam.pos[0] = (float)wx - fwd[0] * distance;
    g_cam.pos[1] = (float)wy - fwd[1] * distance;
    g_cam.pos[2] = (float)wz - fwd[2] * distance;
}

/* Build eye_inv via DG_LookAt: feed it a camera position and a center one
   forward step ahead, mirroring what main_game.c update_camera does for the
   in-game free-cam. The renderer convention (handedness, +Y-down screen)
   then matches the existing pipeline by construction. */
void ed_camera_build_eye_inv(DG_CHANL *chanl)
{
    float fwd[3], right[3], down[3];
    cam_basis(fwd, right, down);

    SVECTOR eye = {
        (short)g_cam.pos[0],
        (short)g_cam.pos[1],
        (short)g_cam.pos[2],
        0,
    };
    SVECTOR center = {
        (short)(g_cam.pos[0] + fwd[0] * 1000.0f),
        (short)(g_cam.pos[1] + fwd[1] * 1000.0f),
        (short)(g_cam.pos[2] + fwd[2] * 1000.0f),
        0,
    };

    /* PSX H register: smaller → wider FOV. ~300 matches the in-game default
       and what main_game.c update_camera passes. */
    int h = (int)(300.0f * g_cam.fov_scale);
    if (h < 64)  h = 64;
    if (h > 2000) h = 2000;

    DG_LookAt(chanl, &eye, &center, h);
}
