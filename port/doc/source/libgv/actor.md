---
file: source/libgv/actor.c
---

# `libgv/actor.c` — the Actor System

The actor system is the **single execution-loop primitive** the entire
engine is built on. Every game-side update — whether the player, an
enemy, an effect, the camera, the menu, the save manager — is an
*actor* with a function pointer that gets called once per frame, in
priority order.

There is no other kind of update path: if it runs every frame, it's
an actor.

## Data structures

### `GV_ACT` — one actor

```c
typedef struct _GV_ACT {
    struct _GV_ACT *prev;       // doubly-linked list pointers
    struct _GV_ACT *next;
    GV_ACTFUNC      act;        // tick callback (NULL = "scheduled to die")
    GV_ACTFUNC      die;        // shutdown callback
    GV_FREEFUNC     free;       // memory-free callback (defaults to GV_Free)
    const char     *filename;   // __FILE__ where it was created (for debug dump)
    int             runtime;    // µs spent in act() this frame (debug)
    int             count;      // tick count (debug)
} GV_ACT;
```

Every "actor work" struct in the codebase begins with `GV_ACT actor;`
as its first field. That's the contract: pass the work struct's
address to `GV_NewActor` / `GV_SetNamedActor` and the engine will
treat the prefix as the actor descriptor.

### `ActorList` — one execution tier

```c
typedef struct {
    GV_ACT first;       // sentinel head
    GV_ACT last;        // sentinel tail
    short  pause;       // bitmask checked against GV_PauseLevel
    short  kill;        // priority at which DestroyActorSystem culls
} ActorList;
```

There are 9 ActorLists, one per priority tier (`GV_ACTOR_DAEMON`,
`GV_ACTOR_MANAGER`, `GV_ACTOR_LEVEL2..5`, `GV_ACTOR_AFTER`,
`GV_ACTOR_AFTER2`, `GV_ACTOR_DAEMON2`).

The default tuning per tier (`gPauseKills_8009D308`):

| Tier | pause | kill | Used by |
| ---- | ----- | ---- | ------- |
| 0 GV_ACTOR_DAEMON | 0 | 7 | Always-on system actors (sound, cd, vibration) |
| 1 GV_ACTOR_MANAGER | 0 | 7 | gamed.c (the master) |
| 2 GV_ACTOR_LEVEL2 | 9 | 4 | (rare) |
| 3 GV_ACTOR_LEVEL3 | 9 | 4 | (rare) |
| 4 GV_ACTOR_LEVEL4 | 15 | 4 | Player, enemies, important characters |
| 5 GV_ACTOR_LEVEL5 | 15 | 4 | Effects, support actors, items |
| 6 GV_ACTOR_AFTER | 15 | 4 | Post-physics fixups |
| 7 GV_ACTOR_AFTER2 | 9 | 4 | (rare) |
| 8 GV_ACTOR_DAEMON2 | 0 | 7 | Late daemons (memory cleanup) |

A `pause` mask of 0 means "never pause" — sound + system tiers keep
running through codec calls and pause menus. Mask 15 means "pause
when any of bits 0..3 are set in `GV_PauseLevel`" — gameplay tiers
freeze during pause and codec.

## The execution loop — `GV_ExecActorSystem`

Called once per frame from `gamed.c`'s outer loop (after V-sync,
before render submission). Walks all 9 lists from highest to lowest
tier:

```c
for (i = GV_ACTOR_LEVEL; i > 0; i--) {
    if ((lp->pause & GV_PauseLevel) == 0) {
        // tier is active — run all its actors
        for each actor in list:
            actor->act(actor)
    }
    lp++;
}
```

Note the **descending** iteration: tier 8 (DAEMON2) runs first, tier
0 (DAEMON) last. Most actors register at LEVEL4/5 so they run before
DAEMON, which gets the final word (e.g. memory compaction).

### Crash recovery (PORT_BUILD)

The port adds a `sigsetjmp` / `siglongjmp` envelope around each
`actor->act()` call (lines 161–235 of [actor.c](../../../../source/libgv/actor.c)).
Without this, a single bad pointer in any decompiled actor would
SEGV the whole process — common during reverse-engineering when a
struct field is mistyped. The port:

