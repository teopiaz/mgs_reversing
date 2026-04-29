# `animal/zako11e/` — late-game guard (s11e)

A guard variant for stage **s11e** (the snowy passage between the
Hind hangar and the Sniper Wolf area). Functionally a standard MGS
guard but with stage-specific tuning + a low-poly LOD swap +
modified action set.

The "zako" naming (雑魚 / "small fry") is the Japanese term for
generic mooks; the codebase keeps the original team's naming.

Total: 4962 lines / 6 files. Fully decompiled.

The base `Zako` / `Watcher` types live in `source/enemy/`. This
component **shadows** that base — re-implements the same
`think → command → action → check` pattern with stage-tuned
constants and adds:

- A low-poly KMD swap when the camera is far / Snake is in
  first-person mode (perf optimisation).
- Stage-private patrol / response timings.
- Different "spotted" → "hunt" → "lost" thresholds.
- Custom death / faint logic for the snowy environment (no fire
  effect, longer fade-out).

## File map

| File | Lines | Role |
| ---- | ----- | ---- |
| [`zako11e.c`](../../../../source/animal/zako11e/zako11e.c) | 602 | Factory (`NewZako11E_*`) + Act dispatcher + Init/Die. The Act loop calls Push, Control, Object, ActionMain, Push targets — same as Meryl72 but for a guard. |
| [`zk11eaction.c`](../../../../source/animal/zako11e/zk11eaction.c) | 1866 | The action callbacks — every `Act<Name>_<addr>` function. ~50 actions covering patrol, alert, chase, aim, fire, take-damage, knockdown, getup, faint, dying. **Largest file in animal/**. |
| [`zk11ecom.c`](../../../../source/animal/zako11e/zk11ecom.c) | 1000 | The "command" tier — combat-action helpers used by think and action. Bullet trajectories, target selection, line-of-sight tests against HZD. |
| [`zk11ethink.c`](../../../../source/animal/zako11e/zk11ethink.c) | 733 | High-level decisions: switching between patrol / alert / hunt modes, evaluating intel from Watcher comm system, reacting to noise. |
| [`zk11echeck.c`](../../../../source/animal/zako11e/zk11echeck.c) | 374 | Per-frame condition tests: vision-cone hit, damage detection, near-Snake test, alert decay. |
| [`zk11eenemy.c`](../../../../source/animal/zako11e/zk11eenemy.c) | 387 | Wraps the standard `EnemyCommand_*` API to integrate this zako into the global enemy registry. |

No `put.c` / `override.c` — those concerns are folded into other
files for this component (e.g. `zk11eaction.c` has visual-effect
spawns inline).

## ZakoWork — the state struct

The struct lives in `enemy/enemy.h` (shared with the base zako/
watcher), not in `animal/zako11e/`. That's because animal/zako11e
re-uses the same struct as enemy/'s zako, then *adds* private
state via reserved padding fields.

A reduced view:

```c
typedef struct ZakoWork {
    GV_ACT          actor;
    CONTROL         control;
    OBJECT          body;
    MOTION_CONTROL  m_ctrl;
    MOTION_SEGMENT  m_segs1[N];
    MOTION_SEGMENT  m_segs2[N];
    SVECTOR         rots[32];

    OBJECT          weapon;
    MATRIX          light[2];

    void           *action;        /* current action fn ptr */
    void           *action2;       /* override / secondary action */
    int             time;
    int             time2;

    TARGET         *target;        /* hit-target — guards damage on this */
    TARGET          field_904;     /* attack-target — Snake hits on this */
    TARGET          field_94C;     /* touch-target — physical body */

    PARAM           param_…;       /* health / faint thresholds */
    int             param_life;
    int             param_faint;

    VISION          vision;        /* cone of awareness */
    Pad             pad;           /* synthetic-input mirror */

    GV_ACT         *shadow;
    int            *shadow_enable;
    GV_ACT         *glight;        /* gun-tip light */
    int            *glight_enable;

    /* Zako11E-specific: */
    DG_DEF         *def;           /* normal-detail KMD */
    DG_DEF         *kmd;           /* the same pointer alias for swap */
    int             has_kmd;       /* 0 = low-poly, 1 = normal */
    int             param_low_poly;/* whether this guard supports the LOD swap */
    int             visible;       /* DG_(In)VisibleObjs gate */
    int             scale;         /* body scale (PSX 4096-fixed) */
    int             faseout;       /* 1 = dying / fading out, skips Act body */

    WatcherUnk      unknown;       /* shared comm state — see _unreversed.md */
    int             field_B74;     /* command id for cross-zako sync */
    int             local_data;    /* current life copy */

    /* …more fields… */
} ZakoWork;
```

`unknown` is the cross-guard communication state — when one guard
spots Snake, neighbouring guards in the same comm group hear about
it via this struct. Its layout is in `enemy.h` and partially named
(`last_set`, `last_unset`, `field_14`, `field_1E`).

## NewZako11E — the factory

```c
void *NewZako11E_<addr>(int name, int where, int argc, char **argv)
{
    ZakoWork *work = GV_NewActor(GV_ACTOR_LEVEL4, sizeof(ZakoWork));
    if (work) {
        GV_SetNamedActor(&work->actor,
                         ZakoAct_800D3684,
                         ZakoDie_<addr>,
                         "zako11e.c");
        if (s11e_zako11e_800D3990(work, name, where) < 0) {
            GV_DestroyActor(&work->actor);
            return NULL;
        }
    }
    return work;
}
```

`s11e_zako11e_800D3990` parses the GCL options and inits resources.
The recognised options match the standard zako:

| Option | Field | Meaning |
| ------ | ----- | ------- |
| `-p X Y Z` | mov | spawn position |
| `-d Y` | rot.vy | spawn yaw |
| `-l N` | param_life | initial HP |
| `-f N` | param_faint | knockdown threshold |
| `-a <attr>` | radar_atr | RADAR_VISIBLE / SIGHT bits |
| `-v <flags>` | vision | cone params |
| `-r <route>` | route | HZD route slot for patrol path |
| `-s VOX | voices[] | voice IDs |
| `-i N` | param_low_poly | 1 = use LOD swap, 0 = always full |
| `-c R G B` | light colour | per-side ambient |
| `-w $s:HHHH` | weapon | weapon KMD hash |

