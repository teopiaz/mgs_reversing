# HZD Collision System

## 1. Overview

The HZD (Hazard) system provides collision detection, zone-based spatial queries, and
AI pathfinding data. All collidable geometry in a stage is described by HZD data loaded
from `'h'` cache entries in STAGE.DIR.

Three main subsystems:
1. **Wall collision** (`HZD_LineCheck`, `HZD_PointCheck`) -- prevent actors walking through walls
2. **Floor/ceiling height** (`HZD_LevelTestHazard`) -- gravity, crouch clearance
3. **Zone queries** (`HZD_EnterTrap`) -- triggers, enemy patrol routes, camera angles

## 2. Data Structures

### 2.1 HZD_VEC -- 3D Point (8 bytes)

```c
typedef struct {
    int  long_access[0];  // Flex array for 32-bit aliased access (GTE ops)
    short x, z, y, h;    // NOTE: Z before Y (PSX convention)
} HZD_VEC;                // h = height attribute or surface flag
```

**Important**: The field order is `x, z, y` (not `x, y, z`). This matches the PSX
convention where the game world uses X/Z as the horizontal plane and Y as vertical.

**Port fix**: `long_access[0]` changed from `long` (8 bytes on 64-bit) to `int`
(4 bytes) to maintain binary compatibility with GTE instructions that read 32-bit words.

### 2.2 HZD_SEG -- Wall Segment

```c
typedef struct {
    HZD_VEC p1, p2;     // Two endpoints of the wall line (16 bytes)
} HZD_SEG;
```

Walls are always perfectly vertical (Y extends from floor to ceiling). The soliton
radar is drawn from wall segment data.

### 2.3 HZD_FLR -- Floor/Ceiling Polygon

```c
typedef struct {
    HZD_VEC b1, b2;     // Axis-aligned bounding box (for quick rejection)
    HZD_VEC p1, p2, p3, p4;  // Four corner vertices (48 bytes total)
} HZD_FLR;
```

- `b1.h` = floor type flag (0=normal, 2=special)
- The bounding box `b1`/`b2` is tested first for quick AABB rejection before the
  full polygon test.

### 2.4 HZD_TRG -- Trigger Volume

```c
typedef struct {
    HZD_VEC b1, b2;     // Bounding box
    char    name[12];   // Trigger name string
    u_char  id1, id2;   // Trigger identifiers
    u_short name_id;    // Hashed name for lookup
} HZD_TRG;
```

### 2.5 HZD_ZON -- Zone

```c
typedef struct {
    short   x, z, y;     // Zone center position
    short   w, h;         // Zone dimensions (width, height)
    u_char  nears[6];     // Indices of up to 6 neighboring zones
    u_char  dists[6];     // Distance to each neighbor
    short   padding;
} HZD_ZON;
```

Zones partition the stage into regions. Used for:
- Camera angle selection
- Enemy AI awareness boundaries
- Music/ambient sound regions
- Group visibility (`DG_CurrentGroupID`)

### 2.6 HZD_GRP -- Collision Group

```c
typedef struct {
    short    n_triggers;
    short    n_walls;
    short    n_floors;
    short    n_flat_walls;
    HZD_SEG  *walls;        // Pointer to wall segment array
    HZD_FLR  *floors;       // Pointer to floor polygon array
    HZD_TRG  *triggers;     // Pointer to trigger array
    char     *wallsFlags;   // Per-wall attribute flags
} HZD_GRP;
```

### 2.7 HZD_MAP -- Stage Collision Header

```c
typedef struct {
    void     *ptr_access[0]; // Flex array (route pointer storage on PSX)
    short    version;
    short    min_x, min_y;   // Stage bounding box
    short    max_x, max_y;
    short    n_groups;
    short    n_zones;
    short    n_routes;
    HZD_GRP  *groups;       // Collision groups
    HZD_ZON  *zones;        // Spatial zones
    HZD_PAT  *routes;       // AI patrol routes
} HZD_MAP;
```

### 2.8 HZD_HDL -- Runtime Handle

