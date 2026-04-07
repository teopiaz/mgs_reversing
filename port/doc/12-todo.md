# TODO

Prioritized task list for the macOS port.

---

## Critical (Blocks Gameplay)

### 1. Fix HZD_GetAddress Zone Lookup

`HZD_GetAddress` returns 0 for Snake's position, which breaks movement, enemy AI, and trigger zones. Investigate:

- Compare Snake's world position with zone bounding boxes (print both to console)
- Check if zone x/z/w/h values are in the expected coordinate space
- Verify the zones pointer in HZD_MAP points to valid data (print first few zone entries)
- Test with a known stage (s00a) where Snake's spawn position and zone layout are understood

Once `HZD_GetAddress` returns a valid zone, `GM_PlayerAddress` will be non-zero, enemy AI will pathfind correctly, and `HZD_StepCheck` should allow Snake to move.

**Files**: `source/libhzd/zone.c`, `port/libhzd/hzd_loader.c`, `source/libhzd/hzdd.c`

### 2. Fix Camera to Follow Snake

Verify that the camera actor (`NewCameraSystem` in `source/game/camera.c`) is spawned and its act function receives valid Snake position data. The camera may be pointing at (0,0,0) if `GM_PlayerBody` or the player position vector is not being updated. Check that `GM_PlayerPosition` is set by the Snake actor during initialization.

**Files**: `source/game/camera.c`, `source/chara/snake/sna_init.c`

---

## High (Major Features)

### 3. Fix BGM Audio Quality

BGM sequencer is connected and plays (notes trigger, SPU produces audio), but the
output sounds bad. Investigate:

- Verify ADPCM decoding produces correct waveforms (compare with known-good decoder)
- Check voice_tbl WAVE_W parameters (sample_note, sample_tune) are parsed correctly
  from the .wvx header — the stage loader converts from PSX 16-byte format to port struct
- Verify pitch calculation in sd_ioset.c freq_set/freq_tbl is correct on 64-bit
- Check ADSR envelope timing — env_tick runs at 44100Hz per sample, may need tuning
- The IntSdMain tick rate (3x per 30fps frame ≈ 90Hz) may need adjustment to match
  the actual PSX SPU IRQ rate for correct tempo

**Files**: `port/sound/spu_emu.c`, `source/sound/sd_ioset.c`, `port/libfs/libfs.c` (wave header parsing)

### 4. Fix GCL Bind Proc ID Corruption

The `bind` command stores `0x7F0000xx` pointer table indices as proc IDs. Fix by resolving the pointer at bind registration time:

- In the bind handler, detect `0x7F000000` marker values
- Call `gcl_resolve_ptr()` to recover the actual pointer
- Extract the proc ID from the resolved GCL data

This will re-enable all stage trigger callbacks (door triggers, alert zones, cutscene triggers).

**Files**: `port/libgcl_fix/basic.c`, `port/game_script_fix.c`, `source/libhzd/event.c`

### 5. Convert 31 Stage Character Entries from PSX Addresses to Function Pointers

For each of the 31 stages that use raw PSX addresses in `_StageCharacterEntries[]`:

- Identify which actor constructor each address corresponds to (cross-reference with `build/functions.txt`)
- If the function has been decompiled, replace the address with the function name
- If not decompiled, stub the entry with a no-op constructor that logs the missing actor

**Files**: `source/stage/*.c`, `build/functions.txt`

---

## Medium (Quality and Completeness)

### 6. Port Remaining 5 Excluded Files

Resolve compilation errors in the excluded source files:

- For R-variant overlays (d18ar, s08br, s19br), use weak symbols or rename conflicting functions with a stage prefix
- For other excluded files, fix remaining PSX-specific constructs

### 7. Implement FMV Playback (or Placeholder)

Options in order of complexity:

- **Placeholder**: Display a "MOVIE SKIPPED" overlay for 2 seconds, then continue (trivial)
- **Static frame**: Decode the first frame of each STR file and display it as a still image
- **Full playback**: Implement MDEC decoding (Huffman + IDCT), STR demuxing, audio sync

The STR format is well-documented. Libraries like jPSXdec have open-source MDEC decoders that could be adapted.

