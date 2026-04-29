# `animal/zako11f/` — late-game guard variant (s11f / s11i)

Sibling of [zako11e](zako11e.md). Same ZakoWork base, same
think/check/action/command pattern, but with stage-specific tuning
for **s11f** (the corridor leading to the underground cell where
Liquid breaks Snake out) and **s11i** (the related stage layouts).

The naming is `zako11f` even though it shows up in s11i. That's
because the original team's source-tree organisation tracked the
*authoring stage* for each enemy variant, and this guard was first
implemented for s11f then re-used in s11i. Same pattern as
zako11e being s11e-named.

Total: 5075 lines / 6 files. Fully decompiled.

## File map

| File | Lines | Role |
| ---- | ----- | ---- |
| [`zako11f.c`](../../../../source/animal/zako11f/zako11f.c) | 573 | Factory + Act + Init/Die. Almost 1:1 with `zako11e.c` apart from naming. |
| [`action.c`](../../../../source/animal/zako11f/action.c) | 2001 | Action callbacks — the largest file. Different action set from zako11e (no LOD swap; different damage/recovery curves). |
| [`zk11fcom.c`](../../../../source/animal/zako11f/zk11fcom.c) | 985 | Combat/dispatch helpers. Bullet trajectories, target selection, comm propagation. |
| [`zk11fact.c`](../../../../source/animal/zako11f/zk11fact.c) | 382 | A *second* action file — small. Holds the dispatcher (`Zako11FActionMain`) plus a few late-added actions. |
| [`override.c`](../../../../source/animal/zako11f/override.c) | 789 | Scripted-moment overrides. Larger here than in zako11e because s11f has more cinematic interruptions (Liquid bursts in, Snake escapes, etc.). |
| [`put.c`](../../../../source/animal/zako11f/put.c) | 345 | Per-tick visual effect spawns. |

## Differences from zako11e

The two are 80% identical at the C level — same struct, same Act
shape, same comm-state pattern. The differences track the stage's
different needs:

### What zako11f doesn't have

- **No LOD swap** (`s11e_zako11e_800D34D0` and friends). s11f
  doesn't have first-person sequences with many guards on screen,
  so the perf optimisation isn't needed. The KMD swap path is
  absent entirely.
- **No `param_low_poly` field handling** in init. The GCL `-i`
  option is parsed differently here.

### What zako11f has that zako11e doesn't

- **Larger `override.c`** — s11f / s11i scripts force the guard
  into specific poses for cinematic moments (e.g. when Liquid
  breaks in, all guards play a "salute" override). Zako11e has
  inline override logic; zako11f uses a dedicated file.
- **Distinct `zk11fact.c`** — zako11e folded its dispatcher into
  `zk11eaction.c`. Zako11f keeps the dispatcher in its own file
  for clarity (perhaps because the team chose to migrate it
  later).
- **More patrol-node options**. The `-n` GCL option for patrol
  nodes accepts different formats here (`ReadNodes_800C8E4C` is
  the parser).

### Tuning constants

- **Health / faint defaults**: s11f guards die faster than s11e —
  `param_life` default is lower, `param_faint` more sensitive.
- **Vision cone**: s11f has narrower corridors so vision length
  is reduced (~3500 instead of ~5000).
- **Voice line set**: different `voices[]` table — s11f uses
  the "underground bunker" voice cluster.

## Zako11FWork — the state struct

A near-copy of `ZakoWork` but laid out in `enemy/enemy.h` under a
slightly different macro path. The fields are the same up to the
LOD-swap area (which zako11f omits):

```c
typedef struct Zako11FWork {
    GV_ACT          actor;
    CONTROL         control;
    OBJECT          body;
    MOTION_CONTROL  m_ctrl;
    MOTION_SEGMENT  m_segs1[N];
    MOTION_SEGMENT  m_segs2[N];
    SVECTOR         rots[32];

    OBJECT          weapon;
    MATRIX          light[2];

    void           *action;
    void           *action2;
    int             time;
    int             time2;

    TARGET         *target;          /* hit-target */
    TARGET          field_904;       /* attack-target (Snake) */
    TARGET          field_94C;       /* touch-target */

    /* …PARAM, VISION, pad, shadow, glight as zako11e */

    /* No def/kmd LOD pair — zako11f always uses the full mesh. */
    int             visible;
    int             scale;
    int             faseout;

    WatcherUnk      unknown;
    int             field_B74;
    int             local_data;

    /* …more fields, mostly identical to ZakoWork */
} Zako11FWork;
```

