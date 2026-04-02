# HZD Collision System

## Overview

The HZD (Hazard) system provides collision detection, zone-based spatial queries, and AI pathfinding data. All collidable geometry in a stage is described by HZD data loaded from 'h' cache entries in STAGE.DIR. The system handles three main concerns:

1. **Walk collision** -- preventing actors from passing through walls and placing them on floors
2. **Line-of-sight** -- ray casting for enemy vision cones and weapon fire
3. **Zone queries** -- determining which navigation zone a position falls in (used by enemy AI for pathfinding)

---

## Data Structures

### HZD_MAP (Header)

```c
typedef struct {
    void        *ptr_access[0];   // zero-length array for pointer aliasing (PSX hack)
    short       version;
    short       min_x, min_y;     // bounding box min
    short       max_x, max_y;     // bounding box max
    short       n_groups;         // number of area groups
    short       n_zones;          // number of navigation zones
    short       n_routes;         // number of patrol routes
    HZD_GRP    *groups;           // pointer to group array
    HZD_ZON    *zones;            // pointer to zone array
    HZD_PAT    *routes;           // pointer to patrol route array
} HZD_MAP;
```

The `ptr_access[0]` trick allowed PSX code to write `hzm->ptr_access[0] = 0` to clear the first 4 bytes, which was later used to store the route lookup table pointer. On 64-bit this truncates to 4 bytes; see the route pointer fix below.

### HZD_GRP (Area Group)

Each group represents a spatial subdivision of the stage. A stage typically has 1-4 groups.

```c
typedef struct {
    short       n_triggers;     // trigger volumes (traps + cameras)
    short       n_walls;        // vertical wall segments
    short       n_floors;       // floor polygons
    short       n_flat_walls;   // flat (non-vertical) wall segments
    HZD_SEG    *walls;          // array of wall segments
    HZD_FLR    *floors;         // array of floor polygons
    HZD_TRG    *triggers;       // array of trigger volumes
    char       *wallsFlags;     // per-wall flag bytes
} HZD_GRP;
```

### HZD_SEG (Wall Segment)

Walls are always perfectly vertical, defined by two endpoints with height:

```c
typedef struct { short x, z, y, h; } HZD_VEC;
typedef struct { HZD_VEC p1, p2; } HZD_SEG;
```

The soliton radar map is drawn directly from wall segment data.

### HZD_FLR (Floor)

Floors are quadrilaterals with a bounding box:

```c
typedef struct {
    HZD_VEC b1, b2;           // bounding box (min/max)
    HZD_VEC p1, p2, p3, p4;  // four corner vertices
} HZD_FLR;
```

### HZD_ZON (Navigation Zone)

Zones partition the stage into regions for AI pathfinding. Each zone knows its 6 nearest neighbor zones and the distance to each:

```c
typedef struct {
    short       x, z, y;       // zone center position
    short       w, h;          // zone width and height
    u_char      nears[6];      // indices of up to 6 neighbor zones (0xFF = none)
    u_char      dists[6];      // distance to each neighbor
    short       padding;
} HZD_ZON;
```

### HZD_PAT (Patrol Route)

Enemy patrol routes are sequences of waypoints:

```c
typedef struct {
    short       x, z, y;
    short       command;       // behavior at this point (direction, wait time, etc.)
} HZD_PTP;

typedef struct {
    short       n_points;      // number of waypoints
    short       init_point;    // starting waypoint index
    HZD_PTP    *points;        // array of waypoints
} HZD_PAT;
```

### HZD_TRP / HZD_CAM (Triggers)

Triggers are a union of trap volumes and camera regions:

```c
typedef struct {
    HZD_VEC b1, b2;           // bounding box
    char    name[12];          // trigger name (hashed to name_id)
    u_char  id1, id2;
    u_short name_id;           // GV_StrCode hash of name
} HZD_TRP;

typedef union { HZD_CAM cam; HZD_TRP trap; } HZD_TRG;
```

Triggers and cameras are stored contiguously in the triggers array, with cameras first. `HZD_MakeHandler` separates them by scanning for `id2 == 0xFF`.

### HZD_HDL (Runtime Handle)

Created by `HZD_MakeHandler`, this is the per-map runtime collision state:

```c
typedef struct {
    HZD_MAP    *header;              // pointer to loaded HZD_MAP
    HZD_GRP   *group;               // current area group
    short       map;
    short       dynamic_queue_index;
    short       dynamic_floor_index;
    short       n_cameras;
    short       max_dynamic_floors;
    short       max_dynamic_segments;
    u_char     *route;               // zone distance lookup table
    HZD_TRP    *traps;               // pointer to first trap (after cameras)
    HZD_FLR   **dynamic_floors;      // dynamically added floors (elevators, etc.)
    HZD_SEG   **dynamic_segments;    // dynamically added walls
    char       *dynamic_flags;       // flags for dynamic segments
} HZD_HDL;
```

---

## Key Functions (collide.c)

The collision core is `source/libhzd/collide.c` (1363 lines). It is now compiled from source in the port after replacing 161 scratchpad addresses and 49 GTE inline assembly calls.

### HZD_StepCheck

```c
int HZD_StepCheck(SVECTOR *nears, int count, int scale, SVECTOR *out);
```

Walk collision. Given a movement vector, checks against nearby wall segments to prevent the actor from passing through. Returns the adjusted movement vector in `out`. Used every frame for Snake and enemy movement.

### HZD_LineCheck

Line-of-sight ray cast. Tests a ray against walls and floors to determine visibility between two points. Used by enemy vision cones (`searchli.c`), weapon fire, and camera obstruction checks.

