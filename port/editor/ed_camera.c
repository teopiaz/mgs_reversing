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
    g_cam.mode = ED_CAM_MODE_FLY;
    g_cam.orbit_target[0] = 0.0f;
    g_cam.orbit_target[1] = 0.0f;
    g_cam.orbit_target[2] = 0.0f;
    g_cam.orbit_dist = 5000.0f;
}

void ed_camera_first_person(void)
{
    /* Player-eye preset: ~1500 units above ground (PSX -Y is up), looking
       level forward (+Z). Useful for sanity-checking what the player sees. */
    g_cam.pos[0] = 0.0f;
    g_cam.pos[1] = -1500.0f;
    g_cam.pos[2] = -3000.0f;
    g_cam.yaw    = 0.0f;
    g_cam.pitch  = 0.0f;
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
    int alt_held = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];

    /* Edge-detect "fly mode + Alt+RMB" drag start so we can plant the orbit
     * target one orbit_dist step in front of the camera. Without this, the
     * very first Alt+RMB drag would snap the camera to whatever stale pivot
     * was left over from a prior orbit/F-frame (or to world origin on a
     * fresh launch). The flag re-arms when the drag ends. */
    static int s_alt_rmb_active = 0;
    if (rmb_held && alt_held && g_cam.mode == ED_CAM_MODE_FLY && !s_alt_rmb_active) {
        if (g_cam.orbit_dist < 100.0f) g_cam.orbit_dist = 5000.0f;
        float fwd[3], right[3], down[3];
        cam_basis(fwd, right, down);
        g_cam.orbit_target[0] = g_cam.pos[0] + fwd[0] * g_cam.orbit_dist;
        g_cam.orbit_target[1] = g_cam.pos[1] + fwd[1] * g_cam.orbit_dist;
        g_cam.orbit_target[2] = g_cam.pos[2] + fwd[2] * g_cam.orbit_dist;
    }
    s_alt_rmb_active = (rmb_held && alt_held) ? 1 : 0;

    if (rmb_held) {
        const float sens = 0.0035f;
        g_cam.yaw   += (float)mouse_dx * sens;
        g_cam.pitch += (float)mouse_dy * sens;
        if (g_cam.pitch >  1.55f) g_cam.pitch =  1.55f;
        if (g_cam.pitch < -1.55f) g_cam.pitch = -1.55f;
        /* Orbit mode (or Alt-held in fly mode): RMB-drag rotates around the
         * orbit target — re-derive pos from the new yaw/pitch each frame. */
        if (g_cam.mode == ED_CAM_MODE_ORBIT || alt_held) {
            float fwd[3], right[3], down[3];
            cam_basis(fwd, right, down);
            g_cam.pos[0] = g_cam.orbit_target[0] - fwd[0] * g_cam.orbit_dist;
            g_cam.pos[1] = g_cam.orbit_target[1] - fwd[1] * g_cam.orbit_dist;
            g_cam.pos[2] = g_cam.orbit_target[2] - fwd[2] * g_cam.orbit_dist;
        }
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

    float dx = move[0] * speed * dt;
    float dy = move[1] * speed * dt;
    float dz = move[2] * speed * dt;
    g_cam.pos[0] += dx;
    g_cam.pos[1] += dy;
    g_cam.pos[2] += dz;
    /* In orbit mode, dragging the camera with WASD/QE drags the pivot too
     * so the orbit relationship is preserved (camera stays at orbit_dist
     * from target along the current view). Without this, WASD would push
     * the camera into/away from a stationary target — confusing. */
    if (g_cam.mode == ED_CAM_MODE_ORBIT) {
        g_cam.orbit_target[0] += dx;
        g_cam.orbit_target[1] += dy;
        g_cam.orbit_target[2] += dz;
    }
}

void ed_camera_set_mode(int mode)
{
    if (mode == ED_CAM_MODE_ORBIT && g_cam.mode != ED_CAM_MODE_ORBIT) {
        /* Plant orbit_target one orbit_dist step in front of the camera so
         * the user keeps the same view; subsequent rotation pivots around it. */
        if (g_cam.orbit_dist < 100.0f) g_cam.orbit_dist = 5000.0f;
        float fwd[3], right[3], down[3];
        cam_basis(fwd, right, down);
        g_cam.orbit_target[0] = g_cam.pos[0] + fwd[0] * g_cam.orbit_dist;
        g_cam.orbit_target[1] = g_cam.pos[1] + fwd[1] * g_cam.orbit_dist;
        g_cam.orbit_target[2] = g_cam.pos[2] + fwd[2] * g_cam.orbit_dist;
    }
    g_cam.mode = (mode == ED_CAM_MODE_ORBIT) ? ED_CAM_MODE_ORBIT : ED_CAM_MODE_FLY;
}

