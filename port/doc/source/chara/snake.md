# `chara/snake/` — the player character

By far the largest single component in the codebase. Snake's
init + per-tick logic lives in **8684 lines of `sna_init.c`** —
more than the entire `animal/doll/` subsystem and four times the
typical guard. He's the player, so he gets every state every
other character has, plus several Snake-only mechanics
(crouch / sneak / tap-wall / cigarette / box).

## File map

| File | Lines | Role |
| ---- | ----- | ---- |
| [`sna_init.h`](../../../../source/chara/snake/sna_init.h) | 600+ | `SnaInitWork` (~5 KiB), `ACTPACK`, all `SnaFlag*` enums, every `ACT*` sub-struct (still / move / damage / attack / special). |
| [`sna_init.c`](../../../../source/chara/snake/sna_init.c) | 8684 | Master factory + every action callback + collision integration + animation dispatch. The whole player-character runtime. |
| [`snake.c`](../../../../source/chara/snake/snake.c) | 469 | Cross-cutting helpers — `GM_PlayerStatus` flag setters, animation interp helpers, several `sna_*` wrappers. |
| [`shadow.c`](../../../../source/chara/snake/shadow.c) | 252 | Drop-shadow actor (`NewShadow`) — a small flat polygon under any character. Used by Snake, Meryl, every guard. |
| [`afterse.c`](../../../../source/chara/snake/afterse.c) | ?? | Footstep / surface-effect dispatch — emits `AN_Mark_*` puffs on each footstep, picks sound based on surface. |
| [`breath.c`](../../../../source/chara/snake/breath.c) | ?? | Breath effect for Snake (calls `AN_Breath`). |
| [`sna_hzd.c`](../../../../source/chara/snake/sna_hzd.c) | 91 | HZD-specific helpers for Snake's collision queries. |

Total: ~9500 lines across the snake/ folder.

## SnaInitWork — the player struct

The biggest non-cinematic actor struct in the codebase. Conceptual
groupings:

```c
typedef struct SnaInitWork {
    /* --- Engine actor base --- */
    GV_ACT          actor;
    CONTROL         control;          /* pos, rot, step, target, msgs */
    OBJECT          body;             /* "snake" KMD + bone hierarchy */
    MOTION_CONTROL  m_ctrl;           /* anim player */
    MOTION_SEGMENT  m_segs1[N];       /* primary animation segments */
    MOTION_SEGMENT  m_segs2[N];       /* mask segments (e.g. upper-body
                                         override during aim-while-walking) */
    SVECTOR         rots[32];         /* per-bone rotations */

    OBJECT          weapon;           /* current weapon KMD (handheld) */
    GV_ACT         *subweapon;        /* optional secondary weapon */
    OBJECT          subbody;          /* misc attached object — box, flag */

    /* --- Lighting + shadow --- */
    MATRIX          field_848_lighting_mtx[2];
    GV_ACT         *shadow;
    int            *shadow_enable;
    GV_ACT         *glight;           /* gun-tip muzzle light */
    int            *glight_enable;

    /* --- Action state machine --- */
    void           *action;           /* CURRENT ACTION FN PTR */
    void           *action2;          /* OVERRIDE / OFFHAND */
    int             time;             /* primary frame counter */
    int             time2;             /* override counter */
    int             actend;
    ACTPACK        *act_pack;          /* table of action ids per category */

    /* --- Pad input mirror --- */
    int             pad_press;
    int             pad_status;
    int             pad_release;
    int             trigger;          /* fire-button bits */

    /* --- Health / ration / HP --- */
    int             life;
    int             max_life;
    int             stamina;
    int             oxygen;           /* underwater / gas-room */

    /* --- Weapon state --- */
    int             current_weapon;
    int             ammo[N_WEAPONS];

    /* --- Targeting --- */
    TARGET         *target;            /* hit-target — bullets damage Snake here */
    HOMING         *hom;               /* aim assist */
    int             field_89C_pTarget; /* TARGET ptr for what Snake's currently
                                          aiming at */
    Target_Data     field_8F4;
    Target_Data     field_8FC;

    /* --- Snake-specific flags --- */
    int             flags1;            /* SNA_FLAG1_* — knockdown, dead, …  */
    int             flags2;            /* SNA_FLAG2_* — sneak, crouch, … */
    int             status_flags;      /* PLAYER_DEBUG / PLAYER_BEHIND_CAM, …
                                          via GM_(Set/Clear/Check)PlayerStatusFlag */

    /* --- Movement / collision --- */
    int             height_floor;
    int             height_ceil;
    int             contact_wall_r;
    int             contact_wall_l;

    /* --- Equipment / item bookkeeping --- */
    int             current_item;
    int             box_type;          /* if equipped, which box variant */

    /* --- Camera link --- */
    short           camera_zone_id;
    short           camera_first_person;

    /* --- Lots more state — patrol-route bookkeeping for cinematic mode,
           tap-wall countdown, jpeg-cam, equipment-menu cursor, … --- */
    /* Total: ~5 KiB of state. */
    int             field_950;
    int             field_A22_snake_current_health;
    /* …many more. */
} SnaInitWork;
```