After parsing:

1. `GM_InitControl` — register CONTROL.
2. `GM_InitObject(body, KMD_GUARD, BODY_FLAG, MOTION_GUARD)` —
   load the guard KMD ("guard11e" or similar).
3. `GM_ConfigMotionControl` — wire the animation player.
4. `GM_InitObject(weapon, KMD_FAMAS, WEAPON_FLAG, 0)` — FAMAS rifle.
5. `GM_ConfigObjectRoot(weapon, body, 4)` — bind to right hand.
6. `NewShadow(...)`.
7. `NewGunLight_800D3AD4(&body->objs->objs[4].world, ...)`.
8. `s11e_zako11e_800D3934(work)` — zero the comm-state struct
   (`unknown`).
9. `InitTarget_800D3800(work)` — register 3 TARGETs:
   - **target** (TARGET_FLAG, ENEMY_SIDE) — what Snake's bullets hit.
   - **field_904** (TARGET_POWER, PLAYER_SIDE) — what the guard's
     bullets hit (Snake).
   - **field_94C** (TARGET_TOUCH, ENEMY_SIDE) — physical body
     touch detection.
10. `param_low_poly` from `-i` GCL option.
11. Initial action: usually patrol or alert depending on `-a` flags.

## ZakoAct — the per-tick driver

