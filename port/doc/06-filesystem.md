# File System and Data Loading

This document describes the PSX disc layout, the STAGE.DIR format, the stage
loading pipeline, the cache system, and how the port replaces CD-ROM access
with direct file I/O.

---

## 1. Disc Layout

The game disc contains the following key data files:

| File          | Contents                                          |
|--------------|---------------------------------------------------|
| STAGE.DIR    | All stage data (geometry, textures, scripts, etc.)|
| RADIO.DAT    | Codec radio conversation data                     |
| FACE.DAT     | Codec character face textures                     |
| ZMOVIE.STR   | FMV video streams                                 |
| VOX.DAT      | Voice audio data                                  |
| DEMO.DAT     | Demo/replay data                                  |
| BRF.DAT      | Briefing/mission data                             |

All files are opened at startup. The port maps these to local files under a
configurable data path (default: `data/disc1/MGS/`).

---

## 2. STAGE.DIR Format

STAGE.DIR is a single large file containing all stage data packed sequentially.

### Directory Header

The first sector (2048 bytes) contains the directory table:

```
Offset 0:  uint32_t table_size   (total bytes of directory entries)
Offset 4:  DirEntry entries[]    (repeated)
```

Each `DirEntry` is 12 bytes:
```
Offset 0:  char name[8]     Stage name (null-padded, e.g. "init\0\0\0\0")
Offset 8:  int  offset      Sector offset within STAGE.DIR
```

The number of stages is `table_size / 12`. The game contains approximately 95
stages indexed in this table. Stage names follow the convention:
- `init` — Initial startup/title
- `s00a`, `s01a`, etc. — Playable stages ("s" prefix)
- `d00a`, `d01a`, etc. — Cutscene stages ("d" prefix)
- `*r` suffix — RED (alternate) variants
- `select` — Stage select screen (VR disc)

### Stage Data

Each stage's data begins at the sector offset given in the directory. The first
sector of each stage contains a DATACNF header.

---

## 3. DATACNF Format

The DATACNF header describes the contents of a stage:

```c
typedef struct {
    int version;      /* format version */
    int size;         /* total size in sectors */
    DATACNF_TAG tags[]; /* variable-length array of tags, terminated by mode=0 */
} DATACNF;
```

Each tag describes one data section:

```c
typedef struct {
    unsigned short id;    /* resource ID (hash) */
    char           ext;   /* file extension character: 'k','o','h','p','g', etc. */
    char           mode;  /* loading mode: 'r','n','c','s', or 0 (terminator) */
    int            size;  /* size in bytes (or offset, depending on mode) */
} DATACNF_TAG;
```

### Tag Modes

#### 'r' — Resident

Data that persists across stage transitions (e.g., player model, common
textures).

**Format**: DAR archive (sequence of `DARFILE_TAG` + data).

```c
typedef struct {
    unsigned short id;    /* resource ID */
    unsigned short ext;   /* extension character */
    int            size;  /* data size in bytes */
    /* followed by `size` bytes of data */
} DARFILE_TAG;
```

**Processing**:
1. Iterate through DAR entries.
2. Compute `cache_id = ((ext - 'a') << 16) | id`.
3. Copy data to persistent memory (`malloc`, NOT per-frame `GV_AllocMemory`).
4. Call `GV_LoadInit(data, cache_id, GV_REGION_RESIDENT)` to invoke the
   registered loader for the extension.
5. Set `FS_ResidentCacheDirty = 1`.

#### 'n' — Nocache

Texture data uploaded directly to VRAM. Not stored in the cache system.

**Format**: Same DAR archive format as 'r'.

**Processing**: Each DAR sub-file (typically ext='p', PCX textures) is passed
to `GV_LoadInit(data, cache_id, GV_REGION_NOCACHE)`, which calls
`DG_LoadInitPcx` to decode and upload to VRAM.

#### 'c' — Cache

Standard cached resources: models, scripts, collision data, images.

**Format**: Sequential entries with offset-based addressing. Unlike 'r' and 'n',
'c' mode tags use the `size` field as an OFFSET from the c-region base (not an
absolute size). The size of each entry is computed as `next_tag.size - this_tag.size`.

A special terminator tag with `ext = 0xFF` marks the end of the c-region; its
`size` field gives the total c-region size.