The struct uses recovered names where the team's symbols were
visible. Many `field_XXX` placeholders remain — see
[_unreversed.md](_unreversed.md).

## ACTPACK — the action-id dispatch table

Each action category has its own sub-struct giving the animation
ids to play in that state:

```c
typedef struct ACTPACK {
    ACTSTILL   *still;     /* stand / squat / crouch / setup / against-wall */
    ACTMOVE    *move;      /* walk / run / setup-aim / aim-strafe */
    ACTTRANS   *trans;     /* transitions (stand→squat, squat→crouch) */
    ACTDAMAGE  *damage;    /* hit-stagger flavours */
    ACTATTACK  *attack;    /* aim / shoot / reload / punch */
    Sna_E6     *special1;  /* per-stage specials (rappel, ladder) */
    ACTSPECIAL *special2;  /* more specials */
} ACTPACK;
```

`SnaInitWork.act_pack` points at one of these per stage — Snake's
animation set differs slightly per stage (e.g. snowy stages have
shivering-stand; underwater has swim). Each `ACTSTILL.stand` is a
single byte = animation id passed to `GM_ConfigObjectAction`.

## Action state machine

Same pattern as Meryl72 / zako11e — a function pointer
`work->action` runs each tick:

```c
static void Act(SnaInitWork *work)
{
    if (HASH_KILL received) destroy + return;

    /* 1. Read pad input. */
    sna_read_pad(work);

    /* 2. Apply messages. */
    sna_drain_messages(work);

    /* 3. Run override action (action2) — e.g. "drawing weapon"
     *    plays alongside primary "walking". */
    if (work->action2) ((SnaFn)work->action2)(work, work->time2++);

    /* 4. Run primary action — sets control.step, control.turn. */
    ((SnaFn)work->action)(work, work->time++);

    /* 5. Engine integrate. */
    GM_ActMotion(&work->body);
    GM_ActControl(&work->control);
    GM_ActObject(&work->body);

    /* 6. Effects. */
    sna_spawn_breath_if_cold(work);
    sna_spawn_footstep_marks(work);   /* afterse.c */
    sna_publish_player_pos(work);     /* GM_PlayerPosition = work->control.mov */
}
```

There are ~50 distinct action callbacks across `sna_init.c`. The
state graph (which transitions to which) is encoded in the action
bodies — each callback decides "should I switch to ActAim now?".

## SnaFlag1 / SnaFlag2 — Snake-private state

```c
typedef enum {
    SNA_FLAG1_UNK1   = 0x1,    /* knockdown — confirmed */
    SNA_FLAG1_UNK2   = 0x2,
    SNA_FLAG1_UNK3   = 0x4,
    /* … */
} SnaFlag1;
```

`flags1` and `flags2` are 32-bit bitmasks of Snake-specific
state. Some bits are confirmed (knockdown), most are still
`UNK*`. They gate animation choices, weapon usability, hitbox
shape.

`GM_PlayerStatus` (the global) gets set via
`GM_SetPlayerStatusFlag` — these flags are the *cross-actor*
visible state (PLAYER_DEBUG, PLAYER_BEHIND_CAM) others can read.

## Pad input mirror

Snake reads `GM_CurrentPadData` each tick into `pad_press` /
`pad_status` / `pad_release` and converts to:

- `trigger` — when fire button pressed.
- Direction bits → desired step direction.
- Action bits → which action to switch to.

The dispatch is huge (hundreds of lines) because the player has
context-dependent input: pressing X has different meanings when
crouching vs against a wall vs holding a box.

## Weapons

`current_weapon` is a `WP_*` enum. Each weapon has its own
`weapon/<name>.c` actor that Snake spawns when he switches.
`SnaInitWork.weapon` (the OBJECT) holds the *visible* weapon
KMD; the actual firing logic lives in `weapon/`.

Switch-weapon flow:

1. Player opens equipment menu (handled by `menu/`).
2. Menu writes `work->current_weapon = NEW_ID`.
3. Snake's next-frame Act sees the change, calls
   `sna_swap_weapon(work, NEW_ID)`.
4. The swap re-loads the weapon KMD, plays a draw animation,
   updates `work->weapon`'s OBJECT.

## Snake's `New*` factory

```c
void *NewSnake(int name, int where, int argc, char **argv)
{
    SnaInitWork *work = GV_NewActor(GV_ACTOR_LEVEL5, sizeof(SnaInitWork));
    if (!work) return NULL;
    GV_SetNamedActor(&work->actor, sna_act_8005AD10, sna_kill_8005B52C, "sna_init.c");
    if (sna_LoadSnake(work, name, where) < 0) {
        GV_DestroyActor(&work->actor);
        return NULL;
    }
    return work;
}
```