[`zako11e.c:105`](../../../../source/animal/zako11e/zako11e.c#L105):

```c
void ZakoAct_800D3684(ZakoWork *work)
{
    if (HASH_KILL received) { GV_DestroyActor; return; }

    RootFlagCheck_800D34C8(work);          /* (no-op stub) */

    if (!work->faseout) {
        Zako11EPushMove_800D889C(work);    /* think + action selection */
        GM_ActControl(ctrl);                /* integrate motion */
        GM_ActObject2(&work->body);
        GM_ActObject2(&work->weapon);
        DG_GetLightMatrix2(&ctrl->mov, work->light);

        Zako11EActionMain_800D8830(work);   /* run current action callback */

        GM_MoveTarget(work->target, &ctrl->mov);
        GM_PushTarget(work->target);

        /* Touch-target (his body for collision against Snake) */
        if (work->target->class & TARGET_TOUCH &&
            work->field_94C.class & TARGET_TOUCH)
        {
            if (work->field_94C.damaged & TARGET_TOUCH) {
                work->field_94C.damaged &= ~TARGET_TOUCH;
            }
            GM_MoveTarget(&work->field_94C, &ctrl->mov);
            GM_TouchTarget(&work->field_94C);
        }

        /* Body scale animation (used for damage knockback bounces) */
        vec.vx = vec.vy = vec.vz = work->scale;
        ScaleMatrix(&work->body.objs->world, &vec);
    }

    s11e_zako11e_800D354C(work);            /* visibility + LOD swap */
    *work->glight_enable = 0;
    *work->shadow_enable = 0;

    /* Cross-zako kill sync — if the comm system tells me to die, die. */
    if (s11e_dword_800DF3B4 == 0xF &&
        ZakoCommand_800DF280.field_0x8C[work->field_B74].field_04 == 1)
    {
        GV_DestroyActor(&work->actor);
    }
}
```

Key points:

- **`work->faseout` gates almost everything off**. When the guard
  starts dying, `faseout = 1` and the body just renders + fades.
  Action / collision / pushing all stop.
- **`Zako11EPushMove`** is called **before** `GM_ActControl`. This
  inverts the order from Meryl72. Reason: PushMove sets
  `control.step` from think's decision, and ActControl integrates.
- **`Zako11EActionMain`** is called **after** rendering. So the
  action *for next frame* is decided based on this-frame's
  rendered pose. Slightly weird but consistent with the action's
  job being to set up the next frame's animation.
- **`s11e_zako11e_800D354C`** does the LOD swap each tick. See
  below.

## LOD swap — `s11e_zako11e_800D354C`

The novel-vs-base feature. Some guards have two KMDs: a normal-
detail mesh (used in third-person mid-distance) and a low-poly
mesh (used when the camera's in first-person mode or behind-camera
mode, where many guards may be visible at once).

```c
void s11e_zako11e_800D354C(ZakoWork *work)
{
    if (work->visible) {
        if (work->param_low_poly == 1) {
            if (GM_GameStatus & (GAME_FLAG_BIT_07 | STATE_BEHIND_CAMERA) ||
                GM_Camera.first_person)
            {
                /* In first-person / behind-cam → use low-poly. */
                if (work->has_kmd != work->param_low_poly) {
                    work->has_kmd = work->param_low_poly;
                    s11e_zako11e_800D34D0(work->body.objs, work->def);
                }
            }
            else if (work->has_kmd) {
                /* Restore normal KMD. */
                work->has_kmd = 0;
                s11e_zako11e_800D34D0(work->body.objs, work->kmd);
            }
        }
        DG_VisibleObjs(work->body.objs);
        DG_VisibleObjs(work->weapon.objs);
        work->shadow_enable[0] = 1;
        work->glight_enable[0] = 1;
    } else {
        DG_InvisibleObjs(work->body.objs);
        /* …same for weapon, shadow, glight — all off when invisible */
    }
}
```

`s11e_zako11e_800D34D0` rebinds the `DG_OBJS`'s `def` pointer to
the new KMD and frees / reallocates the per-bone packets. That's
why this isn't just a flag flip — the rendering data changes
shape.

## ActionMain — the action dispatcher

`zk11eaction.c::Zako11EActionMain_800D8830` runs the current action
callback. The pattern matches Meryl72:

```c
int Zako11EActionMain(ZakoWork *work)
{
    /* 1. Run override action (if queued) at action2. */
    if (work->action2) {
        ((ZakoFn)work->action2)(work, work->time2++);
    }
    /* 2. Run primary action. */
    if (work->action) {
        ((ZakoFn)work->action)(work, work->time++);
    }
    /* 3. If primary action self-cleared, switch to default idle next tick. */
    if (!work->action) {
        SetMode(work, IdleAction);
    }
    return 1;
}
```

## Action callbacks — the 50 states

`zk11eaction.c` is the largest single file in animal/ — every state
the guard can be in. They group:

### Patrol / passive
- Idle stand
- Walking patrol path
- Looking around (head turn)
- Sitting / smoking (rare, in some scenes)

### Alerted
- Heard a noise — turn toward sound
- Spotting — visible target acquired, react
- Calling for backup — voice + alert state propagation

### Combat
- Aim ready
- Take aim at target
- Fire single round
- Fire burst
- Reload
- Take cover

### Damage
- Hit-stagger (light damage)
- Knockdown (heavy damage)
- Getup
- Faint (knocked unconscious)
- Dying

### Special
- Throw grenade
- Open door
- Climb ladder
- Rescued state (bound / hostage)

Each action is a function with the signature:

```c
void ActSomething_<addr>(ZakoWork *work, int frame_count);
```

The `frame_count` parameter is `work->time` — frames since this
action was set. The action uses it to:

- Time animation phase changes (e.g. play the windup → throw →
  recover sequence of grenade-toss).
- Trigger frame-keyed events (muzzle flash on frame 4 of fire,
  bullet spawn on frame 6).

When the action ends or wants to transition, it sets
`work->action = NextActionFn; work->time = 0;` (often via a
helper that handles both atomically).

## Think — `zk11ethink.c`

The brain. Reads:

- `work->vision.facedir/length/angle` — what the guard can see.
- `work->target` damage state — was he hit recently?
- The shared comm state in `unknown` — did a peer report
  something?
- `GM_PlayerPosition` and `GM_PlayerControl->mov` — Snake's
  current pos.
- `work->control.nears[]` — what surfaces are nearby (for cover
  decisions).

Writes:

- `work->pad.press` — synthetic input bits the action dispatcher
  reads.
- `work->action2` — to install an override action.
- The shared comm state — to broadcast "I see Snake" to peers.

Think runs once per few frames (rate-limited by `work->time` cycles
internally), not every frame. This is intentional — guard AI
doesn't need to re-evaluate every tick, and the frame budget is
already tight.

## Check — `zk11echeck.c`

Per-tick condition tests called by think:

- `CheckSee_<addr>(work)` — returns 1 if Snake is in vision cone
  (uses `GM_PlayerPosition` + `vision`).
- `CheckShot_<addr>(work)` — returns 1 if hit by bullet this frame.
- `CheckSound_<addr>(work)` — returns 1 if a noise event happened
  in range.
- `CheckCommand_<addr>(work)` — returns 1 if comm-system has news.
- Various `CheckPad_<addr>` variants for synthesised input gates.

## Command — `zk11ecom.c`

Helper layer between think (high-level) and action (state
callback). Provides:

- Bullet spawn helpers — when an action fires, command emits the
  bullet actor with the right trajectory.
- Path-finding queries — "what's the route to Snake from here?".
- Voice playback for alert calls.
- Damage application — when this guard is hit, command computes
  damage + updates `param.life`.

## Cross-zako communication

Multiple zakos in the same map share state via a global
`ZakoCommand_<addr>` table indexed by `work->field_B74` (the
guard's id within the comm group). The `s11e_dword_800DF3B4`
flag is the "alert level" of the group.

When one guard spots Snake:

1. He sets his own `unknown.last_set` to the action id he wants
   peers to take.
2. The shared command table records his alert.
3. Peer guards' `Check` functions read the table; their `Think`
   sees `unknown.last_set` and switches to a matching action.

This is how the "guard radio" effect is implemented — one shout
becomes a wave of alerted guards.

## Common pitfalls

### Guard never sees Snake

`vision.length` defaults to ~5000 PSX units. If the GCL `-v`
option underspecified it, the guard has near-zero sight range.
Check the `-v` argument values.

### Guard fires through walls

Line-of-sight isn't checked in the action callback — it's checked
in `zk11eenemy.c::CheckLOS_<addr>` before the action is selected.
If think bypassed the check (rare, usually a bug in the comm
system), the action will fire bullets that pass through walls.

### Guard count keeps growing

Faseout doesn't `GV_DestroyActor` — only the cross-zako kill check
at the bottom of Act does. If the comm-state cleanup is broken,
guards stay in `faseout` forever. Watch
`s11e_dword_800DF3B4 == 0xF` — that's the destroy trigger.

### LOD swap flickers

`work->has_kmd` is per-tick state. If two consecutive frames
disagree on `STATE_BEHIND_CAMERA`, the swap toggles every frame.
The guard renders fine but the per-bone `DG_OBJS` packets are
reallocated 60×/sec — performance issue, not visual.

## See also

- [`source/enemy/`](../../../../source/enemy/) — the canonical
  zako AI this component diverged from. The `enemy/watcher.c`
  / `enemy/think.c` / `enemy/action.c` files are 80% identical.
- [zako11f.md](zako11f.md) — sibling component for stage s11f.
  Compare side-by-side to see what's per-stage tunable.
- [doll.md](doll.md) — for the "actor without AI" base
  pattern.
- [`source/game/target.c`](../../../../source/game/target.c) — the
  TARGET system used for `target` / `field_904` / `field_94C`.
