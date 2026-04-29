# `animal/meryl72/` — Meryl as a stage-7/9 ally

The most elaborate component in `animal/`. Meryl appears as a
**fully-AI ally** in s07c (the bathroom / women's restroom escape
sequence) and s09a (the post-Sniper-Wolf canyon walk-through). She
follows Snake, fights guards, takes damage, has her own life bar,
plays grenade animations, and reacts to scripted moments.

The "72" in the filename is a stage code reference (07a / 09a → "07"
+ "2" = stage-bucket 7 mode 2). She has separate non-72 logic for
her cinematic appearances elsewhere — that lives in
`source/chara/others/` not here.

Total: 5266 lines across 8 files. The largest single source file is
`action.c` at 1487 lines.

## File map

| File | Lines | Role |
| ---- | ----- | ---- |
| [`meryl72.h`](../../../../source/animal/meryl72/meryl72.h) | 314 | `Meryl72Work` (~3.1 KiB), `PARAM` health struct, `VISION` cone, `Meryl72Pad` input, all the ACTION_* constants, ~50 forward-declared action callbacks. |
| [`meryl72.c`](../../../../source/animal/meryl72/meryl72.c) | 678 | Factory + `Act` dispatcher + `Die` + `GetResources` (GCL parse). Same shape as `doll.c` but bigger because she has a weapon, a target system, and patrol nodes. |
| [`action.c`](../../../../source/animal/meryl72/action.c) | 1487 | Every `ActSomething_<addr>` state callback. ~30 actions: idle, ready-gun, aim, shoot, grenade, reload, dodge, knockdown, getup, danbowl-keri (kick). |
| [`ml72act.c`](../../../../source/animal/meryl72/ml72act.c) | 330 | The action **dispatcher** — `Meryl72ActionMain` reads `act_status` + the `Meryl72Pad` input and decides which `work->action` to install next. |
| [`think.c`](../../../../source/animal/meryl72/think.c) | 1310 | High-level decisions: target acquisition, where to walk, whether to engage / flee / call out, patrol-node selection. Sets `work->pad` flags that ml72act consumes. |
| [`think9.c`](../../../../source/animal/meryl72/think9.c) | 903 | Stage-9 variant of think.c — the canyon scene needs different waypoint logic + scripted "wait for Snake" beats. |
| [`check.c`](../../../../source/animal/meryl72/check.c) | 379 | Per-frame condition tests: `CheckPad`, `CheckDamage`, `ReviseReadyGun`. Reads world state, sets `work->pad.press` bits. |
| [`override.c`](../../../../source/animal/meryl72/override.c) | 189 | Scripted-moment overrides — when a cinematic / script wants to force her into a specific pose, this short-circuits the AI. |
| [`put.c`](../../../../source/animal/meryl72/put.c) | 180 | Per-frame visual effect spawns: breath, blood markers when hit, shell-casing ejections during gunfire. |

## Meryl72Work — the state struct

This is one of the most fully-named structs in the codebase — most
fields have been recovered to readable names. Highlights:

```c
typedef struct Meryl72Work {
    GV_ACT          actor;           /* engine actor header */
    CONTROL         control;         /* pos / collision (game/control.c) */
    OBJECT          body;            /* "meryl" KMD */
    MOTION_CONTROL  m_ctrl;          /* "mel_07a" or "mel_09a" anim lib */
    MOTION_SEGMENT  m_segs1[17];     /* primary anim segments */
    MOTION_SEGMENT  m_segs2[17];     /* mask anim segments (overrides) */
    SVECTOR         rots[32];        /* bone rotations buffer */
    OBJECT          weapon;          /* "desert" KMD (Desert Eagle) */
    MATRIX          light[2];

    UNK             f8BC;            /* per-tick state mirror */
    void           *action;          /* CURRENT ACTION FN PTR ← */
    void           *action2;         /* OVERRIDE / PRE-ACTION (e.g. grenade
                                        toss runs alongside walk) */
    int             time;            /* primary action frame counter */
    int             time2;           /* override action frame counter */
    int             actend;          /* end-of-action flag */

    TARGET         *target;          /* enemy hits her here */
    TARGET          target2;         /* she pushes / bumps into things via this */
    TARGET          punch;           /* melee hit-target */
    HOMING         *hom;             /* aim assist for shooting */

    int             n_patrols;       /* count of authored patrol nodes */
    SVECTOR         nodes[32];       /* the patrol nodes themselves */
    int             next_node;       /* cursor into nodes[] */

    GV_ACT         *shadow;          /* drop shadow */
    int            *shadow_enable;
    GV_ACT         *glight;          /* gun-tip light when ready-gun */
    int            *glight_enable;

    void           *fA9C[8];         /* fn-ptr table (see _unreversed.md) */
    short           think1, think2;  /* think.c sub-state counters */
    short           think3, think4;
    int             count3;

    Meryl72Pad      pad;             /* per-tick input bitmask */
    unsigned int    trigger;         /* fire-pressed flags */
    GV_ACT         *subweapon;       /* grenade actor when one is in flight */

    PARAM           param;           /* health / faint / defends bitmap */
    int             fAF0, fAF4;
    char            fB0A, fB0B;      /* anim-step gates */
    int             fB0C;
    VISION          vision;          /* cone: facedir + angle + length */

    signed char     modetime[8];     /* per-mode timeout counters */
    int             act_status;      /* current ACTION_* value */
    SVECTOR         fB28, start_pos, target_pos;
    int             start_addr, start_map;
    int             target_addr, target_map;

    int             sn_dis, sn_dir;  /* dist + bearing to Snake */
    int             player_addr;
    SVECTOR         player_pos;
    int             player_map;

    short           stage;           /* 7 or 9 */
    int             voices[25];      /* voice-line table */

    /* …more state */
    int             proc_id;         /* GCL proc id for "tell script" hook */
} Meryl72Work;
```

The recovered names tell the story: she's much closer to a
"player-grade" actor than a `doll`.

## PARAM — the health struct

```c
typedef struct _PARAM {
    signed char fAF8;       /* current attack-readiness (0..3) */
    char        fAF9;       /* state index */
    char        fAFA;       /* sub-state */
    signed char c_root;     /* current "root" — bone group active */
    char        defends[4]; /* per-direction defence values */
    signed char roots[4];   /* root group rotations for damage */
    short       life;       /* current HP */
    short       max_life;   /* HP cap (drawn as red bar overlay) */
    short       faint;      /* knockdown threshold */
} PARAM;
```

`life` ticks down when she takes damage; `faint` controls how much
damage triggers a `KNOCKDOWN` state. `defends[4]` is per-direction
(front/back/left/right) defence reduction — Meryl can block from
the front but not when shot in the back.

## VISION — the awareness cone

```c
typedef struct _VISION {
    short facedir;     /* PSX 4096-fixed angle of cone center */
    short angle;       /* half-cone width */
    short length;      /* PSX world units, max distance */
    short field_06;
} VISION;
```

Used in `check.c` to test whether a target (Snake or a guard) is
inside Meryl's vision cone. If yes, she'll react (call out
"Snake!", aim her gun, etc.).

## Meryl72Pad — the per-tick input mirror

```c
typedef struct _Meryl72Pad {
    int   press;     /* bitmask of "input" events for this frame */
    int   mode;      /* current high-level mode */
    int   tmp;       /* scratch */
    short time;      /* mode-time elapsed */
    short dir;       /* desired facing */
    short sound;     /* sound trigger */
    short field_14;
} Meryl72Pad;
```

`pad.press` is the central state-input machine. `check.c` /
`think.c` set bits here based on world conditions; `ml72act.c`'s
`Meryl72ActionMain` reads them and decides what action to start
next.

Conceptually it's "if Meryl were a player, what would the player
press this frame?" The think functions translate world events into
synthetic pad input which the action dispatcher then consumes
identically to how snake.c handles real input.

## Lifecycle

### Factory: NewMeryl72

Spawned by GCL `chara &MERYL72 ...`. Calls the standard
`GV_NewActor → GV_SetNamedActor(Act, Die, "meryl72.c")` then
`Meryl72GetResources_800C7738`. Resource init order:

1. `GM_InitControl(&work->control, name, where)` — register CONTROL.
2. `GM_InitObject(body, BODY_DATA, BODY_FLAG, motion)` — load
   "meryl" KMD + the right OAR ("mel_07a" for stage 7, "mel_09a"
   for stage 9).
3. `GM_ConfigObjectJoint(body)` + `GM_ConfigMotionControl(body,
                          m_ctrl, motion, m_segs1, m_segs2,
                          control, rots)`.
4. `GM_InitObject(weapon, WEAPON_DATA, WEAPON_FLAG, 0)` — Desert
   Eagle KMD.