**Processing**:
```
c_base = data_ptr at first 'c' tag

For each 'c' tag (until ext == 0xFF):
    entry_offset = tag->size
    entry_size   = next_tag->size - entry_offset
    entry_data   = c_base + ALIGN4(entry_offset)
    cache_id     = ((tag->ext - 'a') << 16) | tag->id

    GV_LoadInit(entry_data, cache_id, GV_REGION_CACHE)
```

`GV_LoadInit` dispatches to the registered loader based on the extension:

| Extension | Loader              | Data type                    |
|-----------|--------------------|-----------------------------|
| 'k'       | `DG_LoadInitKmd`   | KMD 3D model                |
| 'o'       | `DG_LoadInitOar`   | OAR animation archive       |
| 'h'       | `HZD_LoadInitHzd`  | HZD collision map           |
| 'p'       | `DG_LoadInitPcx`   | PCX texture                 |
| 'l'       | (LIT loader)       | Lighting data               |
| 'i'       | `DG_LoadInitImg`   | IMG tilemap                 |
| 'g'       | (GCL loader)       | GCL script bytecode         |
| 's'       | `DG_LoadInitSgt`   | SGT stage data              |
| 'n'       | `DG_LoadInitNar`   | NAR animation data          |

#### 's' — Sound/Overlay

Sound data and stage overlay binaries.

| Extension | Handler             | Data type                    |
|-----------|--------------------|-----------------------------|
| 'w'       | `SD_WavDataLoadInit`| WAV sample data → SPU RAM  |
| 'e'       | `SD_SeDataLoadInit` | SE (sound effect) data      |
| 'm'       | `SD_SngDataLoadInit`| Song/music sequencer data   |
| 'b'       | `GM_LoadInitBin`    | Stage overlay binary code   |

The 'b' extension is special: it loads the stage's executable overlay, which
contains actor constructors and stage-specific logic. After loading,
`StageCharacterEntries` is set to the overlay's actor table.

---

## 4. Cache System

**File**: `source/libgv/cache.c`

### Cache Table

```c
typedef struct {
    int   id;       /* resource ID (extension << 16 | hash) */
    void *ptr;      /* pointer to loaded data */
} GV_CACHE_TAG;

static GV_CACHE_TAG GV_CacheTags[128];
```

**Lookup**: Hash table with `id % 128` and linear probing.

**`GV_SetCache(id, ptr)`**: Store a resource in the cache. Called by loaders
after processing raw data.

**`GV_GetCache(id)`**: Look up a cached resource by ID. Returns `ptr` or NULL.

### RESIDENT_FLAG

```c
#define RESIDENT_FLAG 0x01000000
```

Cache entries with this flag set in their ID persist across stage transitions.
When `GV_FreeCacheSystem()` is called during a stage change, it clears all
entries EXCEPT those with `RESIDENT_FLAG`.

---

## 5. Resident Data Lifecycle

The resident data system ensures that core resources (player model, common
textures, etc.) are loaded once and reused across stages.

### Step 1: Initial Stage Load

When the 'init' stage (or first playable stage with resident data) loads:

1. The 'r' archive is processed.
2. Each entry is copied to persistent `malloc`'d memory.
3. `GV_LoadInit` runs the appropriate loader (e.g., KMD for models).
4. The loader calls `GV_SetCache(id, ptr)` to store in the cache table.
5. `FS_ResidentCacheDirty` is set to 1.

### Step 2: Save Resident Caches

In `gamed.c`, during the `WAIT_LOAD` state transition:

```c
if (FS_ResidentCacheDirty) {
    GV_SaveResidentFileCache();     /* snapshot cache entries with RESIDENT_FLAG */
    DG_SaveResidentTextureCache();  /* snapshot texture table entries */
    FS_ResidentCacheDirty = 0;      /* clear the flag */
}
```

`GV_SaveResidentFileCache()` copies all cache entries with `RESIDENT_FLAG` to a
separate saved list. `DG_SaveResidentTextureCache()` does the same for the
texture hash table (`TexSets[512]`).

### Step 3: Stage Transition

When the player moves to a new stage:

1. `GV_FreeCacheSystem()` clears the cache table (all non-resident entries freed).
2. `GV_LoadResidentFileCache()` restores the saved resident entries back into
   the cache table.
3. `DG_LoadResidentTextureCache()` restores saved texture entries.
4. The new stage's data is loaded normally; its 'c' entries fill in the
   stage-specific slots.

