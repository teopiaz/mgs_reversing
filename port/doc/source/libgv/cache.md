---
file: source/libgv/cache.c
---

# `libgv/cache.c` — file cache + extension-loader registry

The cache decouples *file identity* (a 32-bit hash + 1-byte
extension) from *memory location*, so two pieces of code can refer
to the same loaded asset without copying or coordinating.

```
       +---------+        +-----------------+        +-------------+
       |  file   |  hash  |  GV_CacheSystem |  ptr   |  loaded     |
       | s01.kmd |------->|                 |------->|  KMD bytes  |
       +---------+        |  128 tags       |        +-------------+
                          +-----------------+
```

## Cache ID

```c
int GV_CacheID(int name_hash, int ext_first_char);
```

Builds a 32-bit ID:

```
  bit 31:24      bit 23:16        bit 15:0
  reserved   |   ext - 'a'    |   strcode hash low 16 bits
              (uppercase  +32)
```

A file `data/door01.kmd` is identified by `GV_CacheID(GV_StrCode("door01"), 'k')`.
Hash collisions on the low 16 bits would in theory be possible, but
the hash function (`gv_strcode`) is engineered so that the asset
filename set in the disc image happens to be collision-free.

## Tag table

```c
#define MAX_CACHE_TAGS 128
typedef struct GV_CACHE_TAG {
    int   id;     // bit 24 = RESIDENT_FLAG; bits 23:0 = cache ID
    void *ptr;    // pointer into normal-memory heap
} GV_CACHE_TAG;
extern GV_CACHE_PAGE GV_CacheSystem;       // tags[MAX_CACHE_TAGS]
```

128 slots, **open-addressed hash table** with linear probing:

```c
start = id % 128;
for i in 0..127:
    if tag.id == 0: free slot — first-found is remembered
    if tag.id == id: hit — return tag
    advance, wrap at 128
```

`id == 0` is the sentinel for "unused". Real IDs are guaranteed
non-zero by the hash + extension construction.

## Loaders

```c
#define GV_MAX_LOADERS  26       // 'a'..'z'
typedef int (*GV_LOADFUNC)(unsigned char *data, int id);
extern GV_LOADFUNC GV_LoaderFunctions[GV_MAX_LOADERS];
```

One callback per file extension (first character only):

| Ext | Loaded by | Used for |
| --- | --------- | -------- |
| `k` | KMD loader | 3D models (`*.kmd`) |
| `m` | Motion loader | Skeletal animations (`*.mds` etc.) |
| `p` | PCX loader | Texture atlases |
| `h` | HZD loader | Collision data |
| `a` | ABS loader | Sound waveforms |
| `r` | RADIO.DAT loader | Codec dialogue |
| ... | (and more) | |

`GV_SetLoader('k', LoadKmd)` registers; `GV_LoadInit` invokes the
right one based on the second char of the file's extension.

## Loading flow — `GV_LoadInit`

```c
GV_LoadInit(ptr, id, region):
    if region == NOCACHE:
        // load but don't register — used for raw byte buffers
        return load_func(ptr, id)
    else:
        // normal path — register first then load
        if id already exists: ERROR
        SetCurrentTag(ptr, id, region)   // CACHE bit, or |= RESIDENT
        ret = load_func(ptr, id)         // loader may rewrite ptr (KMD!)
        if ret <= 0: clear tag
        return ret
```

`GV_REGION_RESIDENT` sets the high bit `0x01000000` on the tag ID
so `GV_SaveResidentFileCache` knows to preserve it across stage
transitions.

### Important port behaviour

The port's KMD/HZD loaders **rewrite** `tag->ptr` after rebuilding
the asset to a 64-bit-pointer-friendly representation. The original
`GV_SetCache` rejected updates if the tag already existed; the port
patched it (line 174) to overwrite. See
[memory project notes](#) for context.

## Resident vs cache

```c
enum GV_CACHE_REGION {
    GV_REGION_NOCACHE,    // no tag added
    GV_REGION_CACHE,      // cleared on FreeCacheSystem
    GV_REGION_RESIDENT,   // survives FreeCacheSystem
};

void GV_SaveResidentFileCache(void);    // copy resident tags out
void GV_FreeCacheSystem(void);          // wipe + restore residents
```

Stage transition pattern:

```
1. GV_SaveResidentFileCache()   // snapshot RESIDENT entries
2. GV_FreeCacheSystem()          // wipe everything
   --> resident entries automatically reload from snapshot
3. load new stage's CACHE entries
```

The resident snapshot lives in resident memory (allocator from
`resident.c`).

## Public API

```c
int   GV_CacheID(int name_hash, int ext);
int   GV_CacheID2(const char *name, int ext);     // hashes name
int   GV_CacheID3(char *filename);                 // parses ext too
void *GV_GetCache(int id);                         // returns NULL on miss + prints
int   GV_SetCache(int id, void *ptr);              // 0 = ok, -1 = full
void  GV_SetLoader(int ext, GV_LOADFUNC func);
void  GV_InitLoader(void);
void  GV_InitCacheSystem(void);
void  GV_SaveResidentFileCache(void);
void  GV_FreeCacheSystem(void);
int   GV_LoadInit(void *ptr, int id, int region);
```

Most callers use `GV_GetCache(GV_CacheID(name, ext))` after a
preceding load.

## Pitfalls

- **128-slot limit.** A stage that tries to register more than 128
  cached files will get `id conflict` at the 129th and the
  asset will be unloadable. In practice no stage hits this; loaders
  share KMDs aggressively.
- **The 16-bit truncation.** `GV_CacheID` only mixes the *low* 16
  bits of the strcode hash. Two filenames whose hashes match in
  the low 16 bits but differ in the high 16 will collide. The
  shipped asset set is engineered to avoid this, but adding a new
  file with the wrong name could trigger it.
- **Loader return value.** Returning ≤0 from a loader cancels the
  cache insertion (tag is cleared). This is how a loader signals
  "data is bad, don't keep me".
- **Port: pointer rewriting.** If you write a custom loader that
  needs to enlarge or relocate the buffer, you MUST `GV_SetCache(id,
  new_ptr)` to update the tag. The port's KMD/HZD loaders do this.

## See also

- [memory.md](memory.md) — the heap that backs the loaded data.
- [resident.md (TODO)] / `source/libgv/resident.c` — bump allocator
  for the resident snapshot.
- [`source/libfs/`](../libfs/README.md) — the disc-streaming layer
  that feeds bytes into loaders.
