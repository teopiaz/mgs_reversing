# Actor System and GCL Scripting

## 1. Actor System Overview

The MGS engine uses an **actor model** where every game entity (Snake, enemies, cameras,
doors, items, effects, HUD elements) is an actor with `act()`, `die()`, and `free()`
callbacks. Actors are organized into 9 priority levels that execute sequentially each
frame.

### 1.1 Priority Levels

```
Level  Name          Pause  Kill   Purpose
-----  ----          -----  ----   -------
  0    DAEMON         0      7     System daemons (never paused/killed)
  1    MANAGER        0      7     System managers
  2    LEVEL2         4      4     Early game logic
  3    LEVEL3         9      4     Main game logic (enemies, items)
  4    LEVEL4        15      4     Late game logic
  5    LEVEL5         9      4     Additional game logic
  6    AFTER          9      4     Post-update (camera, effects)
  7    AFTER2        15      4     Late post-update
  8    DAEMON2        0      7     Post-frame daemon
```

**Pause threshold**: If `(level->pause & GV_PauseLevel) != 0`, all actors in that
level are skipped. This is how codec (pause=1) and menu (pause=2) freeze gameplay.

**Kill threshold**: When `GV_PauseLevel >= level->kill`, all actors in the level are
destroyed. Used during stage transitions to clean up game objects.

### 1.2 GV_ACT Structure

```c
typedef struct GV_ACT {
    GV_ACT  *prev;          // Doubly-linked list
    GV_ACT  *next;
    void   (*act)(GV_ACT*); // Per-frame update callback
    void   (*die)(GV_ACT*); // Destruction callback
    void   (*free)(GV_ACT*);// Memory free callback
    char    *name;          // Debug name (e.g., "enemy.c")
    int      runtime;       // Ticks since creation
    int      count;         // Execution count
} GV_ACT;
```

### 1.3 Actor Lifecycle

```
Creation:
  GV_NewActor(level, size)
    +-- GV_AllocMemory(size)
    +-- Insert into level's linked list
    +-- Return pointer to work area

Each Frame:
  GV_ExecActorSystem()
    +-- For each level 0-8:
         +-- If (level->pause & GV_PauseLevel) != 0: skip
         +-- For each actor in level:
              +-- actor->act(actor)   // Game logic
              +-- actor->runtime++

Destruction (deferred):
  GV_DestroyActor(actor)
    +-- actor->act = GV_DestroyActorQuick  // Replace act with cleanup
    +-- Next frame: GV_DestroyActorQuick() calls die() then unlinks

  GV_DestroyOtherActor(actor)
    +-- Calls die() immediately
    +-- Unlinks from list
```

**Important**: Destruction via `GV_DestroyActor` is deferred. The `act` pointer is
replaced with a cleanup function. This prevents list corruption during iteration.
`GV_DestroyOtherActor` destroys immediately (safe when called from a different actor).

### 1.4 Actor Memory Layout

Actors are allocated as a contiguous block. The first bytes are `GV_ACT`, followed
by the actor's private work data:

```
+------------------+
| GV_ACT (header)  |  prev, next, act, die, free, name, runtime, count
+------------------+
| Actor Work Data  |  e.g., SnaInitWork, EnemyWork, CameraWork
|                  |  Contains OBJECT, CONTROL, state variables
+------------------+
```

### 1.5 Key Actors

| Actor          | File                    | Level    | Description                |
|----------------|-------------------------|----------|----------------------------|
| Game Daemon    | source/game/gamed.c     | DAEMON   | Stage loading, transitions |
| DG Daemon      | source/libdg/dgd.c      | DAEMON   | Render pipeline execution  |
| Snake          | source/chara/snake/      | LEVEL3   | Player character           |
| Enemy          | source/enemy/enemy.c     | LEVEL3   | Genome soldiers            |
| Camera         | source/game/camera.c    | AFTER    | Camera control             |
| Menu Manager   | source/menu/menuman.c   | MANAGER  | HUD, radar, item display   |
| Radio          | source/menu/radio.c     | MANAGER  | Codec conversations        |
| Subtitle Ctrl  | source/game/jimctrl.c   | MANAGER  | Cutscene subtitle timing   |

### 1.6 DG_OBJS and the Render Queue

Each visible actor creates one or more `DG_OBJS` (render object) and adds it to the
rendering channel queue via `DG_QueueObjs()`. The DG_OBJS contains the world matrix,
model definition, flags, and an array of sub-model `DG_OBJ` entries.

```
Actor Work:
  +-- OBJECT body:
  |    +-- DG_OBJS *objs    --> DG_QueueObjs() adds to channel
  |    +-- MOTION *motion    --> Animation state
  |    +-- SVECTOR *rots     --> Per-joint rotations
  +-- CONTROL control:
       +-- SVECTOR mov       --> World position
       +-- MAP *map          --> HZD collision data
```

When an actor is destroyed, `DG_DequeueObjs()` removes it from the render queue.
If this doesn't happen cleanly (e.g., memory freed before dequeue), the render queue
contains dangling pointers -- a common crash source on the port.

## 2. Stage System

### 2.1 Stage Loading Flow

