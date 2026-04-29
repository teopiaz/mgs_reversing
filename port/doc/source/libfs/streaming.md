---
file: source/libfs/libfs.c (+ port/libfs/libfs.c)
---

# `libfs/` — disc / file streaming

The PSX disc filesystem layer. On PSX, the CD-ROM is read sector-
by-sector; libfs builds higher-level abstractions on top:

- File-by-name lookup (no real "directory listing" — names are
  hashed).
- Asynchronous read scheduling (overlap CD seek with computation).
- Multi-track audio + data dual-mode.

## Public API

```c
int  FS_OpenFile(const char *name, FS_FILE *file);
int  FS_ReadFile(FS_FILE *file, void *buf, int size);
int  FS_SeekFile(FS_FILE *file, int offset, int whence);
int  FS_CloseFile(FS_FILE *file);
```

Plus async variants for overlapping CD reads with rendering.

## DAR archives

Many MGS files are bundled into `*.dar` archives — header lists
embedded files + offsets. The libfs reader transparently follows
the dar layer when a file path crosses an archive boundary
(`stage/s01a.dar:s01a.kmd`).

## Cache integration

When a file is loaded via FS, the result feeds into the cache
system (`libgv/cache.c`):

```
FS_OpenFile + FS_ReadFile → buffer
              ↓
          GV_LoadInit(buffer, id, region)
              ↓
          loader callback (KMD parser, PCX decoder, …)
              ↓
          GV_SetCache(id, processed_buffer)
```

The cache makes a single load feed multiple consumers — every actor
that needs `s01a.kmd` shares the same buffer.

## Streaming the audio (CD-DA)

The PSX CD has separate data and audio tracks. libfs handles the
*data* track via sector reads; audio is played via dedicated CD-DA
hardware (separate path through `sd_drv.c`).

## Port replacement

`port/libfs/libfs.c` replaces the entire backend with stdio:

- File lookup → host filesystem path translation
  (`port/datadir/` → original `cdrom:/`)
- Sector reads → `fseek + fread`
- Async → synchronous (no real benefit on host SSD)

The port preserves the FS_* API exactly, so MGS code that opens
files works unchanged. Just the bytes come from disk instead of
disc.

## Pitfalls

- **No real directory enumeration.** Files are looked up by name
  hash; you can't `readdir()` a folder. Tools that need to list
  files (e.g. `tools/extract_disc.py`) parse the directory record
  format directly.
- **DAR archives have alignment.** Each file inside a DAR is
  sector-aligned. Reading past EOF returns garbage.
- **CD seek is slow.** On PSX, randomly accessing files thrashes
  the head. The shipped disc layout puts related files adjacent.
- **Async reads complete out-of-order.** Don't assume FIFO; check
  status per-handle.

## See also

- [`port/libfs/libfs.c`](../../../../port/libfs/libfs.c) — port
  replacement.
- [`source/libgv/cache.md`](../libgv/cache.md) — what consumes FS
  output.
- [`source/sound/architecture.md`](../sound/architecture.md) — CD
  audio path.