### Bug: Flag Timing

**Problem**: The original port code cleared `FS_ResidentCacheDirty` in
`FS_LoadStageComplete()`, which runs BEFORE `gamed.c`'s check. Consequence:
`gamed.c` never saw the flag as dirty, never saved resident caches, and
resident data was lost on the first stage transition.

**Fix**: `FS_LoadStageComplete()` no longer clears the flag:

```c
void FS_LoadStageComplete(void *info)
{
    (void)info;
    /* FS_ResidentCacheDirty is cleared by gamed.c after it saves resident caches.
       Do NOT clear it here. */
}
```

---

## 6. Port File System Implementation

**File**: `port/libfs/libfs.c`

### Initialization

`FS_StartDaemon()`:
1. Opens all 7 data files (`STAGE.DIR`, `RADIO.DAT`, etc.) via `fopen`.
2. Reads the first 2048 bytes of `STAGE.DIR` to parse the directory table.
3. Populates `stage_table[MAX_STAGES]` with name-to-offset mappings.
4. Sets `FS_DiskNum = 0`.

### Stage Lookup

```c
int FS_CdGetStageFileTop(char *dirname)
```

Linear search through `stage_table[]` for a matching name. Returns the sector
offset within STAGE.DIR, or -1 if not found.

### Stage Loading

`FS_LoadStageRequest(dirname)`:

1. Look up sector offset via `FS_CdGetStageFileTop`.
2. Read the first sector to get the DATACNF header.
3. Allocate `total_size = cnf->size * 2048` bytes via `GV_AllocMemory`.
4. Read the entire stage data in one `fread` call (no async, no streaming).
5. Process DATACNF tags sequentially:
   - 'r' tags: iterate DAR archive, `malloc` + `memcpy` for each entry,
     call `GV_LoadInit`.
   - 'n' tags: iterate DAR archive, call `GV_LoadInit` (PCX decode to VRAM).
   - 'c' tags: compute offsets, call `GV_LoadInit` for each entry.
   - 's' tags with ext='b': load stage overlay binary.

Loading is synchronous (no background I/O):
- `FS_LoadStageSync()` returns 0 (always complete).
- `FS_LoadStageComplete()` is a no-op (flag preservation only).

### File Loading

```c
void FS_LoadFileRequest(int fileno, int offset, int size, void *buffer)
```

Direct `fseek` + `fread` from the appropriate data file (`dat_files[fileno]`).
`fileno` indexes into the file table: 0=STAGE.DIR, 1=RADIO.DAT, 2=FACE.DAT,
etc.

### Stubs

The following CD-ROM functions are stubbed (no-op or return defaults):
- `CDBIOS_*`: All CD BIOS functions are no-ops.
- `FS_MovieFileInit`, `FS_GetMovieInfo`: Movie/FMV system stubs.
- `FS_*Memfile*`: Memory card file system stubs.
- `FS_Stream*`: All streaming functions are stubs. `FS_StreamIsForceStop()`
  returns 1 to skip FMV sequences.

---

## 7. Data Flow Summary

```
Disc / Extracted Files
  |
  v
FS_StartDaemon()  --- opens STAGE.DIR, parses directory
  |
  v
FS_LoadStageRequest("s03a")
  |
  +-- FS_CdGetStageFileTop("s03a") --- returns sector offset
  |
  +-- fread(stage_data, total_size) --- read entire stage
  |
  +-- Process DATACNF tags:
        |
        +-- 'r' (resident) DAR entries:
        |     malloc() + GV_LoadInit() + GV_SetCache()
        |     -> DG_LoadInitKmd, DG_LoadInitPcx, etc.
        |     FS_ResidentCacheDirty = 1
        |
        +-- 'n' (nocache) DAR entries:
        |     GV_LoadInit() -> DG_LoadInitPcx -> LoadImage to VRAM
        |
        +-- 'c' (cache) sequential entries:
        |     GV_LoadInit() -> loader per extension
        |     -> KMD, OAR, HZD, PCX, GCL, IMG, LIT, SGT
        |
        +-- 's' ext='b' (overlay):
              GM_LoadInitBin() -> set StageCharacterEntries
```

After loading, the game's actor system reads `StageCharacterEntries` to
instantiate stage-specific actors (enemies, triggers, cinematics). The GCL
script system reads the cached 'g' (GCL) data to execute stage initialization
commands.
