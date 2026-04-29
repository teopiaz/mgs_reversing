# `source/animal/` — non-player humanoids

Subsystem covering NPCs that aren't first-class enemies. Four
components, each implementing one disc character or character class:

| Component | Lines | Used in | Role |
| --------- | ----- | ------- | ---- |
| [doll/](doll.md)         | 1746 | All cinematic stages | Generic "puppet" — DEMODOLL chara, scripted-animation NPC. Two flavours (`Doll` for routed NPCs, `Demodoll` for cutscene puppeteering). |
| [meryl72/](meryl72.md)   | 5266 | s07c, s09a | Meryl as a *full* AI character in stages 7 and 9. Patrols, fights guards alongside Snake, follows scripted nodes, can take damage. |
| [zako11e/](zako11e.md)   | 4962 | s11e | Late-game guard (snowy passage). Uses the standard zako AI but with stage-specific tuning + a low-poly LOD swap. |
| [zako11f/](zako11f.md)   | 5075 | s11f | Sister stage variant — different action set + override path for the Liquid escape sequence. |

Total: ~17 KLOC of decompiled C; **0 functions left in assembly** (the
folder is fully decompiled). What's still under-documented is named in
each component's [`_unreversed.md`](_unreversed.md) page.

## Why these four ended up here vs `enemy/` or `chara/`

The codebase splits NPC code three ways for historical reasons:

- **`source/chara/`** — characters Snake spawns or rides
  (`snake/`, `hind2/`, `others/intr_cam.c`).
- **`source/enemy/`** — generic guards (WATCHER, COMMANDER, basic
  ZAKO patrols). Everything in `enemy/` is *re-used* across stages.
- **`source/animal/`** — characters that needed special-case logic
  for one or two stages and whose authoring team chose a separate
  per-character source file rather than extending the generic
  enemy code.

So `meryl72` is in animal not because she's an enemy but because she
needed her own AI in s07c/s09a. `zako11e` and `zako11f` are guards
specifically tuned for s11e/s11f and ship duplicate-but-different
copies of the standard zako pattern. `doll` is the catch-all puppet
the cinematic system uses; it doesn't fit anywhere else.

The *actual* shared base used by every guard (think → command →
action → check) is in `source/enemy/` — see future doc
`enemy.md` for the inheritance pattern. Each `animal/` component
*re-implements* that same pattern with different data tables.

## Common shape — every animal/ component

All four components follow the same skeletal layout. If you've read
one, the others are 95% the same:

```
component_dir/
├── <name>.c          // factory + Act dispatcher + lifecycle
├── <name>.h          // <Name>Work struct + state constants
├── action.c          // ACTION_* state handlers (one per anim/state)
├── think.c           // AI brain — picks next action from world state
├── check.c           // Per-frame condition tests (target visible?
│                     //                            took damage?
│                     //                            heard noise?)
├── command.c (or _com.c) // Combat / dispatch helpers shared by think
├── put.c             // Visual effects spawned per-tick (breath,
│                     // muzzle flash, blood markers)
└── override.c        // State overrides (cutscenes, scripted moments)
```

The `<Name>Work` struct is the actor's per-instance state. Every
animal struct has the same first three members:

```c
typedef struct <Name>Work {
    GV_ACT          actor;        // engine actor header
    CONTROL         control;      // position / collision (game/control.c)
    OBJECT          body;         // KMD + bone hierarchy (game/object.c)
    MOTION_CONTROL  m_ctrl;       // animation player (game/motion.c)
    /* ... per-character state ... */
} <Name>Work;
```

— so calling `GM_ActControl(&work->control)` /
`GM_ActMotion(&work->body)` works identically across components, and
the GCL `mesg` system finds these actors by `control->name` regardless
of type.

## The think → command → action → check loop

Every animal NPC's per-frame `Act()` follows this pattern:

```c
static void Act(Work *work)
{
    /* 1. Drain incoming GV messages (HASH_KILL, HASH_MAP, etc.). */
    if (GM_CheckMessage(&work->actor, work->control.name, HASH_KILL)) {
        GV_DestroyActor(&work->actor);
        return;
    }
    handle_messages(work);

    /* 2. CHECK — read world state into `work->pad` and friends.
     *    Does the actor see the player? Hear noise? Take damage? */
    Check(work);

    /* 3. THINK — based on world state, decide which ACTION to be in. */
    Think(work);

    /* 4. ACTION — run the per-state callback (function pointer).
     *    Sets work->control.step / .turn / .interp + plays animation. */
    work->action(work, work->time++);

    /* 5. ENGINE — integrate motion + collision + render. */
    GM_ActMotion(&work->body);
    GM_ActControl(&work->control);
    GM_ActObject(&work->body);

    /* 6. PUT — spawn visual effects keyed off animation frames. */
    Put(work);
}
```

`work->action` is a function pointer — switching state means writing
a different `ACTION_*` callback into it. `SetMode(work, NewAction)`
is the helper used everywhere; it also clears `work->time` so the
new action sees `time=0` on its first tick.

This indirection-by-function-pointer is why every component has an
`action.c` with dozens of small handler fns named
`ActSomething_<addr>`. Each one is one **state**: walking, aiming,
firing, hit-stagger, knocked-down, getting-up, dying.

## Reading order

1. **[doll.md](doll.md)** first — it's the simplest (no real AI;
   purely scripted) and introduces the per-component file shape.
2. **[zako11e.md](zako11e.md)** — adds the full think/check/action
   loop without too much stage-specific weight.
3. **[zako11f.md](zako11f.md)** — variant of zako11e. Compare
   side-by-side to see what's per-stage tunable.
4. **[meryl72.md](meryl72.md)** — the most elaborate component;
   she's an ally, not an enemy, and has unique mechanics
   (target-following Snake, danbowl-keri kick, grenade actions,
   patrol nodes).
5. **[_unreversed.md](_unreversed.md)** — what's still opaque in
   each component (function names by address, struct fields named
   `fXXX`, ambiguous flag bits, etc.).

## See also

- [`source/game/control.c`](../../../../source/game/control.c) +
  [`motion.c`](../../../../source/game/motion.c) — the engine-tier
  primitives every animal struct uses. Documented in
  [03-control-and-motion.md](../03-control-and-motion.md).
- [`source/enemy/`](../../../../source/enemy/) — the canonical
  zako/watcher/commander code. animal's zakos diverged from this.
- [`source/chara/snake/`](../../../../source/chara/snake/) — Snake
  himself. Same pattern, much larger (he's the player).
- [`port/doc/demo/03-key-actors.md`](../../demo/03-key-actors.md) —
  for the cutscene side of `doll/demodoll.c` (DEMODOLL chara).