5. `GM_ConfigObjectRoot(weapon, body, 4)` — bind weapon to bone 4
   (right hand).
6. `NewShadow(...)` — drop shadow.
7. `NewGunLight_800D3AD4(&body->objs->objs[4].world, ...)` — gun
   tip light.
8. Patrol-node parsing — `n_patrols` and `nodes[32]` from the GCL
   `-n` option.
9. Voice-line table from `-s` GCL option.
10. PARAM init from `-l` (life), `-f` (faint), `-d` (defends).
11. `GM_SetTarget(target, ...)` — register her in the target system
    so other actors can hit her.
12. Initial action: `SetMode(work, FirstAction)` — entry pose.

### Per-tick: Meryl72Act

```c
void Meryl72Act_800C6D54(Meryl72Work *work)
{
    if (HASH_KILL received) { GV_DestroyActor; return; }

    s07c_meryl72_unk1_800CBCD8(work);    /* drain mesgs */

    GM_ActControl(&work->control);        /* integrate motion */
    GM_ActObject2(&work->body);           /* render pose */
    GM_ActObject2(&work->weapon);         /* render weapon */

    DG_GetLightMatrix2(&control->mov, work->light);

    /* think → check → action → put */
    s07c_meryl72_800C6AF8(work);          /* misc bookkeeping */
    RootFlagCheck_800C6B5C(work);         /* update damage roots */
    Meryl72ActionMain_800CBC44(work);     /* run think/action */

    target = work->target;
    GM_MoveTarget(target, &control->mov); /* keep her hit-target with her */
    GM_PushTarget(target);                /* register for collision queries */

    s07c_meryl72_800C6C48(work);          /* light matrix update + put fx */
    work->fC04++;
    meryl72_800D52F8 = work->control.mov; /* publish her pos for other code */
}
```

Note GM_ActControl runs **before** the AI dispatcher
(`Meryl72ActionMain`). This means the AI sees this-frame's
collision-corrected position. The pattern is identical to how
guards in `enemy/` work.

### ActionMain — the dispatcher

`ml72act.c::Meryl72ActionMain_800CBC44` is the per-tick brain:

```c
void Meryl72ActionMain(Meryl72Work *work)
{
    /* 1. Read inputs / world state. */
    CheckPad_800C8308(work);         /* fills work->pad.press */
    CheckDamage_800C7F6C(work);      /* if hit, sets pad.press damage bits */

    /* 2. Run "override" action (if one is queued). */
    if (work->action2) {
        ((ACTION)work->action2)(work, work->time2++);
    }

    /* 3. Run primary action — this is where state runs. */
    ((ACTION)work->action)(work, work->time++);

    /* 4. Action may have set work->action = NewState; if so, restart
     *    next tick at time=0 in the new state. */
}
```

Two parallel "tracks" — primary (`work->action`) and override
(`work->action2`). The override is for animations that play
*alongside* primary — e.g. throwing a grenade while continuing to
walk.

## State constants

`meryl72.h` defines ~58 ACTION_* constants. Notable named ones:

| Constant | Purpose |
| -------- | ------- |
| `STANDSTILL` (0) | Idle stand |
| `ACTION1..ACTION16` | Generic action slots — meanings inferred per file |
| `GRENADE` (7) | Grenade toss |
| `DANBOWLKERI` (17) | "Cardboard kick" — early gag where she kicks the box Snake hides in |
| `DANBOWLPOSE` (18) | Pre-kick stance |
| `ACTION19..ACTION58` | More slots |

The integer values index into `m_segs1[17]` (modulo bucket math) to
pick which animation segment plays. The numeric naming is because
many of these actions have unclear narrative meaning — they do play
when triggered, but the high-level "what is Meryl doing?" wasn't
matched by the decompiler.

## Forward-declared functions named by address

