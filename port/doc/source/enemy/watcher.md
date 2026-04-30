# `enemy/watcher.c` — the canonical guard

Generic patrol guard. The most-spawned `chara` in the game by a
wide margin — every regular soldier on every stage uses
`WATCHER` (chara hash registered in `MainCharacterEntries[]`).

716 lines for the actor itself, plus the shared `think.c` /
`action.c` / `command.c` / `check.c` files that handle most of
the AI.

## WatcherWork — the state struct

The base struct every guard variant builds on. Lives in
`enemy.h`:

```c
typedef struct _WatcherWork {
    GV_ACT          actor;
    CONTROL         control;          /* pos / collision / msgs */
    OBJECT          body;             /* "guard" KMD */

    int             has_kmd;          /* current LOD slot (0=hi, 1=lo) */
    DG_DEF         *kmd;              /* full-detail mesh */
    DG_DEF         *def;              /* low-poly mesh — same as kmd
                                         when no LOD */
    MOTION_CONTROL  m_ctrl;           /* anim player */
    MOTION_SEGMENT  m_segs1[17];      /* primary anim segments */
    MOTION_SEGMENT  m_segs2[17];      /* mask segments (upper-body) */
    SVECTOR         rots[16];         /* per-bone rotations */
    SVECTOR         adjust[16];       /* per-bone adjusts (head-track) */

    OBJECT          weapon;           /* FAMAS or SOCOM */
    MATRIX          light[2];

    WatcherUnk      unknown;          /* shared comm state — broadcast */

    void           *action;           /* current action fn */
    void           *action2;          /* override / secondary action */
    int             time;
    int             time2;
    int             actend;

    TARGET         *target;           /* hit-target — bullets damage here */
    TARGET          field_904;        /* attack-target — guard's bullets
                                         hit Snake here */
    TARGET          field_94C;        /* touch-target — physical body */
    TARGET          punch;            /* melee attack volume */
    HOMING         *hom;              /* aim assist */

    short           scale;            /* body scale (damage knockback) */
    short           visible;          /* 1 = render, 0 = hidden */
    int             n_nodes;          /* patrol-route waypoint count */
    SVECTOR         nodes[32];        /* the waypoints */

    int             search_flag;
    GV_ACT         *field_AF0;        /* glight (gun-tip) */
    int            *field_AF4;
    GV_ACT         *field_AF8;        /* shadow */
    int            *field_AFC;

    void           *field_B00[8];     /* fn-ptr table — per-state hooks */

    short           think1, think2,   /* think.c sub-state counters */
                    think3, think4;
    unsigned int    count3;
    int             t_count, l_count; /* time / look-around counters */
    int             next_node;        /* cursor into nodes[] */

    WatcherPad      pad;              /* synthetic input bitmask */
    unsigned int    trigger;
    GV_ACT         *subweapon;        /* secondary weapon actor */

    GV_ACT         *mark;             /* alert-icon (eyeflash) */
    GV_ACT         *mosaic;           /* mosaic-censor effect */
    int             mark_time;
    int             act_status;       /* current high-level state */

    /* PARAM fields exposed at top level (per WatcherWork's flatter
     * layout vs other variants which nest them). */
    signed char     param_blood;
    signed char     param_area;
    signed char     param_root;       /* current node group */
    char            param_c_root;
    signed char     param_item;       /* item dropped on death */
    short           param_life;
    short           param_faint;
    char            local_data;       /* current life copy */

    VISION          vision;           /* facedir / angle / length */
    int             alert_level;      /* 0..N — current alert intensity */
    signed char     modetime[4];      /* per-mode timeout counters */

    SVECTOR         start_pos;        /* spawn position */
    SVECTOR         target_pos;       /* current move target */
    int             start_addr, start_map;
    int             target_addr, target_map;

    int             sn_dis;           /* dist to Snake */
    int             sn_dir;           /* bearing to Snake */
    short           faseout;          /* 1 = dying / fading out */

    /* …more fields, mostly per-mode timing/state. */
} WatcherWork;
```

The struct is ~3.2 KiB. Many `field_*` placeholders remain.

## VISION — the awareness cone

```c
typedef struct _VISION {
    short facedir;       /* PSX 4096-fixed angle of cone center */
    short angle;         /* half-cone angle (PSX 4096 = 360°) */
    short length;        /* PSX world units, max sight distance */
    short field_B92;
} VISION;
```

