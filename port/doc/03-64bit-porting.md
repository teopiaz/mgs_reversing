# 64-bit Porting Issues

This document catalogs every 64-bit porting issue encountered when moving the
decompiled PSX (MIPS, 32-bit) code to macOS (ARM64/x86_64, 64-bit), along with
the fix applied in each case.

---

## 1. Type Size Mismatches

### u_long redefined to uint32_t

On PSX, `unsigned long` is 4 bytes. On 64-bit macOS, it is 8 bytes. Every PSX
GPU primitive struct packs a `u_long tag` as the first field (4 bytes holding a
24-bit next-pointer and an 8-bit length). If `u_long` were 8 bytes, every
primitive struct layout would be wrong.

**Fix** (`port/psx/sys/types.h`):
```c
typedef uint32_t u_long;   /* force 4-byte, matching PSX */
```

This is included before any PSYQ header, so all `u_long` usages throughout the
codebase resolve to 4 bytes.

### MATRIX int vs long

The PSYQ `MATRIX` struct uses `long` for the translation vector `t[3]`. On PSX
this is 4 bytes; on 64-bit it is 8 bytes. The GTE math code and all
`CompMatrix`/`MulMatrix` calls assume 4-byte fields.

**Fix** (`port/psx/libgte.h`): The `MATRIX` struct is redefined with `int`
fields (always 4 bytes) instead of `long`.

### Pointer sizes in structs

Any struct that contains a pointer is larger on 64-bit than on PSX. This is the
root cause of every binary data loader issue described in Section 3. On PSX,
`DG_MDL` is 76 bytes (pointers are 4 bytes each). On 64-bit, it is 120 bytes
(pointers are 8 bytes each). You cannot cast a raw data buffer to `DG_MDL *`
and expect it to work.

---

## 2. Pointer-in-int Storage

The original PSX code frequently stores pointers in `int` variables. On 32-bit
MIPS, `sizeof(int) == sizeof(void *)`, so this works. On 64-bit, the upper 32
bits of the pointer are silently truncated.

### GM_PlayerPosition

**Original**: declared as `int GM_PlayerPosition` in the data section. Game code
writes `*(SVECTOR *)&GM_PlayerPosition`, relying on `int` being the same size as
`SVECTOR` (8 bytes = 4 shorts, packed into two `int`-sized words on PSX).

**Fix** (`port/link_stubs.c`): Declared as `SVECTOR GM_PlayerPosition = {0}`,
which is the actual type.

### GM_PlayerControl / GM_PlayerBody

**Original**: `int GM_PlayerControl`, `int GM_PlayerBody`. These store pointers
to `CONTROL` and `OBJECT` structs respectively.

**Fix**: Declared as `void *`:
```c
void *GM_PlayerBody = NULL;
static char _default_control[256] = {0};
void *GM_PlayerControl = _default_control;
```
`GM_PlayerControl` needs a valid default because many enemy actors dereference
it unconditionally. The 256-byte zero buffer prevents NULL-pointer crashes.

### DG_HikituriFlagOld

**Original PSX**: `int` variable in BSS. Some port stubs initially declared this
as a function (forward-declared prototype was misread). Writing to a function
address crashes with `EXC_BAD_ACCESS code=2` (write to code page).

**Fix**: Declared as `int DG_HikituriFlagOld = 0` in `link_stubs.c`.

### dword_8009F440 / dword_8009F444 / dword_8009F448

Same issue as above. These unnamed data variables from the original binary were
initially stubbed as `void dword_8009F440(void) {}` (function stubs) instead of
`int dword_8009F440 = 0` (data stubs). When the game engine wrote values to
these "variables", it was writing to the code segment.

**Symptom**: `EXC_BAD_ACCESS (code=2, address=0x...)` where the faulting address
is in the `__TEXT` segment.

**Fix**: Changed from function stubs to data stubs in `link_stubs.c`.

### 16 data variables wrongly stubbed as functions

Beyond the individual cases above, 16 overlay data variables (including
`ZAKOCOM_PlayerAddress_800D5C50`, `ZAKOCOM_PlayerMap_800D5C54`,
`TOPCOMMAND_800D5C40`, various `s07a_dword_*`, `s11e_dword_*`, etc.) were
initially generated as function stubs by the auto-stub script. All were changed
to `int name = 0;` or `STUB_SVECTOR name = {0};` as appropriate:

```c
int ZAKO11F_GameFlag_800D5C4C = 0;
int ZAKOCOM_PlayerAddress_800D5C50 = 0;
int ZAKOCOM_PlayerMap_800D5C54 = 0;
STUB_SVECTOR ZAKOCOM_PlayerPosition_800D5AF0 = {0};
int Zako11FCommand_800D5AF8 = 0;
int TOPCOMMAND_800D5C40 = 0;
int s07a_dword_800E3650 = 0;
/* ... etc ... */
```

---

## 3. Binary Data Loaders

On PSX, binary file formats store 32-bit offsets in fields that the original
code patches into pointers using `OFFSET_TO_PTR(base, field)` — which simply
adds the base address to the offset and stores the result back in the same
4-byte field. On 64-bit, pointer fields are 8 bytes, so the raw binary data no
longer lines up with the C struct. Each loader must parse the raw 32-bit layout
and construct proper 64-bit structs.

### KMD (3D Model)

**File**: `port/libdg/kmd_loader.c`

**PSX binary layout** (`KMD_MDL_RAW`, 76 bytes per model):
```
offset  size  field
 0       4    flags
 4       4    n_faces
 8      24    min, max, pos (3x DG_VECTOR)
32       4    parent
36       4    extend
40       4    n_verts
44       4    vertices_off      <- 32-bit offset
48       4    vindices_off      <- 32-bit offset
52       4    n_normals
56       4    normals_off       <- 32-bit offset
60       4    nindices_off      <- 32-bit offset
64       4    texcoords_off     <- 32-bit offset
68       4    materials_off     <- 32-bit offset
72       4    padding
```

**64-bit `DG_MDL`** (120 bytes): Same fields but each `_off` field is an 8-byte
pointer (`SVECTOR *`, `unsigned char *`, `unsigned short *`).

**Conversion** (in `DG_LoadInitKmd`):
1. Read `n_models` from the `KMD_DEF_RAW` header.
2. `malloc(sizeof(DG_DEF) + sizeof(DG_MDL) * n_models)` -- persistent allocation.
   Must NOT use `GV_AllocMemory(GV_NORMAL_MEMORY, ...)` because that memory is
   per-frame (cleared each tick).
3. Copy integer fields directly.
4. Convert offset fields: `mdl->vertices = (SVECTOR *)(buf + rm->vertices_off)`.
5. Store in cache via `GV_SetCache(id, def)`.

### OAR (Animation Archive)

**File**: `port/libdg/libdg_stub.c` (`DG_LoadInitOar`)

**PSX binary layout** (16-byte header):
```
offset  size  field
 0       4    [unused/overwritten]  <- PSX stores archive ptr here
 4       4    n_joint
 8       4    n_motion
12       4    [unused/overwritten]  <- PSX stores table ptr here
16       ...  oarData (table + archive bitstream)
```

On PSX, the loader overwrites offsets 0 and 12 with computed pointers into the
data. On 64-bit, these 4-byte slots cannot hold 8-byte pointers.

**Fix**: `malloc(sizeof(DG_OAR))` to create a proper 64-bit struct, then compute
the pointer fields:
```c
oar->table   = (unsigned short *)data;
oar->archive = (unsigned short *)(data + table_size);
```
where `table_size = ((n_joint + 2) * n_motion) * sizeof(unsigned short)`.

### IMG (Tilemap Image)

**File**: `port/libdg/libdg_stub.c` (`DG_LoadInitImg`)

**PSX binary layout** (20 bytes):
```
offset  size  field
 0       2    image_width
 2       2    image_height
 4       2    tile_width
 6       2    tile_height
 8       4    textures offset    <- relative to struct start
12       4    attribs offset
16       4    tilemap offset
```

On PSX, the three `uint32_t` offset fields are patched to pointers in-place.
On 64-bit, `DG_IMG` has three 8-byte pointer fields and is larger than 20 bytes.