### HZD_PointCheck

Point query. Given a position, finds the floor polygon directly below it and returns the floor height. Used to keep actors grounded on the terrain.

### HZD_SurfaceNormal

Computes the surface normal vector of a floor polygon. Used for slope-dependent movement speed and effects.

### HZD_GetAddress

```c
int HZD_GetAddress(HZD_HDL *hzdMap, SVECTOR *pos);
```

Zone lookup. Determines which navigation zone contains the given position. Returns the zone index, or 0 if no zone matches. This is critical for enemy AI: enemies use zone indices to compute shortest paths to the player via the route distance table.

---

## HZD Loader (port/libhzd/hzd_loader.c)

### The 64-bit Problem

On PSX, HZD data is loaded as a raw binary blob. Pointer fields (groups, zones, routes, walls, floors, triggers) are stored as 32-bit offsets from the start of the buffer. The PSX code converts these to pointers in-place using:

```c
#define OFFSET_TO_PTR(ptr, offset) (*(int *)offset = (int)ptr + *(int *)offset)
```

This writes `base + offset` back into the same 4-byte field. On 64-bit, `int` is still 4 bytes but pointers are 8 bytes. Writing a pointer into a 4-byte field truncates it, and the `HZD_GRP` struct layout changes because pointer fields are now 8 bytes wide.

### Port Solution

`port/libhzd/hzd_loader.c` defines packed 32-bit raw structures (`HZD_MAP_RAW`, `HZD_GRP_RAW`) that match the PSX binary layout exactly. The loader:

1. Reads the raw 32-bit header to extract counts and offsets
2. Allocates a new `HZD_MAP` + `HZD_GRP[]` block with correct 64-bit struct sizes via `GV_AllocMemory`
3. Copies scalar fields (version, bounds, counts)
4. Converts each 32-bit offset to a 64-bit pointer: `(HZD_SEG *)(base + raw_groups[i].walls_off)`
5. Patrol routes require special handling because `HZD_PAT.points` is a pointer field. Raw routes use `HZD_PAT_RAW` (8 bytes: 2 shorts + 1 uint32_t offset), and the loader allocates a new `HZD_PAT[]` array with `malloc` and converts each `points_off` to a full pointer.

The `HZD_GRP` array is placed immediately after the `HZD_MAP` in the same allocation (`groups = (HZD_GRP *)(hzm + 1)`).

### Zones: No Conversion Needed

`HZD_ZON` contains only scalar fields (shorts and chars), no pointers. The zones pointer is set directly: `hzm->zones = (HZD_ZON *)(base + raw->zones_off)`. This points into the original raw buffer, which is fine because the struct layout is identical between 32-bit and 64-bit.

---

## Route System (hzdd.c)

### HZD_MakeRoute

Computes a zone-to-zone distance matrix using a BFS-based approach (not Floyd-Warshall as initially suspected -- it does per-source BFS via `HZD_MakeRoute_helper`). The result is a compressed upper-triangular matrix stored as `(n-1)*(n-2)/2 + (n-1)` bytes.

For each source zone, `HZD_MakeRoute_helper` performs a breadth-first search through the zone neighbor graph, recording the hop count to every other zone. The BFS uses a double-buffered work queue (`zone_buf[2]`, 16 ints each) to alternate between current and next frontier.

### Route Pointer Fix

On PSX, `HZD_MakeHandler` stored the computed route table pointer by writing it into the first 4 bytes of the HZD_MAP struct:

```c
*(int *)hzd = (int)zones;  // via hzm->ptr_access[0]
```

On 64-bit, this truncates the pointer. The port fixes this with a static cache:

```c
static void *cached_route = NULL;
static HZD_MAP *cached_hzd = NULL;
if (cached_hzd != hzd) { cached_route = NULL; cached_hzd = hzd; }
if (!cached_route) { ... HZD_MakeRoute(hzd, zones); cached_route = zones; }
```

This works because only one stage is active at a time, so there is at most one route table.

---

## Trigger Processing (HZD_ProcessTraps)

When HZD data loads, `HZD_ProcessTraps` iterates all trigger volumes and converts their `name` character arrays to hash codes via `GV_StrCode`. The trap name string is null-terminated at the first space character, then hashed. The resulting `name_id` is used for event matching (bind callbacks in GCL).

---

## Known Issues

### HZD_GetAddress Returns 0 for Snake

`HZD_GetAddress` is supposed to return the zone index containing Snake's position. It currently returns 0, which sets `GM_PlayerAddress = 0`. This means:

- Enemy AI cannot determine Snake's zone
- Route distance lookups return garbage
- The "Where Is Snake ????" debug message prints every frame

**Root cause**: The zone data itself is correct (HZD_ZON has no pointers). The issue is likely that Snake's position does not fall within any zone's bounding box. This could be caused by:
1. Snake spawning at the wrong position (coordinate system mismatch)
2. Zone coordinates needing a transform that is not being applied
3. The zone lookup algorithm using a different coordinate space than Snake's world position

### Snake Does Not Move

Even with correct input, Snake does not move because `HZD_StepCheck` and related floor/wall queries may return incorrect results if the collision handle (`HZD_HDL`) is not properly initialized, or if the scratchpad emulation in collide.c has subtle issues with the 161 replaced addresses.

### Dynamic Collision

Elevators and moving platforms add/remove floor and wall segments at runtime via `HZD_HDL.dynamic_floors` and `dynamic_segments`. This system is functional but has not been extensively tested in the port.