void ed_camera_orbit(int mouse_dx, int mouse_dy)
{
    /* Same sensitivity as the look-around RMB-drag. Caller is responsible
     * for ensuring an orbit target exists (set_mode + frame_aabb both do). */
    const float sens = 0.0035f;
    g_cam.yaw   += (float)mouse_dx * sens;
    g_cam.pitch += (float)mouse_dy * sens;
    if (g_cam.pitch >  1.55f) g_cam.pitch =  1.55f;
    if (g_cam.pitch < -1.55f) g_cam.pitch = -1.55f;
    float fwd[3], right[3], down[3];
    cam_basis(fwd, right, down);
    g_cam.pos[0] = g_cam.orbit_target[0] - fwd[0] * g_cam.orbit_dist;
    g_cam.pos[1] = g_cam.orbit_target[1] - fwd[1] * g_cam.orbit_dist;
    g_cam.pos[2] = g_cam.orbit_target[2] - fwd[2] * g_cam.orbit_dist;
}

void ed_camera_zoom(float wheel_steps)
{
    if (g_cam.mode == ED_CAM_MODE_ORBIT) {
        /* 15% per notch, clamp to keep the camera from collapsing onto the
         * pivot or shooting off into space. */
        float factor = powf(0.85f, wheel_steps);
        g_cam.orbit_dist *= factor;
        if (g_cam.orbit_dist < 100.0f)    g_cam.orbit_dist = 100.0f;
        if (g_cam.orbit_dist > 200000.0f) g_cam.orbit_dist = 200000.0f;
        float fwd[3], right[3], down[3];
        cam_basis(fwd, right, down);
        g_cam.pos[0] = g_cam.orbit_target[0] - fwd[0] * g_cam.orbit_dist;
        g_cam.pos[1] = g_cam.orbit_target[1] - fwd[1] * g_cam.orbit_dist;
        g_cam.pos[2] = g_cam.orbit_target[2] - fwd[2] * g_cam.orbit_dist;
    } else {
        /* Fly mode: dolly along the view direction. ~1500 units per notch
         * (a few player heights) feels brisk without overshooting. */
        float fwd[3], right[3], down[3];
        cam_basis(fwd, right, down);
        const float step = 1500.0f * wheel_steps;
        g_cam.pos[0] += fwd[0] * step;
        g_cam.pos[1] += fwd[1] * step;
        g_cam.pos[2] += fwd[2] * step;
    }
}

/* Reframe so the AABB fits comfortably inside the perspective view.
 *
 * The renderer projects with PSX H = ~300 for the in-game default, mapped
 * through (eye.x * H / 160) and (eye.y * H / 112) to NDC. The smallest
 * matching half-FOV is the vertical one (atan(112/H)). To fit a sphere of
 * radius `r` inside the view, the camera must sit at  r / sin(half_fov)
 * along its forward axis. We use the AABB's half-diagonal as `r` so the
 * worst-case orientation still fits, and add a 20% margin. */
