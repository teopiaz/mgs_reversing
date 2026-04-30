# `source/libgv/` — engine backbone

The runtime backbone — actor system, memory pools, asset cache,
pad input, message bus, math helpers. Everything in `source/`
sits on top of this.

## Files

### Actor system
| File | Role |
| ---- | ---- |
| [`actor.c`](../../../../source/libgv/actor.c) | The actor list — `gActorsList_800ACC18[9]`, `GV_NewActor`, `GV_DestroyActor`, `GV_ExecActorSystem`, `GV_SetNamedActor`. |
| [`gvd.c`](../../../../source/libgv/gvd.c) | The "gvd daemon" — actor at level 0 that processes `LoadReq` etc. |
| [`debug.c`](../../../../source/libgv/debug.c) | `GV_DumpActorSystem` — diagnostic walker. |

### Memory
| File | Role |
| ---- | ---- |
| [`memory.c`](../../../../source/libgv/memory.c) | `GV_Malloc`, `GV_Free`, `GV_InitMemorySystem`. Three pools: `PACKET0`, `PACKET1`, `NORMAL`. Bump-with-free-list allocator. |
| [`resident.c`](../../../../source/libgv/resident.c) | Resident-vs-transient bookkeeping — `GV_SaveResidentFileCache`, `GV_RestoreResident*`. |

### Cache
| File | Role |
| ---- | ---- |
| [`cache.c`](../../../../source/libgv/cache.c) | Asset cache — `GV_CacheSystem.tags[MAX_CACHE_TAGS]`, `GV_GetCache`, `GV_CacheID(name, ext)`. Linear-probe table. |

### Communication
| File | Role |
| ---- | ---- |
| [`message.c`](../../../../source/libgv/message.c) | `GV_SendMessage`, `GV_ReceiveMessage` — actor-to-actor mesg dispatch. |
| [`pad.c`](../../../../source/libgv/pad.c) | Pad-input ring buffer — `GV_PadData[2]`, `GV_PadMask`, `GV_OriginPad`. |

### Math
| File | Role |
| ---- | ---- |
| [`math.c`](../../../../source/libgv/math.c) | Fixed-point primitives — `GV_AddVec3`, `GV_SubVec3`, `GV_DotVec3`, `GV_DiffVec3`. |
| [`math_near.c`](../../../../source/libgv/math_near.c) | "Near*" interp helpers — `GV_NearTimeV`, `GV_NearExp4V`, `GV_NearTimePV`. |
| [`math_quat.c`](../../../../source/libgv/math_quat.c) | Quaternion ops (rare — used for some skeletal poses). |

### Naming
| File | Role |
| ---- | ---- |
| [`strcode.c`](../../../../source/libgv/strcode.c) | `GV_StrCode(name)` — 5-bit-rotate hash. The universal "name → 16-bit id" function. |

## Actor system

The single most important data structure in the engine:

```c
extern ActorList gActorsList_800ACC18[GV_ACTOR_LEVEL];   /* 9 levels */
```

Each level is a doubly-linked list of `GV_ACT` entries. Every
actor has:

```c
typedef struct GV_ACT {
    struct GV_ACT *prev, *next;
    void          *act;       /* per-tick callback */
    void          *die;       /* destructor callback */
    const char    *filename;  /* for GV_DumpActorSystem */
    int            level;     /* index into gActorsList_* */
    /* …actor's own state appended after this header */
} GV_ACT;
```

Spawning:

```c
work = GV_NewActor(GV_ACTOR_LEVEL5, sizeof(MyWork));
GV_SetNamedActor(&work->actor, MyAct, MyDie, "myfile.c");
```

Per-tick:

```c
void GV_ExecActorSystem(void) {
    for (level = HIGH; level >= 0; level--) {
        for (each actor in gActorsList[level]) {
            actor->act(actor);
        }
    }
}
```

The HIGH→LOW walk is crucial — higher-level actors (cinematic
data writers at level 3) run before lower-level actors (camera
at level 2) within one tick.

## Cache

```c
#define MAX_CACHE_TAGS 128

typedef struct GV_CACHE_TAG {
    int   id;       /* (name_hash & 0xFFFF) | (ext_offset << 16) */
    void *ptr;      /* loaded data */
} GV_CACHE_TAG;

extern GV_CACHE_PAGE GV_CacheSystem;   /* tags[MAX_CACHE_TAGS] */
```

Encoding: `GV_CacheID(name, 'k')` = `name_hash | (('k' - 'a') << 16)`.

Linear probe by `id % MAX_CACHE_TAGS`. The `'k'` extension is
KMD; `'h'` is HZD; `'p'` is PCX; `'g'` is GCL bytecode; `'o'` is
OAR (animation).

The cache is populated by FS loaders (libfs registers a
per-extension callback). When a stage's DATACNF unloads, entries
get marked free; the resident cache (always-loaded init.dar
entries) survives.

## Memory pools

```
PACKET0 (~? KiB)   short-lived per-frame allocations (GPU packets)
PACKET1 (~? KiB)   alternate frame's packets (double-buffered)
NORMAL  (large)    long-lived allocations (actors, cached assets)
```

`GV_Malloc(size)` defaults to NORMAL. Specific allocators target
PACKET0/1 for per-frame work.

## Messages

```c
GV_SendMessage(target_name, message_array, count);
   /* leaves message in target's mailbox */

count = GV_ReceiveMessage(my_name, &out_messages);
   /* drains my mailbox, returns # messages */
```

Used pervasively — `mesg HASH_KILL`, `mesg HASH_MOVE2`, etc. Each
message is a 4-int payload. Targets are identified by name hash.

## Math helpers

`GV_NearTimeV` / `GV_NearExp4V` are the smoothing functions used
across the codebase for "interpolate this value toward target
over N frames". The `Near*` family:

- `NearTime(...)` — linear over time.
- `NearExp4(...)` — exponential decay (exp(-x/4)).
- `NearTimeP(...)` — periodic (handles 4096-fixed angle wraparound).

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [actor.md](actor.md) | The actor list, exec loop, lifecycle, port-only crash recovery |
| [memory.md](memory.md) | Three heaps, FREE/VOID/USED + movable-pointer trick, compaction |
| [cache.md](cache.md) | 128-tag table, ID encoding, loader registry, resident vs cache |
| [message.md](message.md) | Double-buffered message ring, run-length tracking, pause behaviour |
| [pad.md](pad.md) | GV_PAD struct, button-mode swap, analog modes, dir_table |
| [math.md](math.md) | math.c + math_near.c + math_quat.c + strcode.c |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [02-game-loop.md](../02-game-loop.md) — `GameWork` is the actor
  that drives stage transitions.
- [03-control-and-motion.md](../03-control-and-motion.md) — how
  actors use the CONTROL primitive.
- [`port/editor/ed_demo.c`](../../../../port/editor/ed_demo.c) —
  the editor calls `GV_ExecActorSystem` once per editor frame to
  drive Demo Play.
