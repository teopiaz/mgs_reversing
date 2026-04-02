# Actor System and GCL Scripting

## Actor System

### Priority Levels

The actor system uses 9 priority levels (0 through 8), each containing a doubly-linked list of actors. The levels are iterated in order during each frame tick. Each level has configurable pause and kill thresholds that control when actors in that level are suspended or destroyed during stage transitions.

| Level | Name               | Pause | Kill | Purpose                          |
|-------|--------------------|-------|------|----------------------------------|
| 0     | GV_ACTOR_DAEMON    | 0     | 7    | System daemons (GameWork, DG)    |
| 1     | GV_ACTOR_MANAGER   | 0     | 7    | Manager-level actors             |
| 2     | GV_ACTOR_LEVEL2    | 9     | 4    | Assist-level (ZOE1 naming)       |
| 3     | GV_ACTOR_LEVEL3    | 9     | 4    | Prepare-level                    |
| 4     | GV_ACTOR_LEVEL4    | 15    | 4    | Main actor level                 |
| 5     | GV_ACTOR_LEVEL5    | 15    | 4    | Secondary actor level            |
| 6     | GV_ACTOR_AFTER     | 15    | 4    | Post-update (refer level)        |
| 7     | GV_ACTOR_AFTER2    | 9     | 4    | Second post-update (pause level) |
| 8     | GV_ACTOR_DAEMON2   | 0     | 7    | System daemon tail (frame sync)  |

The pause field is a bitmask. If `(lp->pause & GV_PauseLevel) == 0`, actors in that level execute. The global `GV_PauseLevel` is set to:
- 0x0: no pause (normal gameplay)
- 0x1: codec active
- 0x2: game paused (menu open)

Daemon levels (0, 1, 8) have pause=0, so they always run regardless of pause state. Gameplay levels (4, 5, 6) have pause=15 (0xF), so they stop when any pause bit is set.

The kill field controls `GV_DestroyActorSystem(exec_level)`: all levels with `kill <= exec_level` have their actors destroyed. This is used during stage transitions to clean up gameplay actors (kill=4) while preserving daemons (kill=7).

### ActorList Structure

Each priority level is stored as an `ActorList`:

```c
typedef struct {
    GV_ACT first;   // sentinel head node (act/die = NULL)
    GV_ACT last;    // sentinel tail node (act/die = NULL)
    short  pause;   // pause bitmask
    short  kill;    // kill threshold
} ActorList;
```

The `first` and `last` are sentinel nodes that simplify insertion/removal (no NULL checks needed for list edges). Active actors are linked between first and last.

### GV_ACT Structure

```c
typedef struct _GV_ACT {
    struct _GV_ACT *prev;       // previous actor in list
    struct _GV_ACT *next;       // next actor in list
    GV_ACTFUNC      act;        // per-frame update callback
    GV_ACTFUNC      die;        // shutdown/destructor callback
    GV_FREEFUNC     free;       // memory deallocation callback
    const char     *filename;   // source filename string (for debug)
    int             runtime;    // cumulative execution time
    int             count;      // tick counter
} GV_ACT;
```

Most game actors embed `GV_ACT` as their first struct member. The actor's specific state follows immediately after, so `GV_ACT*` can be cast to the concrete actor type.

### Execution Loop: GV_ExecActorSystem

`GV_ExecActorSystem()` is the main game loop body, called once per frame. It iterates all 9 levels and, for each non-paused level, walks the linked list calling `actor->act(actor)` for every actor with a non-NULL act function.

```c
void GV_ExecActorSystem(void)
{
    for (i = GV_ACTOR_LEVEL; i > 0; i--) {
        if ((lp->pause & GV_PauseLevel) == 0) {
            actor = &lp->first;
            for (;;) {
                current = actor;
                next = current->next;
                if (current->act)
                    current->act(current);
                GM_CurrentMap = 0;
                actor = next;
                if (!next) break;
            }
        }
        lp++;
    }
}
```

Note: `GM_CurrentMap` is reset to 0 after each actor, preventing one actor's map context from leaking to the next.

### Crash Handler (REMOVED)

