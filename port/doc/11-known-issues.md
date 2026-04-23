# Known Issues

Current bugs and limitations with root cause analysis.

---

## 1. Snake Does Not Move

**Symptom**: Snake spawns in the stage but does not respond to directional input. The character model may appear but remains stationary.

**Root cause**: `HZD_GetAddress` returns 0 for Snake's position (see issue #2). The movement system depends on valid collision data: `HZD_StepCheck` needs to find floor polygons under Snake to allow movement, and the floor height query determines where Snake can walk. If the collision handle is not producing correct results, the movement vector is zeroed out.

Additionally, the `GM_GameStatus` flag `STATE_PADRELEASE` was previously stuck, though this has been fixed. Remaining movement issues are collision-related.

**Status**: Blocked by issue #2.

---

## 2. HZD_GetAddress Returns 0 (Zone Lookup Failure)

**Symptom**: `HZD_GetAddress` always returns 0 for Snake's world position. This sets `GM_PlayerAddress = 0`.

**Root cause**: The zone lookup iterates `HZD_ZON` entries and checks if the query position falls within each zone's bounding box (x, z, w, h). The zone data contains only scalar fields (no pointers), so the 64-bit loader does not need to convert it -- it points directly into the raw buffer. Possible causes:

1. Snake's initial spawn position (set by GCL `start` or `chara` command) may be in a coordinate space that does not match the zone coordinate space
2. The zone bounding box check may use different axes than expected (HZD uses x/z as horizontal, y as vertical, which may not match the world transform)
3. The zones pointer itself may be offset incorrectly if the raw buffer base address calculation is wrong

**Impact**: All enemy AI pathfinding is broken. Enemies cannot compute routes to the player. Triggers and event zones may also fail.

---

## 3. GCL Proc ID Corruption (0x7F0000xx)

**Symptom**: GCL `bind` callbacks registered during stage initialization have corrupted proc IDs of the form `0x7F0000xx` instead of valid GCL proc hashes.

**Root cause**: When `bind` processes its parameters, it reads a value that was encoded via `gcl_store_ptr()` as a pointer table index (marker `0x7F000000 | idx`). This index is stored as the proc ID in the `HZD_BIND` struct's `field_14_proc_and_block` field. When a trigger fires and calls `GCL_ExecProc` with this corrupted ID, `get_proc_block` cannot find a matching proc and returns NULL.

The fix requires either:
- Resolving the pointer at bind registration time and extracting the actual proc ID
- Storing the full pointer alongside the bind entry

**Impact**: Stage trigger callbacks (zone enter/exit, trap activation) do not fire. This breaks scripted events, door triggers, alert zones, and cutscene triggers.

---

## 4. Dangling DG_OBJS in Render Queue

**Symptom**: The software renderer encounters `DG_OBJS` entries whose memory has been freed (fields contain 0xFCFCFCFC pattern). Detected by the freed-memory check in `DG_BoundObjs`.

**Root cause**: When an actor is destroyed, it frees its `DG_OBJS` via the die callback. However, the render queue (ordering table entries, `DG_OBJS` linked lists) may still contain pointers to the freed object if the object was not properly dequeued before destruction. The port's `malloc`/`free` implementation fills freed memory with 0xFC, making this detectable.

On PSX, this was masked because memory was recycled from a pool and freed memory often still contained valid-looking data until overwritten. On the port, freed memory is poisoned.

**Impact**: Occasional rendering glitches or skipped objects. Currently handled by detecting and skipping, but the root cause (improper dequeue on actor destruction) remains.

---

## 5. "Where Is Snake ????" Spam

**Symptom**: The message "Where Is Snake ????" prints to console every frame.

**Root cause**: This is a direct consequence of issue #2. Enemy AI code in `source/enemy/think.c` (and related files) calls `HZD_GetAddress` for the player's position. When it returns 0, the AI prints this debug message. `GM_PlayerAddress` being 0 means the enemy AI's "find player" logic cannot determine which zone Snake is in.

**Impact**: Console spam, minor performance impact from repeated printf. Resolves automatically when issue #2 is fixed.

---

## 6. shakemdl Model Hash Mismatch

**Symptom**: `shakemdl.c` (camera shake model effect) fails to load its model, or loads the wrong model.