`facedir` updates each tick to track Snake when alerted (or
follow patrol direction when not). `length` is typically 5000
PSX units (~5m at the game's scale).

## WatcherUnk — the shared comm state

```c
typedef struct _WatcherUnk {
    int     field_00;
    short   field_04, field_06;
    int     field_08;
    SVECTOR field_0C;        /* world pos of last sighting */
    int     field_14;        /* alert-cooldown counter */
    short   last_set;        /* command id this guard issued */
    short   last_unset;      /* command id this guard cleared */
    short   field_1C;
    short   field_1E;        /* "alert active" flag */
    short   field_20, field_22;
} WatcherUnk;
```

Every guard has one. When guard A spots Snake, A writes
`last_set = ALERT_ID` to its own `unknown`. Other guards' `Check`
fns read peers' state via the `EnemyCommand_*` shared command
table and react.

## NewWatcher — the factory

(Inferred shape from the file.)

```c
void *NewWatcher(int name, int where, int argc, char **argv)
{
    WatcherWork *work = GV_NewActor(GV_ACTOR_LEVEL4, sizeof(WatcherWork));
    if (work) {
        GV_SetNamedActor(&work->actor, WatcherAct, WatcherDie, "watcher.c");
        if (LoadWatcher(work, name, where) < 0) {
            GV_DestroyActor(&work->actor);
            return NULL;
        }
    }
    return work;
}
```

`LoadWatcher` parses the GCL `chara &WATCHER ...` directive's
options. The full set:

| Option | Field | Meaning |
| ------ | ----- | ------- |
| `-p X Y Z` | mov | spawn position |
| `-d Y` | rot.vy | spawn yaw (PSX 4096 = 360°) |
| `-l N` | param_life | initial HP |
| `-f N` | param_faint | knockdown threshold |
| `-i ITEM_ID` | param_item | what to drop on death |
| `-a <attr>` | radar_atr | RADAR_VISIBLE / SIGHT bits |
| `-v FACEDIR ANGLE LENGTH` | vision | sight cone params |
| `-r ROUTE_SLOT` | nodes[] | HZD route to patrol |
| `-s VOX1 VOX2 ...` | voices | voice-line table |
| `-c R G B` | light | per-side colour |
| `-w $s:HHHH` | weapon | weapon KMD hash |
| `-b BLOOD_TYPE` | param_blood | blood colour variant |
| `-x <flags>` | various | misc flags (see _unreversed.md) |

After parsing:

1. `GM_InitControl` — register CONTROL.
2. `GM_InitObject(body, KMD_GUARD, BODY_FLAG, MOTION_GUARD)`.
3. `GM_ConfigMotionControl` — wire animation player.
4. `GM_InitObject(weapon, KMD_FAMAS, WEAPON_FLAG, 0)`.
5. `GM_ConfigObjectRoot(weapon, body, 4)` — bone 4 = right hand.
6. `NewShadow(...)`.
7. `NewGunLight_800D3AD4(&body->objs->objs[4].world, ...)`.
8. Comm-state init — zero the `WatcherUnk`.
9. `InitTarget(work)` — register 4 TARGETs:
   - `target` — what Snake's bullets hit (ENEMY_SIDE).
   - `field_904` — what Snake hits (PLAYER_SIDE).
   - `field_94C` — physical body touch (ENEMY_SIDE,
     TARGET_TOUCH).
   - `punch` — melee attack volume (ENEMY_SIDE).
10. Patrol-node parsing via HZD route slot.
11. Initial action: usually patrol or stand-watch depending on
    `-a` flags.

## Per-tick `Act`

```c
static void WatcherAct(WatcherWork *work)
{
    /* HASH_KILL drains: actor self-destroys on KILL or various
     * stage-cleanup messages. */
    if (RootFlagCheck_800C3EE8(work)) {
        return;   /* mesg handled, switch state, exit early */
    }

    if (!work->faseout) {
        WatcherPushMove(work);            /* pre-control hook */
        GM_ActControl(&work->control);    /* integrate motion */
        GM_ActObject2(&work->body);
        GM_ActObject2(&work->weapon);
        DG_GetLightMatrix2(&work->control.mov, work->light);

        WatcherActionMain(work);          /* run think + action */

        GM_MoveTarget(work->target, &work->control.mov);
        GM_PushTarget(work->target);

        /* Touch-target / body */
        if (work->target->class & TARGET_TOUCH) {
            /* same pattern as zako11e — check + push */
        }

        /* Body scale (damage knockback bounce) */
        ScaleMatrix(&work->body.objs->world, ...);
    }

    UpdateVisibilityAndLOD(work);

    /* Cross-watcher kill sync */
    if (EnemyCommand_kill_signal && my_command_entry.field_04 == 1) {
        GV_DestroyActor(&work->actor);
    }
}
```

## RootFlagCheck — the mesg drain

[`watcher.c:49`](../../../../source/enemy/watcher.c#L49). Drains
incoming messages. Recognised:

| Message | Action |
| ------- | ------ |
| `0x430F` | "Change route" — switch to a new patrol node group |
| `0xF1BD` | "Faseout" — initiate graceful death/disappear |
| `0x1DC4` | "Phase in" — restore visibility (after faseout) |
| `HASH_KILL` | (handled by `GM_CheckMessage` shortcut) |

The 0x430F handler reads the new route's center pos, looks up
the HZD bucket, and updates `field_B7C` so the next think tick
finds the new patrol path.

## Action / Think / Command / Check split

(Same pattern as `animal/zako11e/`. The base lives here in
`enemy/`; zako variants copy + tune.)

- `enemy/think.c` (2886 lines) — the brain. Picks states
  (patrol → alert → engage → cover → retreat). Reads vision,
  pad input, comm state. Writes `work->pad.press` synthetic
  inputs.

- `enemy/command.c` (1328 lines) — combat helpers.
  - Bullet spawn (`spawn_watcher_bullet`).
  - Voice-line playback for alerts.
  - Comm-broadcast (when this guard sees Snake, write
    `unknown.last_set` so peers learn).
  - Damage application from incoming hits.

- `enemy/action.c` (2090 lines) — state callbacks. ~50 states:
  patrol, walk, run, idle, look-around, draw-weapon, aim,
  fire-once, fire-burst, reload, take-cover, knockdown, getup,
  faint, dying.

- `enemy/check.c` (436 lines) — per-frame condition tests.
  - `CheckSee_*` — Snake in vision cone?
  - `CheckShot_*` — hit by bullet this frame?
  - `CheckSound_*` — noise event in range?
  - `CheckCommand_*` — comm state from peers?
  - `CheckPad_*` — synthetic input gates.

## EnemyCommand — cross-guard coordination

```c
extern ENEMY_COMMAND EnemyCommand_800E0D98;

typedef struct _ENEMY_COMMAND {
    /* …shared state… */
    A4_INNER_STRUCT field_0xC8[N_GUARDS];
    /* …more fields */
} ENEMY_COMMAND;
```

The global table indexed by `work->field_B78` (this guard's
slot in the comm group). Each entry:

```c
typedef struct _A4_INNER_STRUCT {
    short field_00;
    short field_02;
    short field_04;     /* 1 = "kill me"; 2 = "phase in" */
    /* …more fields */
} A4_INNER_STRUCT;
```

`field_04 == 1` is the kill signal; `field_04 == 2` is "phase in
visible". The table is consumed by the cross-watcher kill sync
at the end of `WatcherAct`.

## Alert state machine

`work->alert_level` ranges 0..(some max). Driven by:

- `CheckSee` returning 1 → alert_level += step.
- Time-since-last-see ticks → alert_level decays.

State thresholds:

| `alert_level` | State |
| ------------- | ----- |
| 0 | Patrol |
| > 0 | Caution (look harder, slower decay) |
| > threshold_a | Alert (raise weapon, call out) |
| max | Engage (fire) |

## Targeting

Four TARGET registrations:

- **`target`** (TARGET_FLAG, ENEMY_SIDE) — what Snake hits to
  damage the guard. Damage value comes from `param_life`.
- **`field_904`** (TARGET_POWER, PLAYER_SIDE) — what the guard's
  bullets damage. Snake takes hits via this.
- **`field_94C`** (TARGET_TOUCH, ENEMY_SIDE) — physical body for
  Snake-bumping-into-guard contact.
- **`punch`** (varies, ENEMY_SIDE) — melee strike volume,
  active only during punch action callback.

## Search flag

`work->search_flag` is a bitmask of "what should I do next".
Bits include "investigate noise pos", "patrol after alert
decays", "check for body".

## Death + fade-out

When `param_life ≤ 0`:

1. Action switches to dying.
2. `faseout = 1` — Act loop skips the gameplay branch.
3. Body scale lerps to 0 over ~60 ticks.
4. EnemyCommand entry's `field_04 = 1`.
5. Next-tick destroy check fires `GV_DestroyActor`.

## See also

- [index.md](index.md) — folder overview.
- [`animal/zako11e/`](../animal/zako11e.md) and
  [`animal/zako11f/`](../animal/zako11f.md) — derived variants.
- [`source/game/target.c`](../../../../source/game/target.c) —
  the TARGET system.
- [`source/game/homing.c`](../../../../source/game/homing.c) —
  HOMING for aim assist.
