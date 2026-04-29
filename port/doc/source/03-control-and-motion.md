# Control and motion — `control.c` + `motion.c`

Every spatial actor in MGS — Snake, every guard, every door, every
DEMODOLL — wraps a `CONTROL` struct that owns its **position,
orientation, and per-frame collision integration**. Skeletal
characters layer a `MOTION_CONTROL` on top to play back
animation-segment data.

This doc covers both — they're tightly coupled (one writes pose,
the other writes step) and ticked together once per frame from the
actor's `Act`.

Sources:
[`source/game/control.c`](../../../source/game/control.c) +
[`control.h`](../../../source/game/control.h),
[`source/game/motion.c`](../../../source/game/motion.c) +
[`motion.h`](../../../source/game/motion.h),
[`source/game/object.c`](../../../source/game/object.c).

## CONTROL — the spatial state struct

```c
typedef struct CONTROL
{
    SVECTOR     mov;          // current position (PSX world units)
    SVECTOR     rot;          // current orientation (4096 = 2π)
    HZD_EVT     event;        // bound HZD trap event
    MAP        *map;          // current map / region
    u_short     name;         // GV_StrCode of the actor's script id
    short       height;       // body half-height (offset to mov.vy)
    short       hzd_height;   // -32767 = no floor, else last-known floor Y
    short       step_size;    // collision radius² — 0 disables collision
    short       field_38;     // unused (echoes step_size)
    u_short     radar_atr;    // bitmask: visibility / vision-cone / noise
    RADAR_CONE  radar_cone;   // dir/len/ang for vision cones
    SVECTOR     step;         // movement vector this tick
    SVECTOR     turn;         // rotation target
    signed char interp;       // turn-rate (frames to interp turn → rot)
    char        skip_flag;    // CTRL_SKIP_MESSAGE / TRAP / NEAR_CHECK / BOTH_CHECK
    signed char n_messages;   // pending messages this frame
    signed char level_flag;   // 1 = below floor; 2 = above ceiling
    signed char touch_flag;   // 1 = collided this tick
    char        exclude_flag; // surface-flag mask to ignore (e.g. excluding
                              // certain walls during a cutscene)
    char        nearflags[2]; // last 2 nearby surface flags
    GV_MSG     *messages;     // ring of incoming GV_SendMessage data
    SVECTOR     nearvecs[2];  // closest 2 surface points
    void       *nears[2];     // HZD_SEG (wall) or HZD_FLR (floor) ptrs
    short       levels[2];    // floor / ceiling Y at this position
} CONTROL;

#define MAX_CONTROLS 96
```

The whole struct lives inside the actor's `Work` struct (e.g.
`SnaInitWork.control` for Snake, `WatcherWork.control` for guards).
Spatial actors don't malloc their CONTROL — it lives inline.

A global registry tracks all live CONTROLs:

```c
extern CONTROL *gControls[MAX_CONTROLS];        // GM_ControlPushBack
extern int      gControlCount_800AB9B4;
```

`GM_ControlPushBack(c)` adds; `GM_ControlRemove(c)` deletes. The
registry is what `GM_FindControl(name)` searches by `control->name`
to resolve `mesg $s:NAME` directives back to actors.

## Lifecycle

### Init: `GM_InitControl(control, scriptData, scriptBinds)`