```
GM_LoadRequest set (e.g., "s01a")
  |
  v
gamed.c LoadRequest:
  +-- GV_SetPauseLevel(0xFF)       -- Pause all gameplay actors
  +-- GV_ClearMemorySystem()       -- Free normal memory pool
  +-- FS_LoadStageRequest("s01a")  -- Load from STAGE.DIR
  |
  v
gamed.c LoadEnd:
  +-- Process DATACNF tags (textures, models, scripts)
  +-- Execute GCL "start" procedure
  +-- GV_SetPauseLevel(0)          -- Resume actors
```

### 2.2 Stage Overlay Compilation

On PSX, stage-specific code is loaded as overlays at runtime. The port compiles all
88 overlays statically into the binary. Each overlay maps stage hash -> init function
via a switch statement in `extern_stubs.c`.

Symbol conflicts between overlays (same function name, different implementations) are
resolved by the Makefile: each overlay is compiled with `-D` prefix renames.

Three R-variant overlays (d18ar, s08br, s19br) are excluded due to unresolvable
duplicate symbols.

### 2.3 Character Entry Registration

Each stage can spawn character types via GCL `chara` commands. The port registers
29 character type constructors in `MainCharacterEntries[]`:

```c
// extern_stubs.c
CharacterEntry MainCharacterEntries[] = {
    { GV_StrCode("snake"), NewSnake },
    { GV_StrCode("enemy"), NewEnemy },
    { GV_StrCode("camera"), NewCamera },
    // ... 29 entries total
};
```

## 3. GCL Scripting System

### 3.1 Overview

GCL (Game Command Language) is a bytecode scripting language embedded in stage data.
It controls actor spawning, cutscene flow, variable state, and event triggers.

### 3.2 Bytecode Format

GCL bytecode is a compact binary encoding:

```
Command:  [opcode:8] [length:8] [params...]
Opcode:   0x20='chara', 0x24='camera', 0x28='map', etc.
Params:   Type-length-value encoding
```

### 3.3 Key GCL Commands

| Command   | Description                                    |
|-----------|------------------------------------------------|
| `chara`   | Spawn character actor (name, position, params) |
| `map`     | Create map (collision, model, lighting)         |
| `camera`  | Set camera position/orientation                 |
| `start`   | Stage initialization procedure                  |
| `pad`     | Configure pad/input state                       |
| `bind`    | Bind procedure to trigger event                 |
| `mesg`    | Send message between actors                     |
| `radio`   | Start radio conversation                        |
| `demo`    | Start cutscene                                  |

### 3.4 GCL Expression Evaluator

GCL expressions are stack-based and use scratchpad memory for the evaluation stack:

```c
EXPR_STACK *sp = (EXPR_STACK *)(port_scratchpad + 0x200);
```

Expression operators include arithmetic (+, -, *, /, %), comparison (<, >, ==),
logical (&&, ||, !), and game-specific (random, distance, etc.).

### 3.5 64-bit GCL Issues

**Pointer-in-int storage**: GCL stores pointers in `int` variables (4 bytes on PSX).
On 64-bit, this truncates the address. The port uses a **pointer table**:

```c
void *gcl_ptr_table[256];
int   gcl_ptr_next = 0;

// Store: convert pointer to small index
int gcl_store_ptr(void *p) {
    gcl_ptr_table[gcl_ptr_next] = p;
    return 0x7F000000 | gcl_ptr_next++;
}

// Load: recognize marker and retrieve pointer
void *gcl_load_ptr(int val) {
    if ((val & 0xFF000000) == 0x7F000000)
        return gcl_ptr_table[val & 0xFF];
    return (void *)(intptr_t)val;
}
```

The `0x7F000000` prefix is a marker that distinguishes pointer-table indices from
regular integer values.

**NULL pointer guards**: Several GCL parsing functions could dereference NULL when
a parameter is missing. The port adds NULL checks in `GCL_GetOption`, `GCL_GetParam`,
and related functions.

## 4. Game Daemon (gamed.c)

### 4.1 Main Loop Actor

The game daemon is the top-level actor that orchestrates stage loading, transitions,
and the game state machine:

```
GM_Act (game daemon act callback):
  |
  +-- Check GM_LoadRequest
  |    +-- If set: start stage transition
  |         +-- Save resident caches
  |         +-- Clear memory
  |         +-- Load new stage data
  |         +-- Execute GCL start procedure
  |         +-- Spawn actors
  |
  +-- Check GM_GameOverTimer
  |    +-- If expired: handle game over
  |
  +-- Normal frame:
       +-- Update game timers
       +-- Process global state
```

### 4.2 DG Daemon (dgd.c)

The DG daemon executes the rendering pipeline at AFTER level:

```c
void DG_ActLast(Work *work) {
    DG_RenderFrame();  // Executes all 7 pipeline stages
}
```

### 4.3 Memory Management

```c
// PSX hardcoded addresses (replaced by port):
GV_NORMAL_MEMORY_TOP  = 0x80117000   // Port: malloc'd, 2MB
GV_NORMAL_MEMORY_SIZE = 0x6B000      // Port: 0x200000 (expanded)
GV_PACKET_MEMORY0_TOP = 0x80182000   // Port: malloc'd, 512KB
GV_PACKET_MEMORY1_TOP = 0x801B1000   // Port: malloc'd, 512KB
```

The port allocates larger pools to accommodate 64-bit pointer expansion in structs.
Two packet memory pools alternate with `GV_Clock` for double-buffered GPU packet
allocation.

Memory is managed via a bump allocator (`GV_Malloc`/`GV_Free`) with top-down and
bottom-up regions. The normal pool is cleared during stage transitions via
`GV_ClearMemorySystem()`.
