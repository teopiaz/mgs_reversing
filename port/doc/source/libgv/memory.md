---
file: source/libgv/memory.c + resident.c
---

# `libgv/memory.c` — the memory subsystem

PSX has 2 MiB of main RAM and **no malloc**. Everything dynamic is
satisfied from one of three hand-managed heaps. `memory.c` is the
allocator; `resident.c` is the bump-allocator for static data that
outlives stages.

## The three heaps

```c
enum {
    GV_PACKET_MEMORY0  = 0,   // 188 KiB  @ 0x80182000
    GV_PACKET_MEMORY1  = 1,   // 188 KiB  @ 0x801b1000
    GV_NORMAL_MEMORY   = 2,   // 428 KiB  @ 0x80117000
    GV_MEMORY_MAX      = 3,
};
```

| Heap | Purpose | Lifetime model |
| ---- | ------- | -------------- |
| Packet 0 / 1 | Per-frame DG_PRIM packets (drawables) | Wiped at end of frame; double-buffered for next-frame submission |
| Normal | Everything else: actors, KMDs, OBJs, TARGETs | Lifetime tied to allocators (actor-system, cache) |

The dual packet heaps swap each frame: one is being filled by the
current frame's `DG_GetXXXPacket` calls; the other is being consumed
by the GPU as the previous frame's command list. `GV_SetPacketTempMemory` /
`GV_ResetPacketMemory` (in `gvd.c`) flip them.

## `GV_HEAP` layout

```c
#define MAX_ALLOC_UNITS 512

typedef struct GV_HEAP {
    int      flags;           // FLAG_DYNAMIC | FLAG_VOIDED | FLAG_FAILED
    void    *start;
    void    *end;
    int      used;            // number of valid `units[]` entries
    GV_ALLOC units[MAX_ALLOC_UNITS];
} GV_HEAP;

typedef struct GV_ALLOC {
    void        *start;       // start of this block
    unsigned int state;       // FREE | VOID | USED, or "user-pointer" sentinel
} GV_ALLOC;
```

A heap is described by a sorted array of `(start, state)` records.
The block length is implicit: `units[i+1].start - units[i].start`.
The last entry is always `(end, USED)` as a sentinel so the size of
the last real block is computable.

State values:

| `state` | Meaning |
| ------- | ------- |
| `GV_ALLOC_STATE_FREE = 0` | Free block — combinable with neighbours on free |
| `GV_ALLOC_STATE_VOID = 1` | Logically freed but not compactable yet (used by `GV_DelayedFree`) |
| `GV_ALLOC_STATE_USED = 2` | In-use, allocated by `GV_Malloc` (single-pointer ownership) |
| any pointer ≥ 3 | In-use, allocated by `GV_AllocMemory2` — **the state IS the user-pointer-pointer**: dynamic compaction will write the new address back into `*(void**)state` |

That last variant is the key trick: dynamic mode is "movable
allocations". The user passes a `void **` to `GV_AllocMemory2`; the
heap stores the pointer-to-pointer in `state`. When
`GV_ResetDynamicMemorySystem` compacts, it walks units, copies each
movable block to its new (denser) position, and writes
`*(void**)state = new_addr`. Caller's local pointer is updated for
free.

## Allocation — `GV_AllocMemory` / `GV_AllocMemory2`

```c
size = (size + 15) & ~15;                    // align 16
alloc = GV_FindFreeMemory(heap, size);       // first-fit search
if (!alloc) { heap->flags |= FAILED; return NULL; }
if (block-larger-than-need) GV_SplitAllocation(heap, alloc);
alloc->state = (int)pstart;                  // USED-sentinel or user-pp
return alloc->start;
```

- 16-byte alignment is required by the GTE / DMA.
- First-fit, *not* best-fit — fast but more fragmentation.
- On failure, sets `FAILED` flag instead of returning silently. The
  flag is later picked up by `GV_ClearMemorySystem` which runs
  defragmentation only when needed.
- `MAX_ALLOC_UNITS = 512` — exhausting this also fails.

## Free — `GV_FreeMemory` / `GV_FreeMemory2`