## NewZako11F — the factory

[`zako11f.c:564`](../../../../source/animal/zako11f/zako11f.c#L564):

```c
void *NewZako11F(int name, int where, int argc, char **argv)
{
    Zako11FWork *work = GV_NewActor(GV_ACTOR_LEVEL4, sizeof(Zako11FWork));
    if (work) {
        GV_SetNamedActor(&work->actor,
                         ZAKO11FAct_800C88AC,
                         ZAKO11FDie_800C8E2C,
                         "zako11f.c");
        Zako11FGetResources_800C9070(work, name, where);
    }
    return work;
}
```

`Zako11FGetResources_800C9070` at line 385 (the longest single fn
in zako11f.c) parses every GCL option and inits resources. It
notably:

- Always loads the standard guard KMD ("zako11f" or stage-specific).
- Skips the LOD path entirely.
- Reads patrol nodes via `ReadNodes_800C8E4C` (line 275).
- Initialises 3 TARGETs via `InitTarget_800C8A10`.
- Calls `s11i_zako11f_800C8B3C` to zero the comm-state struct.

Note unlike zako11e/zako11f.c's `NewZako11F` doesn't return -1 on
init failure — it always returns the work pointer even if init
errored (zakoes that failed init render but never tick correctly).
This is a known quirk; see `_unreversed.md`.

## ZAKO11FAct — the per-tick driver

```c
void ZAKO11FAct_800C88AC(Zako11FWork *work)
{
    /* HASH_KILL handler */
    if (GM_CheckMessage(...)) { GV_DestroyActor; return; }

    RootFlagCheck_800C86F0(work);     /* (no-op stub same as zako11e) */
    s11i_zako11f_800C8774(work);      /* visibility/group state update */

    if (!work->faseout) {
        Zako11FPushMove_<addr>(work);  /* think/select action */
        GM_ActControl(&work->control);
        GM_ActObject2(&work->body);
        GM_ActObject2(&work->weapon);
        DG_GetLightMatrix2(&work->control.mov, work->light);

        Zako11FActionMain_<addr>(work); /* run current action callback */

        GM_MoveTarget(work->target, &work->control.mov);
        GM_PushTarget(work->target);

        /* Touch-target maintenance (same as zako11e) */
    }

    /* No LOD-swap call. */
    *work->glight_enable = 0;
    *work->shadow_enable = 0;

    /* Cross-zako kill sync (same pattern as zako11e). */
    if (s11i_zako_command_state == 0xF && my_command_entry.field_04 == 1)
    {
        GV_DestroyActor(&work->actor);
    }
}
```

## Action / Think / Check / Command split

Same structure as zako11e:

- `action.c` + `zk11fact.c` — every state callback. ~50 actions
  for patrol/alert/combat/damage. The dispatcher itself
  (`Zako11FActionMain`) lives in `zk11fact.c` rather than
  `action.c`.
- `zk11fcom.c` — combat helpers (bullet spawn, LOS, voice).
  Notable: more `mesg`-based event dispatch than zako11e because
  s11f scripts cue more guards-react-together moments.
- (no separate `think.c` or `check.c` here) — the think/check
  logic lives partly in `zk11fcom.c` and partly inlined into the
  action callbacks. This is the team's later style; zako11e's
  separation is the older convention.
- `override.c` — large because of the cinematic-heavy nature of
  s11f. Each override fn pushes an action into `work->action2`
  with a specific frame budget; the AI proceeds normally on
  `work->action` underneath.

## override.c — the scripted-moment system

The override layer in zako11f is much more developed than in
zako11e. Three categories of override:

### Salute / hostage poses

When Liquid or Ocelot makes a dramatic entrance, all guards in
the room enter a salute pose. The override callback:

1. Sets `work->action2` to `OverrideSalute_<addr>`.
2. The callback plays the salute animation for N ticks.
3. Locks `work->control.step = 0` so the guard doesn't move.
4. After N ticks, clears `work->action2` and the AI resumes.

### Behind-bars / cell guards

The cell guards in s11f spawn with an immediate "leaning against
wall" override. The override is permanent — never cleared. AI
still runs underneath but actions like "patrol to next node" are
no-ops because of `pos = lock`.

### Forced retreat

When a script wants a guard to flee (Liquid scares them off), the
override is a Walk to a specific waypoint with combat actions
suppressed.

## ReadNodes — the patrol-path parser

[`zako11f.c:275`](../../../../source/animal/zako11f/zako11f.c#L275):

```c
int ReadNodes_800C8E4C(Zako11FWork *work)
{
    char *opt = GCL_GetOption('n');
    if (opt == NULL) return 0;

    /* parse a sequence of (x,y,z) triples — up to 16 nodes */
    int n = 0;
    do {
        if (n >= 16) break;
        s11i_zako11f_800C8EE8(opt, &work->nodes[n].vx);
        opt = GCL_GetParamResult();
        s11i_zako11f_800C8EE8(opt, &work->nodes[n].vy);
        opt = GCL_GetParamResult();
        s11i_zako11f_800C8EE8(opt, &work->nodes[n].vz);
        opt = GCL_GetParamResult();
        n++;
    } while (opt);

    work->n_patrols = n;
    return n;
}
```

Differs from zako11e (which used HZD route slots + `-r` option)
in that zako11f reads inline coords from the GCL — designer
flexibility at the cost of bytecode size.

`s11i_zako11f_800C8EE8` and `s11i_zako11f_800C8F40` are tiny
parsers (parse a `b:N` byte literal vs a 16-bit int literal).

## init helper — `s11i_zako11f_800C8F98`

[`zako11f.c:338`](../../../../source/animal/zako11f/zako11f.c#L338):

```c
int s11i_zako11f_800C8F98(Zako11FWork *work)
{
    /* fills work->fXXX[] with constants from s11i_dword_800C32F0[8] */
    for (i = 0; i < 8; i++) {
        work->???[i] = s11i_dword_800C32F0[i];
    }
    /* …more init… */
}
```

Initialises an 8-element timing/threshold table from a stage-private
constants array. The constants are used by combat actions
(grenade-throw timing, fire-rate gating) and weren't recovered
to named members on the struct yet — see `_unreversed.md`.

## What's `s11i_dword_800C32F0[8]`?

```c
int s11i_dword_800C32F0[8] = { … };  // local constants
```

A stage-private constants table. Eight 32-bit values seeded into
the work struct at init. Unknown semantic — they're the kinds of
"animation timing", "alert-cooldown ticks", "vision threshold"
values the action callbacks read every frame, but no member name
has been recovered.

## Common patterns

### Faseout flow

A guard dying:

1. Action callback for damage sets `work->faseout = 1`.
2. From next tick: Act skips Push/Action/Target updates.
3. Body fades out (alpha ramp via `scale` field).
4. Comm system notifies peers via `WatcherUnk`.
5. Cross-zako kill check eventually `GV_DestroyActor`s the work.

### Override release

When a script wants to end an override:

```c
mesg <guard_name> <HASH_OVERRIDE_END> 0
```

The mesg-handler in `zako11f.c` clears `work->action2`. AI takes
back control next frame.

### Group alert propagation

```c
/* In zk11fcom.c */
WatcherCommand_<addr>(work, action_id);
/* sets work->unknown.last_set = action_id, broadcasts to comm group */
```

When this fires, peer guards' next-tick `Check` see `last_set` and
their `Think` reacts. Same as zako11e — the comm system is shared.

## Dead-code candidates

A few functions in `action.c` are referenced only by name (forward
decls) and never called. These are likely dev-time stubs the team
left in. Listed in `_unreversed.md` for completeness.

## Stages used

| Stage | Notes |
| ----- | ----- |
| s11f  | Primary use — corridor + cell scenes. |
| s11i  | Sister stage — re-uses zako11f code with slightly different scripted moments (override.c covers both). |

The stage check happens implicitly via the GCL — both stages
spawn `chara &ZAKO11F`, and the chara factory is the same
`NewZako11F`.

## Common pitfalls

(The same set as zako11e, plus:)

### "Guard ignores my mesg"

Override mesgs route through `s11i_zako11f_800C8DB8` (mesg drain)
to `work->action2`. If that function isn't called this tick (e.g.
because `faseout = 1`), the override is dropped. Mesgs must be
sent before the guard starts dying.

### Nodes parsing fails silently

If the `-n` GCL option has fewer than 3 values per node,
`ReadNodes_800C8E4C` reads garbage from the next directive. The
node count is right but the coordinates are bogus. Always pass
3-tuples.

## See also

- [zako11e.md](zako11e.md) — sibling component. Compare them for
  the per-stage tuning patterns.
- [`source/enemy/`](../../../../source/enemy/) — base zako/watcher
  AI both diverged from.
- [meryl72.md](meryl72.md) — the canonical full-AI animal
  example.
- [_unreversed.md](_unreversed.md) — what's still by-address.
