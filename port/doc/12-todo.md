# TODO

Prioritised task list for the macOS port. Items are roughly ordered by
impact-to-gameplay; sections are grouped by nature of work.

---

## Critical (Gameplay Blockers)

### 1. Enemy AI Zone Lookup (`Where Is Snake ????`)

Enemy AI code in `source/enemy/command.c` / `think.c` repeatedly prints
`command.c: Where Is Snake ????` because its zone/address lookup for the
player is failing. Snake's own movement works (stages load, player walks
around), so the `HZD_StepCheck` / floor-height path is functional — the
bug is specific to the AI's `HZD_GetAddress`-style queries.

Investigate:
- Log the call site that prints the message and the exact query
  (position + zone id requested). Compare with the PSX zone table for
  s01a.
- Verify `GM_PlayerAddress` actually updates each frame (it should be
  set from Snake's movement path).
- Check whether enemy AI reads a *cached* stale value vs calling
  `HZD_GetAddress` directly each frame.

**Impact**: enemies are visible but don't route to the player; console
spam; scripted alerts may not fire. Movement itself is unaffected.

**Files**: `source/enemy/command.c`, `source/enemy/think.c`,
`source/libhzd/zone.c`, `port/libhzd/hzd_loader.c`.

### 2. GCL Bind Proc ID Corruption (remaining cases)

Infrastructure exists (`gcl_store_ptr` / `gcl_resolve_ptr` via the
`0x7F000000`-tagged pointer table in `port/libgcl_fix/gcl_ptr_table.h`),
and HZD bind callbacks have been partly migrated to use it. Verify
coverage:
- Audit every `GCL_GetParam*` caller that stores a pointer into a
  struct field, not just the bind handlers.
- Confirm the pointer table doesn't overflow on long play sessions
  (grows unbounded currently?).

**Impact**: partial — some callbacks work, some still resolve to
`NULL`. Breaks specific scripted events.

**Files**: `port/libgcl_fix/basic.c`, `port/libgcl_fix/gcl_ptr_table.h`,
`port/game_script_fix.c`, `source/libhzd/event.c`.

### 3. 31 Stages Still Have Raw PSX Addresses in Character Tables

For each of the 31 stages that use raw PSX addresses (e.g. `0x800ABCDE`)
in `_StageCharacterEntries[]`:
- Identify the actor constructor for each address via
  `build/functions.txt`.
- If the function is decompiled, replace the address with its name.
- Otherwise stub with a no-op constructor that logs the missing actor.

**Impact**: stage-specific actors (enemies, objects, effects) don't
spawn in affected stages; stage still loads and plays with core systems
intact.

**Files**: `source/stage/*.c`, `build/functions.txt`.

---

## Rendering Divergences (Cosmetic but Visible)

### 4. `map -c` Stage Walls Too Bright vs PSX

In stages that reload maps via GCL `map -c` (no preshade) — notably
s01a heliport — the stage geometry renders brighter than the emulator
reference. Actors and preshaded props match PSX; only the stage map
itself is too bright. GCL parsing and map loading were audited in April
2026 (see `13-shading-and-lighting.md` §6 and known-issues #12); the
divergence is *not* a parser / pipeline bug.

Next steps:
- Capture identical-pixel samples from port and emulator to
  characterise the shift (uniform darken? blue tint? texture-dependent?).
- If uniform: look for a full-screen ABR TILE in the OT that the port
  drops or blends wrong. Re-verify `apply_abr` cases 0–3 against PSX
  blending formulas.
- If texture-dependent: suspect a CLUT/palette swap at stage load.
  Trace `LoadImage` calls to see if a night-palette is written.
- Check GL fragment shader's `tex * vCol * 2.0` vs PSX GPU
  `(texel_5bit * vcol_8bit) / 128` at `vcol=0x80` for a 5→8-bit
  quantisation bias.

### 5. s02c Hangar Missing Floor / Walls

One room in s02c is missing its floor and some walls because the
scratchpad `mmap` fails at startup, causing `DG_ScreenChanl` to produce
wrong matrices for the ONEPIECE map. Fixing the scratchpad placement
would also let us un-bypass the GTE frustum tests (see #10).

**Files**: `port/port_memory.c` (scratchpad setup),
`source/libdg/screen.c`.

### 6. Dangling `DG_OBJS` in the Render Queue

The freed-memory detection in `DG_BoundObjs` is a workaround. Real fix
is to dequeue objects from the OT before their memory is freed:
- Audit `DG_DestroyObjs`; verify it removes the object from all OT
  entries.
- Investigate whether single-buffered OT (port) causes stale entries
  that double-buffered PSX OT absorbs.
- Add a "pending destroy" debug flag on `DG_OBJS` that asserts on OT
  walker hit.

**Files**: `port/libdg/vram.c`, `port/libdg/libdg_stub.c`,
`source/libdg/dgd.c`.

### 7. shakemdl Model Hash

Diagnostic logging is now in `NewShakeModelGCL` — it prints the raw
GCLCODE byte and following 4 bytes at the 'm' param site alongside
the resolved `model` int and the computed cacheID. Run a stage that
spawns a `shakemdl` actor and capture the log to see whether the
failure is upstream (wrong bytes parsed, e.g. a pointer-table index)
or downstream (correct hash but `GV_GetCache` miss — data not
resident).

Note: `gcl_store_ptr` is no longer called anywhere in the tree — only
the HZD bind persistent pointer table remains active. The original
"going through the pointer table" theory is likely stale; expect the
log to show a clean `GCLCODE_SDCODE (0x09)` followed by a 4-byte
hash, in which case the issue is a cache miss, not a parse bug.

**Files**: `source/takabe/shakemdl.c`.

---

## Audio

### 8. BGM Quality

BGM sequencer plays (notes trigger, SPU mixes) but output sounds off.
Investigate:
- ADPCM decoding waveform shape vs a known-good decoder.
- `.wvx` header parse on 64-bit — `sample_note`, `sample_tune`.
- Pitch calculation in `sd_ioset.c` `freq_set` / `freq_tbl` on 64-bit.
- ADSR envelope timing — `env_tick` runs at 44100 Hz per sample; may
  need tuning.
- `IntSdMain` tick rate (currently 3x per 30 fps frame ≈ 90 Hz) vs
  the PSX SPU IRQ rate.

**Files**: `port/sound/spu_emu.c`, `source/sound/sd_ioset.c`,
`port/libfs/libfs.c` (wave header).

### 9. VOX.DAT / DEMO.DAT Stream Audio

Codec voice and cutscene dialog use the stream system. The codec task
now runs (memory: `mts_sta_tsk` no-op + FACE.DAT parser fixed), but
`FS_StreamGetData` still can't parse the loaded data because the port
doesn't do PSX's per-sector `{size:24, type:8}` framing:

Fix — add a sector demuxer in `FS_StreamTaskStart`:
- Each 2048-byte sector has a 4-byte tag at offset 0.
- Walk sectors, extract entries, place in stream buffer.
- Types: 0x01=audio ADPCM, 0x02=stream header, 0x05=subtitle/timing,
  0x10=control.

Also needs SPU voice 21-22 (left/right) setup via `StrSpuTrans` in
`sd_str.c` — ADPCM blocks to SPU RAM ping-pong buffers.

**Files**: `port/libfs/libfs.c` (`FS_StreamTaskStart`,
`FS_StreamGetData`), `source/sound/sd_str.c` (`StartStream`,
`StrSpuTrans`), `source/libfs/stream.c` (reference).

---

## Architecture / Tech Debt

### 10. Un-Bypass the GTE Frustum Tests

`DG_BoundChanl`'s GBOUND test and `DG_BoundObjs`' BOUND test are both
`#ifdef PORT_BUILD`-skipped because the GTE `rtpt_b` + scratchpad
store path writes wrong screen coordinates on 64-bit. Every visible
group passes with `bound_mode = 2`, which is correct but wastes CPU
on off-screen geometry. Root cause is most likely the scratchpad
`mmap` / alignment issue (shared with #5); fix that and the test
should Just Work.

**Files**: `source/libdg/bound.c`, `port/port_memory.c`,
`port/psx/gte_math.c`.

### 11. Port 5 Remaining Excluded Source Files

Resolve compilation errors in files currently excluded from the build:
- R-variant overlays (`d18ar`, `s08br`, `s19br`) have duplicate
  symbols with their non-R counterparts. Use weak symbols or a
  stage-prefix rename.
- Remaining files may have lingering PSX-specific constructs.

**Impact**: actors defined exclusively in these files are unavailable,
affecting specific R-variant stage configurations.

---

## Missing Features

### 12. FMV Playback

Options in order of complexity:
- **Placeholder**: "MOVIE SKIPPED" overlay for 2 s, continue.
- **Static frame**: decode the first frame of each STR as a still.
- **Full playback**: MDEC (Huffman + IDCT), STR demuxing, A/V sync.

STR is well-documented; jPSXdec is a reference open-source decoder.

### 13. Memory Card Save/Load

Replace PSX memcard API with file I/O:
- `McOpen/McClose` → `fopen/fclose`
- `McRead/McWrite` → `fread/fwrite`
- Saves in `~/.mgs_saves/` or `port/saves/`, one flat file per slot
  (no need to emulate 8 KB × 15 block structure).

**Files**: `source/memcard/`, `port/memcard/`.

---

## Polish

### 14. Analog Stick Support

Current input maps left stick to D-pad with a dead zone. Full analog
needs:
- Pass raw analog (0-255) through to `GV_PAD.left_dx/dy`.
- Ensure Snake's movement code actually reads analog values (it does
  on PSX).
- Calibrate dead zone and sensitivity vs PSX feel.
- Right stick for first-person look mode.

### 15. Widescreen / Window Polish

Fullscreen (F11), arbitrary window resize with letterbox, internal
FBO scaling (`PORT_GL_SCALE`), linear-vs-nearest upscale, and the
16:9 Hor+ toggle are all wired through ImGui
(Renderer → Quality/Output). Remaining nice-to-haves:
- Auto-resize the SDL window when toggling widescreen so the user
  doesn't need to resize manually or fullscreen.
- Per-stage "skybox fills 16:9" — the sphere skybox currently stays
  pillar-boxed in 4:3 because it's a 2D OT prim rendered through the
  HUD-centred coordinate system.

---

## Done (was on previous TODO — keep for reference)

- [x] Camera follows Snake (fixed via GCL `intptr_t` + HZD bind
  persistent ptr table)
- [x] Hardware-accelerated rendering (OpenGL 3.3 backend behind
  `PORT_GL=1`; default on macOS + Linux)
- [x] 3D Gouraud lighting (NCS IR>>4 scaling, pipeline wiring,
  GBOUND test bypass — April 2026)
- [x] 16:9 Hor+ widescreen toggle
- [x] Codec task execution (was no-op before `mts_sta_tsk` fix)
