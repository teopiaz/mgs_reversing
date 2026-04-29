# `source/libfs/` — disc & streaming I/O

The PSX CD-ROM access layer + the streaming subsystem feeding
voice / cinematic data per frame.

## Files

| File | Role |
| ---- | ---- |
| [`cdbios.c`](../../../../source/libfs/cdbios.c) | Wraps PSX BIOS CD calls (`CdRead`, `CdControl`, `CdSync`). |
| [`fscd.c`](../../../../source/libfs/fscd.c) | Game-side CD entry points — `FS_LoadFileRequest`, `FS_LoadFileSync`, `MakeFullPath`. |
| [`fshd.c`](../../../../source/libfs/fshd.c) | Hard-disk variant — used for some prototypes. |
| [`stream.c`](../../../../source/libfs/stream.c) | Streaming subsystem — `FS_StreamGetData`, `FS_StreamClear`, `FS_StreamGetTick`. |
| [`memfile.c`](../../../../source/libfs/memfile.c) | RAM-backed file emulation. |
| [`movie.c`](../../../../source/libfs/movie.c) | ZMOVIE.STR (FMV) playback dispatch. |
| [`cdstage.c`](../../../../source/libfs/cdstage.c) | Stage-data loading — DATACNF parser. |
| [`datacnf.h`](../../../../source/libfs/datacnf.h) | DATACNF format reference (`'r'` / `'g'` / `'c'` / `'n'` archive tags). |
| [`file.cnf`](../../../../source/libfs/file.cnf) | Static `fs_file_info[]` array — the disc's master file table. |

## Key APIs

```c
/* Synchronous file load — read sectors into RAM. */
void FS_LoadFileRequest(int fileno, int offset, int size, void *buffer);
int  FS_LoadFileSync(void);

/* Stage data — entire DATACNF for a stage in one go. */
void FS_LoadStageRequest(const char *stage_name);
int  FS_LoadStageSync(void);

/* Streaming — per-frame VOX / DEMO data. */
void *FS_StreamGetData(int target_type);   /* returns next block of `target_type` or NULL */
void  FS_StreamClear(void *data);          /* releases the block back to ring buffer */
int   FS_StreamGetTick(void);              /* current stream tick counter */
int   FS_StreamGetTop(int is_demo);        /* base sector for VOX (0) or DEMO (1) */
```

## File table

`fs_file_info[FS_MAX_FILEID + 1]`:

```c
{ "STAGE.DIR",  0 },     // 0 — the stage directory
{ "RADIO.DAT",  0 },     // 1 — codec dialogue
{ "FACE.DAT",   0 },     // 2 — codec portraits
{ "ZMOVIE.STR", 0 },     // 3 — pre-rendered FMVs
{ "VOX.DAT",    0 },     // 4 — voice samples
{ "DEMO.DAT",   0 },     // 5 — streamed cinematics
{ "BRF.DAT",    0 },     // 6 — briefing scenes
```

Each `pos` is set at boot by `FS_ResetCdFilePosition` based on
the disc's ISO9660 directory.

## Stream format (DEMO.DAT / VOX.DAT)

Both stream files use the `{type:8, size:24LE}` block format
covered in [doc/demo/10-dmo-format.md](../../demo/10-dmo-format.md).
`FS_StreamGetData(target_type)` walks the stream's ring buffer
looking for the next block of the requested type.

The CD reads sectors continuously into a 96 KiB ring buffer at
~30 Hz; `FS_StreamGetData` consumes from the read side. The
streamer is fed by an interrupt handler tied to the SPU (each
SPU IRQ kicks the next sector read).

## Port replacement

Almost all of `source/libfs/` is **replaced** in the port by
[`port/libfs/libfs.c`](../../../../port/libfs/libfs.c) which
implements:

- `FS_LoadFileRequest` etc. via stdio + ISO9660 reading.
- `FS_StreamGetData` via in-memory buffer of the relevant DAT
  sector range.

The original disc-only code stays in source/ for matching
builds. Port builds link only the engine-side calls (which the
port's libfs satisfies).

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [streaming.md](streaming.md) | DAR archives, async reads, cache integration, CD-DA |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`port/libfs/libfs.c`](../../../../port/libfs/libfs.c) — port
  replacement.
- [`port/doc/demo/09-streamed-demos.md`](../../demo/09-streamed-demos.md)
  — the streamed-cutscene path's reliance on this folder.
- [`port/doc/demo/10-dmo-format.md`](../../demo/10-dmo-format.md)
  — block format reference.