The port initially included a sigsetjmp/siglongjmp-based crash handler that caught SIGSEGV during actor execution. When an actor crashed, it was disabled (act set to NULL) and execution continued with the next actor. The signal handler and jmp_buf are still present in the source (`actor.c` lines 161-168) but the recovery path in `GV_ExecActorSystem` was removed so that crashes produce proper stack traces for debugging. The handler code should be fully removed once the port is stable.

### Actor Lifecycle

**Creation:**

1. `GV_NewActor(exec_level, size)` -- allocates `size` bytes from the GV memory pool via `GV_Malloc`, zeroes the memory, and calls `GV_InitActor` to insert the new actor at the tail of the specified level's linked list. The `free` callback is set to `GV_Free`.

2. `GV_SetNamedActor(actor, act_func, die_func, "filename.c")` -- assigns the act, die, and filename fields. The convenience macro `GV_SetActor(actor, act, die)` automatically fills in `__FILE__` for the filename.

**Destruction:**

- `GV_DestroyActor(actor)` -- deferred destruction. Sets `actor->act` to `GV_DestroyActorQuick`, so on the next iteration of the execution loop, the actor destroys itself.

- `GV_DestroyActorQuick(actor)` -- immediate destruction. Unlinks the actor from its list, calls `die()` if set, then calls `free()` if set.

- `GV_DestroyActorSystem(exec_level)` -- bulk destruction. Walks all levels where `kill <= exec_level` and destroys every actor. Used during stage transitions.

- `GV_DestroyOtherActor(actor)` -- searches all levels for the given actor pointer and schedules its destruction. Prints "#" to console if the actor is not found in any list.

### Key Actors

| Actor | Source File | Level | Role |
|-------|------------|-------|------|
| GameWork | source/game/gamed.c | DAEMON (0) | Master controller: drives stage loading, transitions, game state machine |
| DG_WorkFirst | source/libdg/dgd.c | DAEMON (0) | Frame sync start (PSX: VSync/DrawSync) -- NOT used in port |
| DG_WorkLast | source/libdg/dgd.c | DAEMON2 (8) | Frame sync end (PSX: queue draw commands) -- NOT used in port |
| CameraSystem | source/game/camera.c | LEVEL4 (4) | Camera positioning and following |
| Snake | source/chara/snake/sna_init.c | LEVEL4 (4) | Player character (8600+ lines, largest single actor) |
| Select | source/game/select.c | LEVEL4 (4) | Stage selection menu |
| Watcher | source/enemy/watcher.c | LEVEL4 (4) | Enemy soldier AI (sight, patrol, alert) |
| Command | source/enemy/command.c | LEVEL4 (4) | Enemy squad coordination |
| MenuMan | source/menu/menuman.c | AFTER (6) | Menu system manager |

In the port, DG_WorkFirst and DG_WorkLast are not created as actors. Their functionality (frame sync, draw command submission) is replaced by explicit calls in the main loop: `game_tick()` calls `GV_ExecActorSystem()` then manually invokes the software renderer.

---

## GCL Scripting System

### Overview

GCL (Game Command Language) is a bytecode scripting language used to initialize each stage. GCL scripts define which actors to spawn, where to place the camera, what maps to load, and how to configure game state. Scripts are compiled offline and loaded as 'g' cache entries from STAGE.DIR.

### Execution Model

GCL bytecode is a linear stream of commands, each identified by a hash code. The interpreter reads the command hash, looks it up in the `GCL_COMMANDLIST` dispatch table, and calls the corresponding C function. Commands receive their parameters by calling GCL_Get* functions that parse tagged values from the bytecode stream.

Key dispatch table (registered in `game_script_fix.c` / `source/game/script.c`):

| Command | Hash | Handler | Description |
|---------|------|---------|-------------|
| `map` | GCL hash | GM_Command_map | Create map geometry |
| `chara` | GCL hash | GM_Command_chara | Spawn an actor |
| `camera` | GCL hash | GM_Command_camera | Set camera position/target |
| `pad` | GCL hash | GM_Command_pad | Enable/disable input |
| `start` | GCL hash | GM_Command_start | Initialize font, menu, variables |
| `bind` | GCL hash | GM_Command_bind | Register trigger callbacks |
| `proc` | GCL hash | GCL_ExecProc | Call a named procedure |
| `func` | GCL hash | GCL_GetFunc | Query game state value |