**Root cause**: The actor reads a GCL parameter to determine which model to load. The parameter hash `0x6D` ('m') is being parsed incorrectly, possibly returning a pointer table index instead of the actual model cache ID. This may be related to the GCL 64-bit pointer table issue (issue #3).

**Impact**: Camera shake effects do not display correctly. Non-critical for gameplay.

---

## 7. 31 Stages with Raw PSX Addresses in Character Entries

**Symptom**: Actors defined in 31 stage files do not spawn because their constructor addresses are raw PSX values (e.g., `0x800ABCDE`).

**Root cause**: These stages reference actors whose source code has not been decompiled into named C functions. On PSX, the linker resolved these to absolute addresses. In the port, these addresses are meaningless -- they point to unmapped memory on macOS.

**Affected stages**: Mostly later-game and variant stages where overlay decompilation is incomplete.

**Impact**: Stage-specific actors (enemies, objects, effects) do not spawn in these 31 stages. The stage still loads and core systems work, but the stage will be empty of its unique content.

---

## 8. No Sound Effects

**Symptom**: No audio plays during gameplay. `PcmOpen` returns -1.

**Root cause**: The SPU emulator (`port/sound/spu_emu.c`) implements low-level ADPCM decoding and voice mixing, but the higher-level sound driver interface (`PcmOpen`, `PcmRead`, `PcmClose`) is not fully connected. `PcmOpen` attempts to open a sound bank file and returns -1 (failure) because the file path resolution from PSX CD-ROM paths to local filesystem paths is not implemented for sound data.

The sound data files (VXX.DAT, ZMOVIE.STR audio tracks) need to be located and their format parsed. The SPU emulator can decode ADPCM samples once they are loaded into SPU RAM, but the pipeline from file to SPU RAM is incomplete.

**Impact**: No sound effects, no voice, no BGM. Game is silent.

---

## 9. No FMV Playback

**Symptom**: FMV sequences are automatically skipped.

**Root cause**: `FS_StreamIsForceStop` is set to 1, which causes all stream open/read operations to immediately return "done". This was intentionally set to prevent hangs, since the STR (MDEC video) streaming pipeline is fully stubbed.

Implementing FMV would require:
- STR file demuxing (interleaved MDEC video + ADPCM audio)
- MDEC decompression (Huffman + IDCT, similar to JPEG)
- Frame-accurate audio/video sync
- Display integration with the software renderer

**Impact**: Opening movies, cutscene movies, and codec sequences with video are skipped. The game proceeds as if the movie finished.

---

## 10. Five Source Files Still Excluded

**Symptom**: Five source files are excluded from the port build due to compilation errors or conflicts.

**Root cause**: These files contain constructs that do not compile under clang targeting 64-bit macOS:
- R-variant overlay files (d18ar, s08br, s19br) have duplicate symbols with their non-R counterparts when linked statically
- Remaining files may have unresolved PSX-specific dependencies

**Impact**: Actors defined exclusively in these files will not be available. This affects specific R-variant stage configurations (alternate versions of certain stages).

---

## 11. s02c Hangar Missing Floor and Walls

**Symptom**: In stage s02c (Tank Hangar), the floor and some walls of one specific
room are invisible — you can see through them into adjacent rooms. Other rooms
in the same stage render correctly, and other stages (s00a, s01a, s03a) work.

**Root cause**: The problematic map object is `DG_FLAG_ONEPIECE` with
`objs->world` = identity and `t=(0,0,0)`. The model vertices are stored in
absolute world coordinates (e.g., 19500, 4250, -2000). The port's renderer
computes `screen_mat = eye_inv * objs->world`, which with identity world gives
just `eye_inv` — and `chanl->eye_inv` is the raw matrix before
`DG_AdjustOverscan` is applied. Result: screen coords in the thousands, well
beyond the ±320/±224 cull range. All faces marked offscreen and culled.

The proper fix requires using `obj->screen` (which `DG_ScreenChanl` computes
correctly via scratchpad) for ONEPIECE objects. But this breaks other objects
whose `obj->screen` is zeroed because `DG_ScreenChanl`'s scratchpad-based
computation is unreliable when the port's scratchpad mmap fails (see the
`port: WARNING: scratchpad mmap failed` log line).

**Root-cause fix**: Fix the scratchpad mmap failure. With a working
scratchpad, `DG_ScreenChanl` produces correct `obj->screen` matrices for all
objects, and the renderer can use them directly.

**Impact**: One room in s02c is missing its floor and some walls.

---

## 12. Stage Walls Too Bright in `map -c` Scenes (e.g. s01a heliport)

**Symptom**: In stages whose script reloads maps via GCL `map -c`
(no-preshade), the stage geometry (walls, floors, static props) renders at
full texture brightness in the port while the emulator shows them visibly
darker / tinted. Actors and preshaded props in the same scene match the
emulator; only the stage map itself looks wrong. Most obvious on s01a at
night (heliport outdoor).

**Root cause**: Not yet identified. Audit confirmed:

- The stage's compiled GCL emits `P'c'` (not `P's'`), so preshade is
  correctly skipped on both PSX and port.
- `DG_InitPolyGT4Pack` writes the same 0x80 neutral default on both
  platforms. No code path populates `obj->rgbs` for these maps.
- Actor-triggered preshade (elevators, doors, walls) fires identically
  on both.

So the pipeline is behaving the same; the visible difference must come
from somewhere outside the normal per-vertex lighting path. Suspected
mechanisms (none confirmed):

1. A PSX-only 2D overlay TILE that tints the scene and isn't
   blending correctly in the port.
2. A CLUT/palette swap that selects a night-palette variant of the
   same textures on PSX.
3. Subtle modulation-math difference between the port's GL fragment
   shader and the PSX GPU's `(texel * vcol) / 128` for vcol=0x80.

**Impact**: Night outdoor stages (s01a and similar) look too bright.
Interior / bright stages (s03a etc.) are less affected because their
textures and ambient lighting are naturally bright to begin with.

**Investigation pointers**: see doc `13-shading-and-lighting.md` §6, and
the GCL raw-byte dump instrumentation in `port/game_script_fix.c`
(removed after April 2026 session but easy to re-add).