1. Installs a SIGSEGV / SIGBUS handler that captures `info->si_addr`.
2. Wraps each actor with `sigsetjmp(actor_jmp, 1)`.
3. On fault, longjmps back, logs the actor's `filename` + `act`
   pointer + fault address — once per (file, fn) pair so a
   permanently broken actor doesn't drown the log.

Original PSX has none of this; faults trip the MTS exception
handler.

## Lifecycle

### Birth — `GV_NewActor` / `GV_SetActor`

Two-step pattern, used uniformly:

```c
WatcherWork *work = GV_NewActor(GV_ACTOR_LEVEL4, sizeof(WatcherWork));
GV_SetActor(work, WatcherAct, WatcherDie);  // expands using __FILE__
```

`GV_NewActor` allocates from the normal heap (`GV_Malloc`), zeroes,
links into the chosen tier's list, and sets `act.free = GV_Free`.
The work struct is now linked but *inactive* — `act` is still NULL.

`GV_SetActor` (a macro for `GV_SetNamedActor` with `__FILE__`)
populates the act/die callbacks. Once `act` is non-NULL the actor
runs at the next `GV_ExecActorSystem`.

### Death — three flavours

| API | Semantics |
| --- | --------- |
| `GV_DestroyActor(actor)` | Set `actor->act = GV_DestroyActorQuick` — the *next* tick will unlink + die + free. Safe inside an actor's own act(). |
| `GV_DestroyActorQuick(actor)` | Unlink, call die, call free **immediately**. Only safe outside the actor list walk. |
| `GV_DestroyActorSystem(level)` | Cull every actor whose tier `kill <= level`. Used at stage transitions; `level=4` clears gameplay actors but leaves daemons. |

`GV_DestroyActor` is the canonical "I'm done now" call. By deferring
to the next tick, an actor can safely call it from inside its own
`act()` — the function returns normally, the executor walks to the
next actor in the list, and on the *following* frame it sees that
this slot's act is now `GV_DestroyActorQuick` and runs the cull.

### Free callback override

The default `actor->free` is `GV_Free`. Some actors (e.g. those
allocated from a slab pool) override `free` after `GV_InitActor`:

```c
GV_InitActor(GV_ACTOR_LEVEL5, raw, my_pool_free);
```

`my_pool_free(raw)` is called instead of `GV_Free(raw)` during
`GV_DestroyActorQuick`.

## `GV_DestroyOtherActor` — destroy by pointer

Locates an actor across all 9 lists by pointer comparison and
schedules it. Used when one actor wants to destroy another (e.g.
`watcher.c::s07a_meryl7_800D5B28` calls
`GV_DestroyOtherActor(work->field_AF8)` to reap the gunlight).

## `GM_CurrentMap` reset

After every `act()` call, `GM_CurrentMap = 0` is written. This is a
defensive reset: `GM_CurrentMap` is the "which HZD is currently
bound" pointer, set by control.c whenever an actor's collision pass
swaps maps. Clearing it after each actor forces the next actor to
re-bind, preventing stale-map collision queries.

## Pitfalls

- **Don't call `GV_DestroyActorQuick` from inside an act() callback.**
  It unlinks the actor, but the executor's iterator is holding the
  *old* `next` pointer — it'll either skip the successor or crash.
  Always use `GV_DestroyActor` from inside an act(); use Quick only
  from system code (e.g. tier-wide tear-down).
- **Don't store the actor's `prev`/`next` pointers.** They mutate
  every time another actor at the same tier registers/dies.
- **Don't update the same actor from two paths.** There's no
  reentrancy: an actor's act() runs exactly once per frame from
  `GV_ExecActorSystem`. Don't call it from a message handler or
  another actor's act().
- **First field MUST be `GV_ACT`.** The list manipulation reads the
  prev/next pointers off the work-struct's prefix.

## See also

- [02-game-loop.md](../02-game-loop.md) — `gamed.c` is the canonical
  manager-tier actor and drives `GV_ExecActorSystem` itself.
- [memory.md](memory.md) — heap behind `GV_NewActor`.
- [`source/include/`](../../../../source/include/) — the GV_ACT
  header is in `libgv.h`; many actor work-structs are in
  per-system headers.