```c
typedef struct {
    HZD_MAP   *header;            // Loaded collision data
    HZD_FLR  **dynamic_floors;    // Runtime-added floors
    HZD_SEG  **dynamic_segments;  // Runtime-added walls
    char      *dynamic_flags;     // Flags for dynamic segments
    short      n_dynamic_floors;
    short      n_dynamic_segments;
    short      max_dynamic_floors;
    short      max_dynamic_segments;
    HZD_GRP   *group;            // Current active group
} HZD_HDL;
```

## 3. Key Collision Functions

### 3.1 HZD_LineCheck -- Ray/Segment Test

```
int HZD_LineCheck(HZD_HDL *hzd, SVECTOR *from, SVECTOR *to, int flag, int exclude)
```

Tests a line segment against collision geometry. Returns non-zero if collision found.

**Algorithm**:
1. Copy `from`/`to` to scratchpad
2. Compute AABB of the line segment
3. Compute direction vector
4. For each collision group (filtered by `HZD_CurrentGroup`):
   a. If `flag & HZD_CHECK_SEG`: test against wall segments
   b. If `flag & HZD_CHECK_FLR`: test against floor polygons
   c. If `flag & HZD_CHECK_DYNSEG`: test against dynamic walls
   d. If `flag & HZD_CHECK_DYNFLR`: test against dynamic floors
5. Store nearest collision surface in `collide_ptrs.seg_064`

**Scratchpad layout** (offsets from SCRPAD_ADDR):
```
0x00C: SVECTOR from (copy)
0x014: SVECTOR to (working copy, may be modified)
0x054: SVECTOR to (original copy)
0x05C: int direction (non-zero if line has length)
0x064: void* nearest surface pointer (HZD_SEG or HZD_FLR)
0x06C: int collision result flags
0x08C: int best distance found
```

**Port guard**: The port adds `port_ptr_readable(hzd->header)` check at entry to
prevent SIGSEGV/SIGBUS from dangling HZD pointers during stage transitions.

### 3.2 HZD_LevelTestHazard -- Floor/Ceiling Heights

```
unsigned int HZD_LevelTestHazard(HZD_HDL *hzd, SVECTOR *point, int mode)
```

Returns a bitmask: bit 0 = floor found, bit 1 = ceiling found.
Heights stored via `HZD_LevelMinMaxHeights()`.

Used by:
- `CheckHeight()` in `control.c` for gravity
- `sna_8004E71C()` for crouch/stand ceiling clearance check
- `sub_8004E588()` for general height queries

### 3.3 HZD_StepCheck -- Wall Push-Back

```
int HZD_StepCheck(SVECTOR *nears, int count, int scale, SVECTOR *out)
```

After `GM_ActControl` detects a collision (`touch_flag > 0`), the step check computes
how to push the actor away from the wall.

### 3.4 HZD_EnterTrap -- Trigger Processing

```
void HZD_EnterTrap(HZD_HDL *hzd, HZD_EVT *event)
```

Tests actor position against trigger volumes. Sets event flags when entering/exiting.

## 4. CONTROL Struct and Collision Integration

### 4.1 CONTROL Structure

```c
typedef struct CONTROL {
    SVECTOR     mov;          // Current world position
    SVECTOR     rot;          // Current orientation (4096 = 360 degrees)
    HZD_EVT     event;        // Trigger event state
    MAP        *map;          // Active map (contains hzd pointer)
    u_short     name;         // Actor hash name
    short       height;       // Actor height offset
    short       hzd_height;   // Collision height (-0x7FFF = disabled)
    short       step_size;    // Movement mode (>0=normal, <0=no collision, 0=static)
    short       field_38;
    u_short     radar_atr;    // Radar display attributes
    RADAR_CONE  radar_cone;
    SVECTOR     step;         // Movement vector this frame
    SVECTOR     turn;         // Target rotation
    signed char interp;       // Turn interpolation speed
    char        skip_flag;    // CTRL_SKIP_* flags
    signed char n_messages;
    signed char level_flag;   // 1=below floor, 2=above ceiling
    signed char touch_flag;   // >0 if wall collision detected
    char        exclude_flag; // Surface exclusion mask
    char        nearflags[2]; // Collision surface attributes
    GV_MSG     *messages;
    SVECTOR     nearvecs[2];  // Direction to nearest surfaces
    void       *nears[2];    // Pointers to collision surfaces (HZD_SEG or HZD_FLR)
    short       levels[2];   // Floor and ceiling heights
} CONTROL;
```