**Fix**: `malloc(sizeof(DG_IMG))`, copy the 4 `short` fields, read the 3 offsets
as `uint32_t`, and convert:
```c
img->textures = (unsigned short *)((char *)buf + tex_off);
img->attribs  = (DG_IMG_ATTRIB *)((char *)buf + att_off);
img->tilemap  = (unsigned char *)((char *)buf + til_off);
```

### HZD (Collision/Hazard Map)

**File**: `port/libhzd/hzd_loader.c`

Two raw struct types mirror the PSX binary:

**`HZD_MAP_RAW`** (24 bytes):
```
offset  size  field
 0       2    version
 2       2    min_x
 4       2    min_y
 6       2    max_x
 8       2    max_y
10       2    n_groups
12       2    n_zones
14       2    n_routes
16       4    groups_off
20       4    zones_off
24       4    routes_off      (total: 28 bytes including alignment)
```

**`HZD_GRP_RAW`** (24 bytes):
```
offset  size  field
 0       2    n_triggers
 2       2    n_walls
 4       2    n_floors
 6       2    n_flat_walls
 8       4    walls_off
12       4    floors_off
16       4    triggers_off
20       4    wallsFlags_off
```

**64-bit conversion**:
1. `GV_AllocMemory(GV_NORMAL_MEMORY, sizeof(HZD_MAP) + sizeof(HZD_GRP) * n_groups)`.
2. Copy header fields.
3. Convert group offsets to pointers:
   `groups[i].walls = (HZD_SEG *)(base + raw_groups[i].walls_off)`.
4. For routes, each `HZD_PAT` has a `points` pointer field. The raw format has a
   4-byte offset. Must `malloc(n_routes * sizeof(HZD_PAT))` and convert each:
   `routes[i].points = (HZD_PTP *)(base + raw_routes[i].points_off)`.

### HZD Route Pointer (hzdd.c)

**Original PSX code** (`source/libhzd/hzdd.c`): Stores the current HZD map
pointer as `*(int *)hzd`, casting a `HZD_MAP *` to `int`. On 64-bit, this
truncates the upper 32 bits of the address.

**Symptom**: Enemies and collision code crash when accessing the HZD map because
the truncated pointer is invalid.

**Fix**: A static cache (`static HZD_MAP *hzd_cache`) stores the full 64-bit
pointer. The truncated `int` value is used as a lookup key.

### GCL (Script Bytecode)

**File**: `port/libgcl_fix/parse.c`, `port/libgcl_fix/gcl_ptr_table.h`

GCL bytecode parsing returns pointers to script data via `int *value_p`.
On PSX, a pointer fits in `int`. On 64-bit, it does not.

**Fix**: A pointer table `gcl_ptr_table[256]` maps small integer indices to full
64-bit pointers:

```c
static inline int gcl_store_ptr(void *p) {
    int idx = gcl_ptr_next++ & (GCL_PTR_TABLE_SIZE - 1);
    gcl_ptr_table[idx] = p;
    return 0x7F000000 | idx;   /* sentinel + index */
}

static inline void *gcl_resolve_ptr(int value) {
    if ((value & 0x7F000000) == 0x7F000000)
        return gcl_ptr_table[value & (GCL_PTR_TABLE_SIZE - 1)];
    return (void *)(intptr_t)value;
}
```

All GCL string/data opcodes (`GCLCODE_STRING`, `GCLCODE_SCRIPT_DATA`,
`GCLCODE_PARAMETER`) use `gcl_store_ptr()` when storing values, and callers use
`gcl_resolve_ptr()` (via `GCL_GetOption`, `GCL_ReadString`) to recover the
pointer.

### RPK (weapon.c OffsetToPointer)

The `OffsetToPointer` macro in weapon loading code adds a base pointer to a
32-bit offset stored in a struct field. On PSX, the offset field is 4 bytes
(same as a pointer). On 64-bit, the pointer field is 8 bytes, but the raw data
only populated the lower 4 bytes.

**Fix**: Rewrite the macro to read the 4-byte offset value, then add it to the
base pointer:
```c
#define OffsetToPointer(base, field) \
    ((void *)((char *)(base) + (uint32_t)(uintptr_t)(field)))
```

### Font (Big-Endian Header)