void ed_camera_frame_aabb(const float bmin[3], const float bmax[3])
{
    float center[3] = { (bmin[0] + bmax[0]) * 0.5f,
                        (bmin[1] + bmax[1]) * 0.5f,
                        (bmin[2] + bmax[2]) * 0.5f };
    float ext[3]    = { (bmax[0] - bmin[0]) * 0.5f,
                        (bmax[1] - bmin[1]) * 0.5f,
                        (bmax[2] - bmin[2]) * 0.5f };
    float radius = sqrtf(ext[0]*ext[0] + ext[1]*ext[1] + ext[2]*ext[2]);
    if (radius < 200.0f) radius = 200.0f;

    float h = 300.0f * g_cam.fov_scale;
    if (h < 1.0f) h = 1.0f;
    float half_fov_y = atan2f(112.0f, h);
    float dist = radius / sinf(half_fov_y) * 1.2f;
    if (dist < 200.0f)    dist = 200.0f;
    if (dist > 200000.0f) dist = 200000.0f;

    g_cam.orbit_target[0] = center[0];
    g_cam.orbit_target[1] = center[1];
    g_cam.orbit_target[2] = center[2];
    g_cam.orbit_dist      = dist;

    /* Plant the camera looking at the AABB center along the current view
     * direction. Both fly and orbit modes benefit — fly mode then continues
     * with WASD as usual; orbit mode pivots around the AABB centroid. */
    float fwd[3], right[3], down[3];
    cam_basis(fwd, right, down);
    g_cam.pos[0] = center[0] - fwd[0] * dist;
    g_cam.pos[1] = center[1] - fwd[1] * dist;
    g_cam.pos[2] = center[2] - fwd[2] * dist;
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

/* ---------------------------------------------------------------------------
 * Hammer-style ortho cameras: Top / Front / Side panes look down a fixed
 * world axis and let the user pan/zoom in the locked plane. Each pane has
 * its own camera state — independent of the perspective camera g_cam.
 * ------------------------------------------------------------------------- */

EdOrthoCam g_top_cam;
EdOrthoCam g_front_cam;
EdOrthoCam g_side_cam;

void ed_camera_ortho_default(EdOrthoCam *cam, EdOrthoAxis axis)
{
    cam->axis = axis;
    cam->center[0] = 0.0f;
    cam->center[1] = 0.0f;
    cam->center[2] = 0.0f;
    cam->half_size = 12000.0f;  /* fits a typical stage at ±10000 */
}

void ed_camera_ortho_pan(EdOrthoCam *cam, int viewport_w, int viewport_h,
                         int pixel_dx, int pixel_dy)
{
    if (viewport_w <= 0 || viewport_h <= 0) return;
    /* World units per screen pixel, anchored on the smaller dimension so the
     * scene fits the same size in both axes. */
    int min_dim = (viewport_w < viewport_h) ? viewport_w : viewport_h;
    float ppu = (cam->half_size * 2.0f) / (float)min_dim;
    float dx = (float)pixel_dx * ppu;
    float dy = (float)pixel_dy * ppu;

    /* Pan within the locked plane: drag-the-world feel — world contents
     * move the same direction as the cursor. Front/Side flip the Y term
     * because their eye-Y is -world Y (so screen +Y down corresponds to
     * world +Y up in user's Blender convention; cursor down means camera
     * focus moves UP in world Y). */
    switch (cam->axis) {
    case ED_ORTHO_TOP:    cam->center[0] -= dx; cam->center[2] += dy; break;
    case ED_ORTHO_FRONT:  cam->center[0] -= dx; cam->center[1] += dy; break;
    case ED_ORTHO_SIDE:   cam->center[2] -= dx; cam->center[1] += dy; break;
    }
}

void ed_camera_ortho_zoom(EdOrthoCam *cam, float wheel_steps)
{
    /* Each wheel notch zooms ~15% in/out. Clamp to avoid degenerate sizes. */
    float factor = powf(0.85f, wheel_steps);
    cam->half_size *= factor;
    if (cam->half_size < 50.0f)     cam->half_size = 50.0f;
    if (cam->half_size > 200000.0f) cam->half_size = 200000.0f;
}

/* Map an ortho cam to (l, r, b, t) bounds in eye space. The eye_inv we
 * build per pane is hand-crafted so eye-X = screen-X axis, eye-Y =
 * screen-Y axis, eye-Z = depth (along the locked axis). */
void ed_camera_ortho_compute(EdOrthoCam *cam, int viewport_w, int viewport_h,
                             float *out_lrbt)
{
    if (viewport_w <= 0) viewport_w = 1;
    if (viewport_h <= 0) viewport_h = 1;
    /* Aspect-correct extents around (0,0) in eye space, anchored on the
     * smaller dim so half_size always means "what fits along the short axis". */
    float aspect = (float)viewport_w / (float)viewport_h;
    float hx, hy;
    if (aspect >= 1.0f) { hy = cam->half_size; hx = hy * aspect; }
    else                { hx = cam->half_size; hy = hx / aspect; }
    out_lrbt[0] = -hx;  /* l */
    out_lrbt[1] =  hx;  /* r */
    out_lrbt[2] = -hy;  /* b */
    out_lrbt[3] =  hy;  /* t */
}

/* Build the eye_inv matrix for an ortho cam. Layout per axis:
 *   Top  : eye-X ←  world X,  eye-Y ←  world Z,  eye-Z ← world Y
 *   Front: eye-X ←  world X,  eye-Y ← -world Y,  eye-Z ← world Z
 *   Side : eye-X ← -world Z,  eye-Y ← -world Y,  eye-Z ← world X
 * The negated eye-Y for Front/Side is what flips Blender +Y-up content
 * to be right-side-up on screen — the GL ortho shader path applies a
 * second -ny flip to match the perspective path's PSX +Y-down screen
 * convention, so world Y_high needs to land at eye Y_low to come out
 * at the top of the rendered pane. (Top doesn't have this issue
 * because its eye-Y is sourced from world Z, not Y.) The Side pane
 * also negates eye-X so the cube's +X direction faces RIGHT on screen
 * when looking down +X. The MATRIX `m` rows are 4.12 fixed-point
 * (×4096); `t` is the camera translation that re-centers the world
 * point cam->center at eye-space origin. */
void ed_camera_ortho_build_eye_inv(EdOrthoCam *cam, MATRIX *out)
{
    short m[3][3] = {{0}};
    int t[3] = {0};
    int K = 4096;
    switch (cam->axis) {
    case ED_ORTHO_TOP:
        /* eye-X = world X, eye-Y = world Z, eye-Z = world Y */
        m[0][0] = K; m[1][2] = K; m[2][1] = K;
        t[0] = -(int)cam->center[0];
        t[1] = -(int)cam->center[2];
        t[2] = -(int)cam->center[1];
        break;
    case ED_ORTHO_FRONT:
        /* eye-X = world X, eye-Y = -world Y, eye-Z = world Z */
        m[0][0] = K; m[1][1] = -K; m[2][2] = K;
        t[0] = -(int)cam->center[0];
        t[1] =  (int)cam->center[1];   /* sign flipped to match -K */
        t[2] = -(int)cam->center[2];
        break;
    case ED_ORTHO_SIDE:
        /* eye-X = -world Z, eye-Y = -world Y, eye-Z = world X */
        m[0][2] = -K; m[1][1] = -K; m[2][0] = K;
        t[0] =  (int)cam->center[2];
        t[1] =  (int)cam->center[1];
        t[2] = -(int)cam->center[0];
        break;
    }
    out->m[0][0] = m[0][0]; out->m[0][1] = m[0][1]; out->m[0][2] = m[0][2];
    out->m[1][0] = m[1][0]; out->m[1][1] = m[1][1]; out->m[1][2] = m[1][2];
    out->m[2][0] = m[2][0]; out->m[2][1] = m[2][1]; out->m[2][2] = m[2][2];
    out->t[0] = t[0]; out->t[1] = t[1]; out->t[2] = t[2];
}

/* Reframe an ortho cam to fit the AABB in its plane. The two planar axes
 * differ per pane; the third (depth) doesn't affect what's visible because
 * the ortho projection ignores it for screen extents. */
void ed_camera_ortho_frame_aabb(EdOrthoCam *cam,
                                const float bmin[3], const float bmax[3])
{
    cam->center[0] = (bmin[0] + bmax[0]) * 0.5f;
    cam->center[1] = (bmin[1] + bmax[1]) * 0.5f;
    cam->center[2] = (bmin[2] + bmax[2]) * 0.5f;
    float ex = (bmax[0] - bmin[0]) * 0.5f;
    float ey = (bmax[1] - bmin[1]) * 0.5f;
    float ez = (bmax[2] - bmin[2]) * 0.5f;
    float h;
    switch (cam->axis) {
    case ED_ORTHO_TOP:   h = (ex > ez ? ex : ez); break; /* world XZ plane */
    case ED_ORTHO_FRONT: h = (ex > ey ? ex : ey); break; /* world XY */
    case ED_ORTHO_SIDE:  h = (ez > ey ? ez : ey); break; /* world YZ */
    default:             h = 5000.0f;
    }
    if (h < 200.0f)    h = 200.0f;
    cam->half_size = h * 1.1f;            /* 10% margin */
    if (cam->half_size > 200000.0f) cam->half_size = 200000.0f;
}