`meryl72.h` lists ~30 `s07c_meryl72_unk1_800CXXXX` extern decls.
These are action / check / override callbacks where the high-level
purpose is known (it's an action) but the *narrative function*
isn't. Calling code refers to them by name; an animator could
identify each by playing the game and watching when they fire.

These are the prime candidates for renaming — see
[_unreversed.md](_unreversed.md).

## Targeting + combat

Three TARGET registrations:

- **`work->target`** — her hit-target. When something attacks her
  (Snake's gun, a guard's bullet), the targeting system reports
  the hit through this.
- **`work->target2`** — her "I'm pushing into something" target.
  Used for soft body collision — if Snake walks into her, she
  reports a contact.
- **`work->punch`** — her melee target. Activated only when the
  punch / kick action is firing.

`GM_PushTarget(target)` is called every frame to keep the targeting
system aware of her current pos. `GM_MoveTarget(target, &mov)`
syncs the position before the push.

## think.c vs think9.c

`think.c` covers stage 7c (the bathroom escape — Meryl follows
Snake to safety). The behaviour:

- Stay within ~3000 units of Snake.
- If a guard appears, draw weapon and engage.
- If knocked down, get up after a delay.
- Speak voice lines in response to script triggers.

`think9.c` covers stage 9a (the canyon — Meryl walks ahead, Snake
follows). Different behaviour:

- Walks the patrol nodes ahead of Snake.
- Wait at certain nodes for Snake to catch up.
- Voice lines triggered by node arrivals, not by combat.

These are separate files because the AI graphs differ enough that
sharing code would have made both messier. The dispatch is by
`work->stage`:

```c
if (work->stage == 9) {
    Meryl72Think9_<addr>(work);
} else {
    Meryl72Think_<addr>(work);
}
```

## override.c — scripted force-states

Override actions are how the cinematic system commandeers Meryl
mid-gameplay. When the GCL fires a `mesg &MERYL HASH_OVERRIDE proc`,
`override.c::Override*` installs an action into `work->action2` that:

1. Runs an authored animation (e.g. "look surprised").
2. Optionally plays a voice line.
3. After N ticks (`work->time2`), clears `work->action2` to restore
   AI control.

The override action exists in parallel with whatever the AI action
is doing — so Meryl can keep walking while reacting visibly to a
scripted event.

## put.c — visual effect spawns

Called from `s07c_meryl72_800C6C48` each tick to spawn
animation-driven effects:

- `ML72_PutBreath_800CB35C` — breath puff at the head bone.
- `ML72_PutBlood_800CB2EC` — blood when a damage event hit on this
  frame.
- `ML72_SetPutChar_800CB584` / `_ClearPutChar_800CB5CC` — install /
  remove a sub-effect actor that lives across multiple frames.

Effect spawn timing is keyed off `m_ctrl->info1.frame` (the
animation's playhead) — e.g. blood spawns only on the frame when
the damage animation hits its peak.

## Common pitfalls

### Meryl walks through walls

Her `control.step_size` defaults to ~450 (~21-unit collision
radius, same as standard humanoids). If a script overrode this
during a cutscene, she may have residual `step_size = 0` after the
cutscene ends. Check the scenario's `mesg` handlers.

### Meryl freezes mid-action

`work->action` is a fn pointer — if it gets clobbered to NULL,
`Meryl72ActionMain` crashes on the call. The dispatcher has a
NULL check (returns early); the symptom is "she stands still
forever". Cause: an action callback wrote NULL into
`work->action` instead of `SetMode(work, NextAction)`.

### Meryl shoots the wrong direction

`work->target` (her hit-target) and `work->punch` (melee) live in
different spaces. The aim direction is from `vision.facedir` not
from the target — if her vision cone hasn't acquired the enemy,
she'll fire forward not at them. `check.c::ReviseReadyGun` is the
function that updates `vision.facedir` based on `work->target`'s
pose.

## Stages Meryl appears in

| Stage | File | Behaviour |
| ----- | ---- | --------- |
| s07c | think.c | Bathroom escape — follows Snake, fights guards |
| s09a | think9.c | Canyon — walks ahead, Snake follows |
| (cinematics) | `chara/others/` | Cinematic-only Meryl (DEMODOLL-style) — separate code |

The stage check happens in `Meryl72GetResources` which inspects
`GV_StrCode(GM_CurrentMap)` and sets `work->stage = 7` or `9`.

## See also

- [`source/enemy/`](../../../../source/enemy/) — the canonical
  guard-AI pattern Meryl's think/action/check loop is modelled on.
- [`source/chara/snake/`](../../../../source/chara/snake/) — Snake
  uses the same `MOTION_CONTROL`/action-fn-ptr pattern, just with
  player input instead of synthetic AI input.
- [`port/doc/source/03-control-and-motion.md`](../03-control-and-motion.md) —
  the engine primitives.
- [`source/game/target.c`](../../../../source/game/target.c) — the
  TARGET system every combat actor uses.
- [`source/game/homing.c`](../../../../source/game/homing.c) —
  HOMING, the aim-assist Meryl's `work->hom` points at.