### 4.2 GM_ActControl -- Per-Frame Collision Processing

```
void GM_ActControl(CONTROL *control)
```

Called each frame for every actor with a CONTROL struct:

```
GM_ActControl(control)
  |
  +-- CheckMessage(control)      -- Process GCL messages
  +-- GM_CurrentMap = control->map->index
  |
  +-- if step_size > 0:          -- Normal movement
  |    +-- touch_flag = 0        -- Reset collision state
  |    +-- CheckCollide(control) -- Wall collision + push-back
  |    |    +-- HZD_LineCheck() for movement ray
  |    |    +-- Store nears[0], nearflags[0], nearvecs[0]
  |    +-- Apply step to position (mov += step)
  |    +-- CheckNear(control)    -- Multi-surface collision
  |    |    +-- HZD_PointCheck() for nearby surfaces
  |    |    +-- Store nears[0..1], nearflags[0..1]
  |    +-- CheckHeight(control)  -- Floor/ceiling
  |         +-- HZD_LevelTestHazard()
  |         +-- Set level_flag (1=below floor, 2=above ceiling)
  |         +-- Gravity: snap to floor if below
  |
  +-- if step_size < 0:          -- No-collision movement
  |    +-- touch_flag = 0
  |    +-- Apply step directly
  |    +-- CheckHeight if step_size >= -1
  |
  +-- if step_size == 0:         -- Static (no movement)
  |    +-- [PORT] Reset touch_flag and nears[] to prevent stale pointers
  |
  +-- Trigger processing: HZD_EnterTrap()
  +-- DG_SetPos2() -- Update rendering position
```

### 4.3 Stale nears[] Pointer Bug (64-bit)

**Problem**: When `step_size == 0`, neither collision branch runs. `touch_flag` and
`nears[]` retain values from a previous frame. On PSX, freed memory remains mapped,
so stale pointers are harmless. On macOS 64-bit, freed pages are unmapped, causing
SIGSEGV when `sna_init_main_logic_helper_helper_800596FC` dereferences `nears[]`.

**Fix**: Reset `touch_flag` and `nears[]` to 0/NULL when `step_size == 0`.

### 4.4 Ceiling Check Stack Layout Bug (64-bit)

**Problem**: `sna_8004E71C()` declares `vec` and `vec_saved` as separate local variables.
`sna_line_check()` accesses `line[0]` and `line[1]` assuming they are contiguous SVECTORs.
On PSX (MIPS), the compiler places them adjacently. On ARM64 (clang), the stack layout
differs -- `line[1]` points to a different variable, causing stack corruption.

```
PSX stack layout:         ARM64 stack layout:
  [vec]      <-- line[0]    [a4]
  [vec_saved]<-- line[1]    [mtx]        (32 bytes)
  [mtx]                     [vec_saved]
  [levels]                  [vec]        <-- line[0]
  [a4]                      [levels]     <-- line[1] points HERE!
```

**Fix**: Declare as array: `SVECTOR vecs[2]` with `#define vec vecs[0]` and
`#define vec_saved vecs[1]` to guarantee contiguity.

## 5. HZD Binary Loading (32-bit to 64-bit)

### 5.1 The Problem

HZD files are binary dumps of PSX memory structures. On PSX, pointers are 4 bytes.
The file contains 32-bit offsets (relative to file start) where the original code uses
`OFFSET_TO_PTR()` to convert them to absolute pointers:

```c
// PSX macro -- DANGEROUS on 64-bit:
#define OFFSET_TO_PTR(ptr, offset) (*(int *)offset = (int)ptr + *(int *)offset)
```