[`control.c:83`](../../../source/game/control.c#L83). Zeroes the
struct, then sets sensible defaults:

```c
control->height       = 850;     // ~85cm body half-height
control->hzd_height   = -32767;  // "no floor known yet"
control->field_38     = 450;
control->step_size    = 450;     // 21-unit collision radius (450 = 21²)
control->exclude_flag = 2;
control->skip_flag    = CTRL_SKIP_TRAP;
control->levels[0]    = -32000;  // floor sentinel
control->levels[1]    =  32000;  // ceiling sentinel
```

`scriptData` is the actor's `name` (GV_StrCode hash); `scriptBinds`
is an optional map override (when 0, uses `GM_CurrentMap`). The
function pushes the CONTROL into the global registry and binds an
HZD event handler via `HZD_SetEvent`.

Returns `-1` on failure (no map for the supplied id) or 0 on success.

### Per-frame: `GM_ActControl(control)`

[`control.c:329`](../../../source/game/control.c#L329). The heart of
character motion. Called once per frame from the actor's `Act`,
after the actor has decided where it wants to move (`step.vx/vz`
populated).

```c
void GM_ActControl(CONTROL *control)
{
    HZD_HDL *hzd = control->map->hzd;
    int      vy, time;

    CheckMessage(control);                    // 1. drain pending mesgs
    GM_CurrentMap = control->map->index;

    if (control->step_size > 0) {
        control->touch_flag = 0;

        if (control->hzd_height != -0x7fff) { // 2. wall collision
            vy = control->mov.vy;
            control->mov.vy = control->hzd_height;
        }
        CheckCollide(control, hzd);

        control->mov.vx += control->step.vx;  // 3. integrate step
        control->mov.vz += control->step.vz;

        CheckNear(control, hzd);              // 4. proximity probe

        if (control->hzd_height != -0x7fff)
            control->mov.vy = vy;

        time = control->interp;               // 5. turn → rot
        if (control->interp == 0)
            GV_NearExp4PV(&control->rot.vx, &control->turn.vx, 3);
        else
            GV_NearTimePV(&control->rot.vx, &control->turn.vx,
                          control->interp, 3);

        CheckHeight(control, hzd);            // 6. floor / ceiling Y
    }
    else if (control->step_size < 0) {
        // …same as above but skip CheckCollide (use only floor query)
    }
    /* step_size == 0 → no movement integration; pos stays put. */

    if (!(control->skip_flag & CTRL_SKIP_TRAP)) {
        control->event.pos     = control->mov;          // 7. fire HZD
        control->event.pos.pad = control->rot.vy;       //    trap events
    }
    /* …trap dispatch… */
}
```

Numbered passes:

1. **Drain messages.** `CheckMessage` walks the actor's `messages[]`
   ring, looking for `HASH_MAP` (relocate to a new map) and
   `HASH_MOVE2` (teleport mov to message payload). Other messages
   are left for the actor's `Act` to consume.
2. **Wall collision.** `CheckCollide` runs a `step_size`-radius
   sweep against `HZD_SEG` walls — clamps step / pushes the actor
   away if they'd overlap a wall. The temporary `mov.vy = hzd_height`
   substitution makes the collision check 2D in the floor plane.
3. **Step integration.** `mov.vx += step.vx` / same for vz. Y is
   handled separately by `CheckHeight`.
4. **Proximity.** `CheckNear` finds the two nearest surface points
   and stores them in `nears[0..1]` + `nearvecs[0..1]` for the
   actor's `Act` to read (e.g. wall-attached movement, cover system).
5. **Turn interpolation.** When `interp == 0`, immediate-but-eased
   exp(-x/4) catch-up; otherwise time-based linear interp over
   `interp` frames. The actor sets `turn.vx/vy/vz` to its desired
   facing; `GM_ActControl` smooths `rot` toward it.
6. **Height query.** `CheckHeight` raycasts down + up to find floor
   and ceiling Y; writes them into `levels[0]/[1]` and updates
   `level_flag` if Snake fell below floor / clipped through ceiling.
7. **Trap fire.** Build an `HZD_EVT` from the new position and call
   the dispatcher. If a trap region matches, its bound script proc
   fires this frame.

`step_size > 0` is the normal "moving entity with collision" mode.
`step_size < 0` skips wall collision but still does floor query —
useful for flying characters (Hind helicopter, drifting smoke).
`step_size == 0` disables all movement integration (cinematic
puppets that get their pos from `DMO_ADJ` frames).

### Free: `GM_FreeControl(control)`

[`control.c:426`](../../../source/game/control.c#L426). Removes from
registry, unbinds HZD event, zeroes the struct. Called from the
actor's `Die` callback.

## Actor patterns

A typical gameplay actor's tick:

```c
static void SnakeAct(SnaInitWork *work)
{
    /* 1. Read input → decide step + turn. */
    sna_HandleInput(work);                  // sets work->control.step,
                                            //                .turn

    /* 2. Run animation player (if skeletal). */
    GM_ActMotion(&work->body);              // GM_PlayAction → updates
                                            // m_ctrl->step from anim data

    /* 3. Integrate motion + collide. */
    GM_ActControl(&work->control);

    /* 4. Submit to render pipeline. */
    DG_SetPos2(&work->control.mov, &work->control.rot);
    GM_ActObject(&work->body);
}
```

The order matters:

- `GM_ActMotion` runs *before* `GM_ActControl` so the animation's
  step (root motion from a walk cycle) lands in `step` before
  collision integrates it. This is how Snake's footsteps add
  forward distance based on the playing animation.
- `DG_SetPos2` must run *after* `GM_ActControl` so the rendered
  matrix reflects the new (post-collision) `mov` and `rot`.

## MOTION_CONTROL — animation playback

Snake, Meryl, every guard — these have multiple body-part KMD
models with a parent-child bone hierarchy. Per-frame, two
animation segments blend: the *primary* (current state) and a
*mask* (e.g. only the upper body for "aim while walking").

```c
typedef struct MOTION_CONTROL {
    OAR              *oar;        // animation library (loaded from .oar)
    MOTION_INFO       info1;      // primary segment + frame pos
    MOTION_INFO       info2;      // mask segment (overrides specific bones)
    int               interp;     // frames to interpolate when changing
    SVECTOR          *rots;       // per-bone rotations (output)
    SVECTOR          *rot;        // → control->rot (root rotation)
    SVECTOR          *step;       // → control->step (root motion delta)
    SVECTOR          *height;     // → control->height
    SVECTOR           waist_rot;  // separate waist twist
} MOTION_CONTROL;
```

The link to CONTROL is by pointer — `m_ctrl->step` *is*
`&control->step`. So when the animation player writes step, it's
writing into the same memory `GM_ActControl` reads.

### Init: `GM_ConfigMotionControl`

[`motion.c:191`](../../../source/game/motion.c#L191). Wires the
motion control to its host object + the underlying control:

```c
m_ctrl->oar         = GV_GetCache(GV_CacheID(name, 'o'));
m_ctrl->height      = &object->height;
m_ctrl->info1.m_segs = m_segs1;
m_ctrl->info2.m_segs = m_segs2;
m_ctrl->info2.mask   = 0xffffffff;       // 1=apply, 0=skip per bone
m_ctrl->rots         = rots;
m_ctrl->rot          = &control->rot;
m_ctrl->step         = &control->step;
object->objs->rots = rots;               // bones[] array seen by render
object->m_ctrl     = m_ctrl;
```

`m_segs1` / `m_segs2` are arrays of `MOTION_SEGMENT` records — one
per available animation. The actor allocates them and passes them
in. Snake's allocation lives at
`work->oars[0..20]` / `work->oars[21..41]` — 21 segments per layer.

### Action change: `GM_ConfigObjectAction`

[`object.c:197`](../../../source/game/object.c#L197). The way an
actor switches animation:

```c
GM_ConfigObjectAction(&snake.body,
                      ANIMATION_SNAKE_RUN,    // action id
                      0,                      // start frame
                      8);                     // interp frames
```

This calls `GM_ConfigAction(m_ctrl, action, frame)` which looks up
the action's `MOTION_SEGMENT` in `oar`, sets up `info1.m_segs` to
play it, then sets `interp = 8` so the rotation/step blend smoothly
from the previous animation over 8 frames.

### Per-frame: `GM_ActMotion` → `GM_PlayAction`

[`object.c:81`](../../../source/game/object.c#L81). The motion
player runs inside `GM_ActMotion`:

```c
void GM_ActMotion(OBJECT *obj) {
    if (obj->m_ctrl) {
        SVECTOR step = *obj->m_ctrl->step;        // snapshot
        GM_ActObjectMotion(obj);                  // run animation player
        GV_AddVec3(&step, obj->m_ctrl->step,      // restore + add
                   obj->m_ctrl->step);
    }
}
```

`GM_ActObjectMotion` calls `GM_PlayAction(m_ctrl)` which advances
the animation's frame pointer, decodes the per-bone rotations into
`m_ctrl->rots[]`, and writes the root step into `m_ctrl->step`.

The save/restore around the call lets the actor accumulate step
contributions from multiple sources — e.g. a walking animation +
a knockback impulse — before `GM_ActControl` integrates them.

### Bone hierarchy

The bone tree is part of the KMD itself (`DG_DEF.model[i].parent`
points at the parent bone, `model[i].pos` is the bone's local
position relative to parent). The render pipeline walks the array
in order assuming parents come before children (which the original
KMD authoring tool guaranteed).

The animation player only writes per-bone Euler triplets; the
matrix composition happens in the render path. See
[doc/demo/04-camera-pipeline.md](../demo/04-camera-pipeline.md) for
a worked example (the editor's DMO inspector does the same compose
to render baked cinematic poses).

## Common pitfalls

### `mov` doesn't update

- **`step.vx == 0`**: the animation isn't writing root motion. Most
  idle animations have zero step; the actor needs to set
  `step.vx/vz` directly from input (Snake's run state) or set a
  walking anim that has root motion (most guard idle→patrol).
- **`step_size == 0`**: collision integration is disabled entirely.
  Cinematic puppets set this to 0 in `CreateDemo` so the engine
  doesn't fight `DMO_ADJ.pos`.

### Actor walks through walls

- **`hzd_height` is `-0x7fff`**: the floor query failed last frame
  (likely outside the map). `CheckCollide` is skipped because the
  Y-substitution can't establish a 2D plane.
- **`exclude_flag` masks the wall away**: rare but happens during
  scripted set pieces (a wall is excluded so Snake can pass through
  during a cinematic). Reset to default `2` for normal collision.
- **`map` doesn't include this wall**: walls are per-map. If the
  control's `map` doesn't reflect the actor's current room,
  `CheckCollide` consults the wrong HZD bucket.

### Animation freezes mid-action

- `m_ctrl->info1.time` reaches 0 — animation finished and didn't
  loop. The actor's `Act` is responsible for queuing the next
  action (`GM_ConfigObjectAction`).
- `obj->is_end == m_ctrl->info1.time` is the convention for
  detecting "animation just finished this frame".

### Cinematic doll appears at camera origin

This was a port-specific bug — `FrameRunDemo`'s non-special branch
(neither `m1e1` nor `hind`) didn't `DG_SetPos2` before
`GM_ActObject`, so the GTE matrix was the camera's, and every
DEMODOLL rendered at camera position. Fixed by adding the
`DG_SetPos2(&model->control.mov, &model->control.rot)` call inside
`#ifdef PORT_BUILD`. See `source/kojo/demo.c::FrameRunDemo`.

## Cross-references

- [02-game-loop.md](02-game-loop.md) — when `GM_ActControl` is
  *called* (every actor's Act inside `GV_ExecActorSystem`).
- [doc/demo/03-key-actors.md](../demo/03-key-actors.md) — DEMODOLL
  uses CONTROL but with `step_size = 0` (puppeted, not collided).
- HZD docs (planned, 13-collision.md) — `CheckCollide`,
  `CheckHeight`, `HZD_GetAddress` internals.