`sna_LoadSnake` is ~200 lines of resource init: GM_InitObject,
GM_ConfigMotionControl, NewShadow, NewGunLight, GM_AllocTarget,
sna_LoadSnake2..4 helpers. The PORT_BUILD guard added during
editor work bails when the SNAKE KMD isn't in cache (see
[02-game-loop.md](../02-game-loop.md) discussion of the crash
fix).

## sna_act_8005AD10 — the actual Act

The dispatcher. ~600 lines. Reads pad, calls the current action
function, advances state, integrates motion. Different from the
schematic above only in that it's all inline.

## shadow.c — the drop shadow actor

```c
GV_ACT *NewShadow(CONTROL *control, OBJECT *body, SVECTOR shadow_indices);
```

Spawns a small flat polygon below the character that follows
their position (queries floor height via `HZD_GetFloor`) and
darkens accordingly. The `shadow_indices` SVECTOR has
`vx`, `vy`, `vz`, `pad` = 4 bone indices used as anchor points
to compute shadow shape (the shadow stretches based on which
limb is over the floor).

Used by Snake, Meryl72, every guard, doll, demodoll. Each calls
`NewShadow(&work->control, &work->body, indices)` from their
`GetResources`.

## afterse.c — footsteps + surface effects

When Snake's foot bone passes through a floor on a footstep
animation frame, `afterse.c` is what fires:

- Plays a surface-appropriate sound (concrete vs grass vs
  water).
- Spawns `AN_Unknown_800CA594` (footstep mark sprite).
- Adds a noise event to the alert system.

The "alert" surface knows what kind of floor it is via HZD flags,
so afterse picks the right effect.

## breath.c — Snake's breath in cold stages

Wrapper that calls `AN_Breath(&head_bone_matrix)` every ~30 ticks
when `STATE_COLD_STAGE` (or the stage code matches a list). One
file because the team factored it out for clarity.

## sna_hzd.c — HZD-specific helpers

91 lines. Wraps `HZD_GetAddress` etc. with Snake-specific
parameters (Snake's collision uses different exclude flags than
guards). Mostly thin convenience wrappers.

## Movement modes

Snake's locomotion is more than just "walk forward":

| Mode | Pad input | Animation | Step magnitude |
| ---- | --------- | --------- | -------------- |
| Stand | none | idle | 0 |
| Walk | direction held lightly | walk-cycle | low |
| Run | direction held + run button | run-cycle | high |
| Sneak | crouch held | crouch-walk | low |
| Crouch | crouch toggle | crouch-idle / crouch-walk | low |
| Squat | quick down | squat-idle | 0 |
| Wall-press | back to wall | press anim | 0 |
| Tap-wall | wall + L1/R1 | tap anim | 0 |
| Aim | aim button | aim-stand / aim-walk | low |

Each mode has its own action callback. Transitions go through
the `ACTTRANS` table for animation choice.

## Damage

When a TARGET hits Snake (`work->target->damaged != 0`):

1. `flags1` updated based on damage kind.
2. Action switches to `ActDamage<variant>` (light hit / hard hit /
   knockdown).
3. `life -= damage_amount`.
4. `STATE_DAMAGED` set on `GM_GameStatus` for one frame so HUD
   draws red overlay.
5. If life ≤ 0, transition to ActDying and eventually GameOver.

## Common pitfalls

(Beyond [02-game-loop.md](../02-game-loop.md)'s NULL guard for
missing-KMD.)

### Snake renders as T-pose

`work->m_segs1` / `work->m_segs2` weren't filled — animation
playback has no segments to step through. Cause: `GM_ConfigMotionControl`
got a NULL OAR. Fix: ensure the OAR ("snake" or stage variant) is
in cache before NewSnake runs.

### Snake walks backwards into walls

`work->control.exclude_flag` defaults to 2. If a stage GCL
overrode it to skip a specific surface, Snake may walk through
that wall *forward* but get pushed back *backwards*. Reset
`exclude_flag` after the cinematic moment ends.

### Weapons don't render

`work->weapon` is a separate OBJECT. If `current_weapon = WP_NONE`,
the weapon OBJECT's `objs` may be NULL. Drawing skips. Switching
back to a real weapon should restore.

## See also

- [02-game-loop.md](../02-game-loop.md) — when Snake's Act fires
  and what guards it.
- [03-control-and-motion.md](../03-control-and-motion.md) — how
  CONTROL and MOTION_CONTROL drive his pose.
- [`source/weapon/`](../../../../source/weapon/) — the per-weapon
  code.
- [`source/equip/`](../../../../source/equip/) — equipment
  (BODYARM / GASMASK / BOX / CIGS / BANDANA).
- [`source/menu/`](../../../../source/menu/) — the inventory UI
  that feeds `current_weapon` and `current_item`.