```c
GV_FreeMemory(which, addr):
    alloc = FindAllocation(heap, addr)         // binary-search by start
    if !alloc or alloc->state == FREE: return
    alloc->state = FREE
    units = 0
    if prev is FREE: units++   else: merge_target++   // merge backward
    if next is FREE: units++                          // merge forward
    if units: MergeMemory(heap, merge_target, units)  // shifts array
```

Adjacent free blocks coalesce immediately — keeps fragmentation
under control without periodic compaction.

`GV_FreeMemory2` is the *defer* variant: it marks the block VOID
instead of FREE, sets `FLAG_VOIDED` on the heap, and returns. The
block is reclaimed during the next `GV_ClearMemorySystem` pass.
Used by `GV_DelayedFree` for actor cleanup that can't safely run
inline.

## Compaction

Two strategies, both triggered from `GV_ClearMemorySystem`:

### `GV_ResetVoidedMemorySystem`

Walks units; merges every VOID/FREE run into one FREE block, drops
unit count. Doesn't *move* user data — it just collapses the
metadata array. Used after a batch of `GV_FreeMemory2` calls.

### `GV_ResetDynamicMemorySystem` (fragments-killer)

Walks units in sorted order; for each USED block whose state is a
user-pointer (≥3), `GV_CopyMemory`'s the data left to be contiguous
with the previous USED block, then writes the new address back into
`*(void**)state` so the user's local pointer is updated. After this
pass the heap is fully compacted.

This is *only* safe to do during a "quiescent" moment (between
levels, while paused). It's not invoked every frame.

## `GV_CopyMemory` / `GV_ZeroMemory`

The PSX `memcpy` and `memset` replacements. Both written to use a
`Unit { long d0, d1, d2, d3; }` 16-byte struct copy where the source
and dest alignments match — the MIPS R3000 had no SIMD, but the
optimiser would emit `lwc1`/`swc1` cop2 loads/stores against this
struct, which were faster than per-byte loops.

The port keeps these functions verbatim; a future cleanup could
replace them with platform `memcpy`/`memset` since the struct trick
no longer matters on x86_64/ARM64.

## Resident memory — `resident.c`

Tiny bump allocator (~64 lines) for *permanently* live data:
RADIO.DAT contact tables, font glyphs, the cache-resident-tag list.

```c
void *GV_AllocResidentMemory(long size);   // bump
void  GV_SaveResidentTop(void);             // checkpoint
void  GV_InitResidentMemory(void);          // rewind to checkpoint
```

The "checkpoint" pattern lets the engine load global data at boot,
mark the watermark, then load per-stage data that will be reset
between stages. Resetting just rewinds the bump pointer; nothing to
compact.

## Globals

| Symbol | What |
| ------ | ---- |
| `MemorySystems_800AD2F0[GV_MEMORY_MAX]` | The three GV_HEAP records. Address-named — RE'd. |
| `dword_800AB93C` | Unused short (legacy MTS reference?). |

## Pitfalls

- **Movable allocations require a stable backing pointer.** If
  caller stores the result in two places, only the one passed via
  `pstart` will be updated by compaction. Don't alias movable
  allocations.
- **Don't free across the wrong heap.** `GV_FreeMemory(2, ptr)` if
  `ptr` came from heap 0 silently returns — the FindAllocation
  range-check filters it out.
- **The `state == int_sentinel` trick means user pointers must not
  be < 3.** In practice every legal `void**` is in high RAM, so
  this is fine — but worth knowing if porting to an environment
  with low-address pointers.

## Port notes

The port runs unchanged on top of the heap allocator — `memory.c` is
plain C. It still owns 0x6b000 of "PSX RAM" carved from a host
allocation. The MOVABLE-allocation trick is preserved (some code
paths assume it).

## See also

- [actor.md](actor.md) — `GV_NewActor` is the dominant memory
  client.
- [cache.md](cache.md) — file cache holds movable pointers,
  benefits from compaction.
- [02-game-loop.md](../02-game-loop.md) — `gamed.c` calls
  `GV_ResetMemory` / `GV_ClearMemorySystem` at WAIT_LOAD ↔ WORKING
  transitions.