Font data files contain big-endian header offsets followed by glyph data.
The header offset fields are read directly as `int` on PSX (which happens to be
little-endian but matches the file format). On 64-bit macOS (also
little-endian), the glyph body data needed byte-swapping.

---

## 4. SCRPAD_ADDR Truncation

**Original definition** (in the port's `common.h`):
```c
#define SCRPAD_ADDR ((unsigned long)(port_scratchpad))
```

Since `u_long` is redefined to `uint32_t` (4 bytes) and `port_scratchpad` is a
global array at a 64-bit address, casting to `uint32_t` truncated the pointer
to its lower 32 bits. ALL scratchpad accesses (collision scratch buffers, GTE
scratch data) then used a completely wrong address.

**Symptom**: Mysterious corruption and crashes in collision code, animation code,
and any function using `getScratchAddr()`.

**Fix** (`port/include/common.h`):
```c
#define SCRPAD_ADDR ((unsigned long long)(port_scratchpad))
```

Note: `SCRPAD_ADDR` is only used for address arithmetic, never stored in a
`u_long` variable, so using `unsigned long long` is safe.

---

## 5. DG_OBJ.extend Field

`DG_OBJ.extend` is typed as `struct _DG_OBJ *` (a pointer). However, the KMD
loader stores a small integer (model index) in it via:
```c
mdl->extend = (struct _DG_OBJ *)(intptr_t)rm->extend;
```

Later, `libdg` code checks:
```c
if (model->extend < 0)
```

On PSX, a pointer is 32 bits, and negative values have bit 31 set (e.g.,
`0xFFFFFFFF`). On 64-bit, the value `(intptr_t)-1` is
`0xFFFFFFFFFFFFFFFF`, which when stored as a pointer is a very large positive
address, so `model->extend < 0` is ALWAYS false (pointers are unsigned on most
platforms, and `<` on pointers is implementation-defined).

**Fix**: Cast to `(int)(intptr_t)` before comparison:
```c
if ((int)(intptr_t)model->extend < 0)
```

---

## 6. GCL Proc ID Corruption

GCL bind callbacks store pointers as proc IDs. The `gcl_store_ptr` function
returns values like `0x7F0000xx`. When `GCL_ExecProc` receives such an ID, it
must recognize it as a pointer-table index rather than a normal proc hash.

**Original problem**: A 64-bit pointer was truncated to 32 bits when stored as a
proc ID. The resulting ID pointed to invalid memory.

**Fix**: Guard in `GCL_ExecProc`:
```c
if ((id & 0x7F000000) == 0x7F000000) {
    /* This is a GCL pointer table index, resolve it */
    void *ptr = gcl_resolve_ptr(id);
    /* ... execute via pointer ... */
}
```

---

## 7. Resident Cache / Texture Table Overflow

### Flag Timing Bug

`FS_ResidentCacheDirty` is set to 1 when a stage's 'r' (resident) archive is
loaded. `gamed.c` checks this flag during the `WAIT_LOAD` transition to save
resident caches.

**Original bug**: `FS_LoadStageComplete()` cleared the flag BEFORE `gamed.c`
had a chance to read it. Result: resident data was never saved and was lost on
stage transitions.

**Fix** (`port/libfs/libfs.c`):
```c
void FS_LoadStageComplete(void *info)
{
    /* Do NOT clear FS_ResidentCacheDirty here.
       gamed.c clears it after saving caches. */
}
```

The flag is now cleared in `gamed.c` AFTER
`GV_SaveResidentFileCache()` and `DG_SaveResidentTextureCache()`.

### Texture Table Overflow

With the flag bug, `DG_SaveResidentTextureCache()` was called on every stage
load (because `FS_ResidentCacheDirty` was always re-set to 1). Each call saved
ALL current textures as resident. After loading multiple stages, 500+ textures
accumulated in the resident list, exceeding the 512-slot `TexSets[]` table.

**Symptom**: After 3-4 stage transitions, textures become corrupted or the game
crashes in `FindTexture` (linear probe wraps the entire table).

**Fix**: The flag timing fix above ensures `DG_SaveResidentTextureCache` is
only called once when truly needed (when new resident data arrives). The
saved list then contains only the genuine resident textures (typically 20-40).