### Parameter Parsing

GCL parameters are parsed via:
- `GCL_GetOption(tag)` -- check if an option with the given tag exists
- `GCL_GetNextValue()` -- read the next value from the current parameter
- `GCL_GetParamResult()` -- read a parameter and return its value
- `GCL_StrToSV(str, svector)` -- parse a string into an SVECTOR (x,y,z)

### Procedure System

GCL scripts define named procedures (procs) that can be called by hash ID. `GCL_ExecProc(proc_id)` looks up the proc's bytecode block and executes it. Procs are referenced by `bind` commands to set up trigger callbacks (e.g., when Snake enters a zone, call proc X).

### 64-bit Porting Issues

The GCL system required significant changes for 64-bit because PSX GCL stored pointers as 32-bit integers.

**Pointer table (`port/libgcl_fix/gcl_ptr_table.h`):**

On 64-bit, pointer values do not fit in `int`. The port introduces a 256-entry circular table `gcl_ptr_table[]` that stores full 64-bit pointers. When GCL needs to store a pointer as an int value, `gcl_store_ptr(p)` saves the pointer in the table and returns an index encoded as `0x7F000000 | idx`. To recover the pointer, `gcl_resolve_ptr(value)` checks for the `0x7F000000` marker and returns the corresponding table entry.

**NULL guards:**

Several GCL parsing functions could dereference NULL pointers on 64-bit due to different control flow from the pointer table system. NULL checks were added to:
- `GCL_GetNextValue` -- return 0 if current pointer is NULL
- `GCL_GetParamResult` -- return 0 if param data is NULL
- `GCL_GetOption` -- return 0 if option not found
- `GCL_StrToSV` -- return without writing if input string is NULL

**Proc lookup corruption (known bug):**

When `bind` registers a callback, the proc ID sometimes gets corrupted to `0x7F0000xx`. This happens because the bind handler stores a value that was encoded as a pointer table index, and subsequent lookups treat it as a raw proc ID. `get_proc_block` returns NULL for invalid IDs, and `GCL_ExecProc` has a NULL check to avoid crashing, but the bound callback never executes. This remains an open issue.

---

## Stage System

### Stage Definitions

There are 92 stage definition files in `source/stage/`, each named after a stage code (e.g., `s00a.c`, `s01a.c`, `d00a.c`). Each file defines a `_StageCharacterEntries[]` array that maps GCL hash codes to actor constructor functions:

```c
// Example: source/stage/s00a.c
STAGE_ENTRY _StageCharacterEntries[] = {
    { GV_StrCode("watcher"), (STAGE_FUNC)NewWatcherActor },
    { GV_StrCode("command"), (STAGE_FUNC)NewCommandActor },
    ...
    { 0, NULL }
};
```

Of the 92 stage files:
- **61** use proper function pointers (compile correctly on 64-bit)
- **31** use raw PSX addresses (e.g., `0x800ABCDE`) instead of function pointers. These stages have actors whose source has not been decompiled into named functions. On 64-bit, these addresses are meaningless and the corresponding actors will not spawn.

### Stage Loading Flow

1. `GM_LoadInitBin` (in `gamed.c`) maps the current stage hash to a symbol via a large switch statement
2. The filesystem loads the stage's data from STAGE.DIR (cache entries: 'g' for GCL, 'k' for KMD models, 'h' for HZD collision, 't' for textures)
3. The GCL 'g' script is executed, which runs `chara` commands to spawn actors
4. Each `chara` command looks up the actor constructor in `_StageCharacterEntries[]` by hash and calls it

### Overlay Compilation

On PSX, stage-specific code was loaded as relocatable overlays at runtime. In the port, all 88 overlay source files are compiled statically into the binary. Three R-variant overlays (d18ar, s08br, s19br) are excluded due to duplicate symbol conflicts with their non-R counterparts.

Overlay source lives in `source/overlays/` organized by stage code (e.g., `source/overlays/s00a/`, `source/overlays/s04c/`). Each overlay may contain unique actors specific to that stage (boss fights, cutscene logic, stage-specific puzzles).