This truncates 64-bit pointers to 32 bits, causing crashes.

### 5.2 Port Solution (hzd_loader.c)

The port defines packed structs matching the binary format:

```c
#pragma pack(push, 1)
typedef struct {
    int16_t  n_triggers, n_walls, n_floors, n_flat_walls;
    uint32_t walls_off, floors_off, triggers_off, wallsFlags_off;
} HZD_GRP_RAW;  // 24 bytes -- matches PSX memory layout exactly

typedef struct {
    int16_t  version, min_x, min_y, max_x, max_y;
    int16_t  n_groups, n_zones, n_routes;
    uint32_t groups_off, zones_off, routes_off;
} HZD_MAP_RAW;  // 24 bytes
#pragma pack(pop)
```

Then converts to runtime structures with full 64-bit pointers:

```c
HZD_MAP *hzd_map = malloc(sizeof(HZD_MAP));
hzd_map->groups = malloc(n_groups * sizeof(HZD_GRP));
for (int i = 0; i < n_groups; i++) {
    hzd_map->groups[i].walls  = (HZD_SEG *)((char *)raw_data + raw_grp[i].walls_off);
    hzd_map->groups[i].floors = (HZD_FLR *)((char *)raw_data + raw_grp[i].floors_off);
    // ... etc
}
```

### 5.3 Zone Data

`HZD_ZON` contains only scalars (shorts and chars) -- no pointers. Loaded directly
from the binary without conversion. However, the `nears[6]` neighbor indices are
zone indices, not pointers, so they work on any platform.

### 5.4 Route Data

`HZD_PAT` contains a pointer to `HZD_PTP` point data. The port converts the 32-bit
offset to a 64-bit pointer at load time. `HZD_MakeRoute()` uses BFS to build a
distance matrix for AI pathfinding.

### 5.5 Dynamic Collision

`HZD_HDL` maintains arrays of runtime-added floors and walls:
- `dynamic_floors[]`: Pointers to `HZD_FLR` added by actors (elevators, doors)
- `dynamic_segments[]`: Pointers to `HZD_SEG` added by actors
- Allocated via `GV_Malloc` with fixed 4-byte-per-entry sizing (critical -- using
  `sizeof(ptr)` on 64-bit caused memory pool corruption)

## 6. Scratchpad Usage in Collision

The collision system makes heavy use of the PSX scratchpad (1KB fast RAM at 0x1F800000).
The port replaces this with a global `char port_scratchpad[9216]` array aligned to 16
bytes (to prevent ARM64 SIGBUS on unaligned access).

### 6.1 Level Testing Scratchpad Layout

```
Offset  Type       Name           Description
0x08    int        side           Which side of surface
0x0C    HZD_VEC    point          Test point
0x34    HZD_VEC    f34            Intermediate result
0x3C    HZD_FLR*   max_floor      [PORT: stored separately as static]
0x40    HZD_FLR*   min_floor      [PORT: stored separately as static]
0x44    int        max_level      Maximum height found
0x48    int        min_level      Minimum height found
```

**Port issue**: `max_floor` and `min_floor` at offsets 0x3C and 0x40 are 4-byte
pointers on PSX but 8-byte on 64-bit. The port stores them in module-level static
variables instead of scratchpad to avoid overlap.

### 6.2 port_ptr_readable -- Safe Pointer Validation

The port uses the Mach VM API to safely check if a pointer is readable before
dereferencing it:

```c
static inline int port_ptr_readable(const void *p) {
    if (!p) return 0;
    char buf;
    vm_size_t sz = 1;
    kern_return_t kr = vm_read_overwrite(
        mach_task_self(), (vm_address_t)p, 1,
        (vm_address_t)&buf, &sz);
    return kr == KERN_SUCCESS;
}
```

This is used in `HZD_LineCheck`, `HZD_PointCheck`, `sna_8004E71C`, and
`sna_init_main_logic_helper_helper_800596FC` to guard against dangling pointers
from freed collision data.