### 8. Memory Card Save/Load (File-Based)

Replace PSX memory card API calls with standard file I/O:

- `McOpen` / `McClose` -> `fopen` / `fclose`
- `McRead` / `McWrite` -> `fread` / `fwrite`
- Save files stored in `~/.mgs_saves/` or `port/saves/`
- Memory card block structure (8KB blocks, 15 blocks per card) can be simplified to a single flat file per save slot

**Files**: `source/memcard/`, `port/memcard/`

### 9. Fix shakemdl Model Hash

Debug the GCL parameter parsing for shakemdl's model parameter. The 'm' parameter (`0x6D`) may be going through the pointer table when it should be a direct integer value. Add logging in the shakemdl actor to print the raw and resolved parameter values.

**Files**: `source/takabe/shakemdl.c`

### 10. Find Root Cause of Dangling Render Queue Entries

The freed-memory detection in `DG_BoundObjs` is a workaround. The real fix is to ensure `DG_OBJS` are dequeued from the ordering table before the actor's memory is freed:

- Audit `DG_DestroyObjs` and verify it removes the object from all OT entries
- Check if double-buffered OT (port uses single buffer) causes stale entries
- Add a debug flag to `DG_OBJS` that marks them as "pending destroy" and asserts if they appear in the OT walker

**Files**: `port/libdg/vram.c`, `port/libdg/libdg_stub.c`, `source/libdg/dgd.c`

---

## Low (Polish)

### 11. Hardware-Accelerated Rendering

Replace the software rasterizer with a GPU-accelerated backend:

- **Metal** (macOS-native): Submit textured triangles via MTLRenderCommandEncoder. Map PSX CLUT/tpage to Metal textures.
- **OpenGL 3.3**: Cross-platform option. Upload VRAM as a texture, render PSX primitives as GL triangles with a fragment shader that samples VRAM.
- **SDL_GPU**: SDL3's GPU abstraction (auto-selects Metal/Vulkan/D3D12)

The software rasterizer is adequate for development but runs at roughly 30 FPS for complex scenes. GPU rendering would trivially hit 60+ FPS.

### 12. VOX.DAT / DEMO.DAT Stream Audio (Codec Voice, Cutscene Dialog)

Codec voice and cutscene dialog use the stream system (FS_StreamGetData). The port
loads raw data from VOX.DAT/DEMO.DAT but FS_StreamGetData fails to parse it because:

- On PSX, the CD driver reads sectors one at a time and wraps each 2048-byte payload
  with a `{size:24, type:8}` header before placing it in the stream circular buffer
- The port loads contiguous file data without this sector-to-entry conversion
- FS_StreamGetData scans for `{size:24, type:8}` entry headers but finds raw sector data

Fix: add a sector demuxer in FS_StreamTaskStart that processes the loaded data:
- Each 2048-byte sector has a 4-byte tag at offset 0: `{size:24, type:8}`
- Walk sector-by-sector, extract entries, place in stream buffer
- Types: 0x01=audio ADPCM, 0x02=stream header, 0x05=subtitle/timing, 0x10=control

The stream audio also needs SPU voice setup for voices 21-22 (left/right channels)
via StrSpuTrans in sd_str.c, which transfers ADPCM blocks to SPU RAM ping-pong buffers.

**Files**: `port/libfs/libfs.c` (FS_StreamTaskStart, FS_StreamGetData),
`source/sound/sd_str.c` (StartStream, StrSpuTrans), `source/libfs/stream.c` (reference)

### 13. Proper Analog Stick Support

Current analog input maps left stick to D-pad directions with a dead zone. Full analog support requires:

- Passing raw analog values (0-255) through to `GV_PAD.left_dx/dy`
- Ensuring Snake's movement code reads analog values (it does on PSX)
- Calibrating dead zone and sensitivity to match PSX feel
- Right stick support (first-person look mode)

### 14. Fullscreen and Resolution Options

- Add fullscreen toggle (SDL_SetWindowFullscreen)
- Support arbitrary window resizing with correct aspect ratio (4:3, 16:9 with pillarbox)
- Render at higher internal resolution (2x, 4x PSX resolution) for sharper visuals
- Optional bilinear filtering on upscale
