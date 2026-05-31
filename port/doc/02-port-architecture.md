# Port Architecture: PSX-to-macOS Mapping

## Design Philosophy

The port aims to be the simplest possible replacement for each PSX subsystem -- not a cycle-accurate emulator but a "good enough" reimplementation that lets the original game logic run. PSX hardware is replaced with software equivalents; the original C source files compile largely unmodified.

## SDL2 Integration

The port uses SDL2 for all platform interaction:

- **Window**: 320x224 logical resolution, 3x scale (960x672 actual), `SDL_WINDOW_ALLOW_HIGHDPI`
- **Renderer**: `SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC`, logical size set to 320x224
- **Audio**: SDL2 audio device with callback, 44100 Hz stereo 16-bit
- **Input**: `SDL_GetKeyboardState()` + `SDL_GameController` API
- **Events**: Polled each frame in `port_poll_events()`

### Boot Sequence

`main()` in `port/main.c` does **two** loops back-to-back: a pre-game
ImGui menu first, then the game loop. The split lets the user pick
resolution / GL backend / language / key bindings before any PSX engine
code runs (and recover from a saved config that would otherwise fail to
boot).

```
main()
  port_config_set_defaults()           // factory defaults
  port_config_load("port_config.ini")  // overlay saved settings
  SDL_Init + create window/renderer    // GL init has a safe-defaults
                                       //   fallback (see §Pre-Game Menu)
  imgui_init()

  /* ----- Pre-game menu loop (ImGui only) ----- */
  port_menu_init()
  while (state != GAME && state != QUIT) {
      port_poll_events()       // forwards events to port_menu_handle_event
      port_menu_frame()        // splash / main / options / controls page
      SDL_GL_SwapWindow or SDL_RenderPresent
  }
  if (state == QUIT) shutdown and return

  apply_language()             // OPTION_ENGLISH bit in GM_OptionFlag
  game_init()

  /* ----- Game loop (~60 fps) ----- */
  while (g_running) {
      port_poll_events()
      port_update_pad()        // keyboard + gamepad -> port_pad_buttons
      if (frame % 2 == 0) game_tick()  // 30 Hz game logic
      port_render()            // VRAM -> SDL texture, ImGui, present
  }
```

`PORT_SKIP_MENU=1` (and any of `MGS_AUTO_INPUT`, `MGS_INPUT_REPLAY`,
`PORT_AUTOLOAD_STAGE`) skips the menu loop entirely for CI / automation.

### game_tick() Sequence

`main_game.c` replicates the original PSX frame order:

```
1. ClearImage({0,0,320,224}, 0,0,32)   -- Clear framebuffer region in VRAM
2. DG_CurrentGroupID = 0xFFFFFFFF       -- Reset draw group
3. port_ot_next = 0                      -- Reset OT handle table
4. memset(port_scratchpad, 0, 1024)     -- Zero scratchpad
5. DG_UnDrawFrameCount = 0              -- Force rendering (bypass blockers)
6. DG_HikituriFlagOld = DG_HikituriFlag -- Frame-swap tracking
7. DG_SwapFrame()                        -- Swap draw/display environments
8. GV_UpdatePadSystem()                  -- Process pad input
9. DG_RenderFrame()                      -- Build OT, transform 3D objects
10. port_RenderObjects(GV_Clock)         -- Software rasterize 3D objects
11. GV_ExecActorSystem()                 -- Run all actors (game logic)
12. GV_Clock = 1 - GV_Clock             -- Flip double-buffer index
```

## Single-Threaded Model

PSX MGS uses MTS cooperative multitasking with 6 concurrent tasks. The port collapses everything to a single thread:

| PSX Task          | Port Replacement                              |
|-------------------|-----------------------------------------------|
| Game logic task   | `game_tick()` called directly from main loop  |
| Sound main task   | SPU emulator called from SDL audio callback   |
| Sound interrupt   | Not needed (no hardware IRQ)                  |
| CD BIOS task      | Synchronous `fread()` in `libfs.c`            |
| System/idle       | No-op                                         |

MTS API functions (`mts_slp_tsk`, `mts_wup_tsk`, `mts_send`, `mts_receive`, `mts_lock_sem`, etc.) are all stubbed to no-ops. `mts_wait_vbl()` increments a tick counter but does not actually sleep.

## Memory System

### PSX Memory Addresses -> malloc'd Pools

The original code uses hardcoded PSX RAM addresses:

```c
// Original (source/libgv/libgv.h):
#define GV_NORMAL_MEMORY_TOP    ((void *)0x80117000)
#define GV_NORMAL_MEMORY_SIZE   0x6b000  /* 428 KiB */
#define GV_PACKET_MEMORY0_TOP   ((void *)0x80182000)
#define GV_PACKET_MEMORY1_TOP   ((void *)0x801b1000)
#define GV_PACKET_MEMORY_SIZE   0x2f000  /* 188 KiB */
```

The port replaces these with a single `malloc`'d block (`port_memory.c`):

```c
// Port sizes (larger to accommodate 64-bit pointers):
#define GV_NORMAL_MEMORY_SIZE   0x200000  /* 2 MiB */
#define GV_PACKET_MEMORY_SIZE   0x80000   /* 512 KiB */

// Allocated as one contiguous block:
port_memory_block = malloc(2MB + 512KB + 512KB);  // 3 MiB total
port_normal_memory  = port_memory_block;
port_packet_memory0 = port_memory_block + 2MB;
port_packet_memory1 = port_memory_block + 2MB + 512KB;
port_gpu_base       = port_memory_block;  // OT handle base
```

All three pools reside in a single contiguous allocation so OT offset-based addressing works (all primitive pointers are within range of each other).

### port_overrides.h Type Fixes

Force-included before every source file (`-include psx/port_overrides.h`):

```c
// Fix type sizes: PSX u_long = 32-bit, macOS long = 64-bit
#define u_long  uint32_t
#define u_short uint16_t
#define u_char  uint8_t

// Disable static assertions (struct sizes differ on 64-bit)
#define STATIC_ASSERT(cond, msg)  /* disabled */

// Redirect PSX fprintf(stream_id, ...) to printf
#define fprintf(stream, ...) printf(__VA_ARGS__)

// Rename stdlib random() conflict
#define random sd_random
```

## VRAM Simulation

`port/libdg/vram.c` provides a software VRAM:

```c
uint16_t vram[512][1024];       // Full PSX VRAM (1 MB)
uint16_t port_zbuf[224][320];   // Z-buffer for framebuffer area
```

### GPU Function Mapping

| PSX Function       | Port Implementation          | Location              |
|--------------------|------------------------------|-----------------------|
| `ClearImage()`     | `port_ClearImage()` -- fills VRAM rect | `vram.c`     |
| `LoadImage()`      | `port_LoadImage()` -- copies data to VRAM | `vram.c`  |
| `StoreImage()`     | `port_StoreImage()` -- copies VRAM to buffer | `vram.c` |
| `MoveImage()`      | `port_MoveImage()` -- VRAM-to-VRAM copy | `vram.c`    |
| `DrawOTag()`       | `port_DrawOTag()` -- walks OT linked list | `vram.c`   |
| `DrawPrim()`       | `port_DrawPrim()` -- renders single primitive | `vram.c` |
| `SetDefDrawEnv()`  | `port_SetDefDrawEnv()` -- sets draw offset | `vram.c`  |
| `SetDefDispEnv()`  | `port_SetDefDispEnv()` -- sets display origin | `vram.c` |
| `DrawSync()`       | No-op (synchronous rendering)  | `gpu_stubs.c`       |
| `VSync()`          | No-op (SDL vsync handles this) | `gpu_stubs.c`       |

### Texture Sampling

The software renderer samples textures directly from VRAM:

```c
uint16_t sample_vram_texel(uint16_t tpage, uint16_t clut, int u, int v) {
    int tp   = (tpage >> 7) & 3;        // color mode
    int tpx  = (tpage & 0xF) * 64;      // base X
    int tpy  = ((tpage >> 4) & 1) * 256; // base Y

    if (tp == 0) {          // 4bpp
        int px = tpx + (u / 4);
        uint16_t texel = vram[tpy + v][px];
        int idx = (texel >> ((u % 4) * 4)) & 0xF;
        // Look up in CLUT
        int cx = (clut & 0x3F) * 16;
        int cy = (clut >> 6) & 0x1FF;
        return vram[cy][cx + idx];
    } else if (tp == 1) {   // 8bpp
        int px = tpx + (u / 2);
        uint16_t texel = vram[tpy + v][px];
        int idx = (texel >> ((u & 1) * 8)) & 0xFF;
        int cx = (clut & 0x3F) * 16;
        int cy = (clut >> 6) & 0x1FF;
        return vram[cy][cx + idx];
    } else {                 // 16bpp
        return vram[tpy + v][tpx + u];
    }
}
```

### Software 3D Renderer

`port_RenderObjects()` in `libdg_stub.c` iterates the DG object queue and for each object:

1. Sets up the GTE rotation/translation matrix from the object's world matrix
2. For each face (triangle/quad), calls GTE RTPT to project vertices
3. Calls NCLIP for backface culling
4. Calls AVSZ3 for Z-depth
5. Rasterizes via `draw_flat_tri()` with Z-buffer test

The rasterizer uses scanline conversion with edge walking. Textured triangles interpolate UV coordinates per-pixel and sample from VRAM.

### OT System (64-bit Safe)

The PSX OT stores 24-bit memory addresses in the low bits of each `u_long`. On 64-bit, addresses exceed 24 bits. The port uses a handle-table approach:

```c
void *port_ot_table[PORT_OT_TABLE_SIZE];  // 256K entries
int   port_ot_next;                        // next free slot

// Register a pointer, get a 24-bit handle:
u_long _ptr_to_handle(const void *p) {
    int idx = __sync_fetch_and_add(&port_ot_next, 1);
    port_ot_table[idx] = (void *)p;
    return (u_long)idx;
}

// Resolve handle back to pointer:
void *_handle_to_ptr(u_long handle) {
    return port_ot_table[handle];
}
```

The OT macros (`addPrim`, `catPrim`, `nextPrim`, `termPrim`, `isendprim`) are redefined in `port/psx/libgpu.h` to use these handle functions. The table is reset each frame (`port_ot_next = 0`).

## GTE Emulation

`port/psx/gte_math.c` implements all GTE operations in C. A global `GTE_State` struct holds all COP2 registers:

```c
typedef struct {
    SVECTOR V0, V1, V2;         // Input vectors
    CVECTOR RGBC;               // Input color
    long    OTZ;                // Average Z result
    long    IR0, IR1, IR2, IR3; // Intermediate results
    DVECTOR SXY0, SXY1, SXY2;  // Screen XY FIFO
    long    SZ0, SZ1, SZ2, SZ3;// Screen Z FIFO
    CVECTOR RGB0, RGB1, RGB2;   // Color FIFO
    long    MAC0, MAC1, MAC2, MAC3; // Accumulators
    // Control registers:
    MATRIX  R;                  // Rotation matrix
    long    TRX, TRY, TRZ;     // Translation
    MATRIX  L;                  // Light matrix
    long    RBK, GBK, BBK;     // Background color
    MATRIX  LR;                 // Light color matrix
    long    RFC, GFC, BFC;      // Far color
    long    OFX, OFY;           // Screen offset (16.16)
    long    H;                  // Projection distance
    long    DQA, DQB;           // Depth queuing
    long    ZSF3, ZSF4;        // Average Z scale
} GTE_State;

GTE_State gte_state;  // global instance
```

### inline_n.h Macro Expansion

PSX code uses inline assembly macros like `gte_ldv0()`, `gte_rtps()`, etc. The port's `inline_n.h` redefines all ~120 macros to manipulate `gte_state`:

```c
// Example: load vertex 0
#define gte_ldv0(r0)  do { \
    SVECTOR *_v = (SVECTOR *)(r0); \
    gte_state.V0 = *_v; \
} while(0)

// Example: rotate-translate-perspective single
#define gte_rtps()    gte_op_rtps()

// Example: store screen XY result
#define gte_stsxy(r0) do { \
    long *_p = (long *)(r0); \
    *_p = *(long *)&gte_state.SXY2; \
} while(0)

// Example: store OTZ
#define gte_stotz(r0) do { *(long *)(r0) = gte_state.OTZ; } while(0)
```

### Trig Tables

`InitGeom()` builds sine/cosine tables (4096 entries, 4.12 fixed-point):

```c
for (int i = 0; i < 4096; i++) {
    double angle = (double)i * 2.0 * PI / 4096.0;
    sin_table[i] = (int)(sin(angle) * 4096.0 + 0.5);
    cos_table[i] = (int)(cos(angle) * 4096.0 + 0.5);
}
```

## Scratchpad Replacement

```c
char port_scratchpad[1024];  // BSS array in gte_math.c

// port_overrides.h makes this available globally
extern char port_scratchpad[1024];

// getScratchAddr2(n) expands to:
#define getScratchAddr2(n)  ((void *)((unsigned long long)(port_scratchpad) + (n) * 4))
```

Additionally, `port_memory.c` attempts to `mmap` a page at `0x1F800000` for code that uses literal scratchpad addresses (e.g., `*(int *)0x1F800038`). This works on macOS but is not guaranteed.

## Filesystem Replacement

`port/libfs/libfs.c` replaces CD-ROM access with stdio file I/O:

### File Access

```c
#define PORT_DATA_PATH "data/disc1/MGS/"

// 7 data files opened at startup:
static FILE *dat_files[7];  // STAGE.DIR, RADIO.DAT, FACE.DAT, ...

// STAGE.DIR directory table parsed from first sector:
typedef struct {
    char name[8];   // stage name (null-padded)
    int  offset;    // sector offset within STAGE.DIR
} DirEntry;
```

### Stage Loading Pipeline

```
FS_LoadStageRequest("s00a")
  -> FS_CdGetStageFileTop("s00a")  -- look up sector offset in directory table
  -> fseek(stage_dir_file, sector * 2048)
  -> fread() the DATACNF header
  -> GV_AllocMemory() for the full stage data
  -> fread() all sectors
  -> Parse DATACNF tags:
       'r' tags -> DAR archive -> GV_LoadInit(data, id, REGION_RESIDENT)
       'c' tags -> offset-based cache entries -> GV_LoadInit(data, id, REGION_CACHE)
       'n' tags -> nocache DAR -> GV_LoadInit(data, id, REGION_NOCACHE)
       's' tags -> sound/overlay binary
  -> Each GV_LoadInit calls the registered loader by extension:
       'p' -> DG_LoadInitPcx (upload texture to VRAM)
       'k' -> DG_LoadInitKmd (parse 3D model)
       'h' -> HZD loader (collision mesh)
       'e' -> GCL script (stored in cache for interpreter)
       'o' -> DG_LoadInitOar (animation data)
       'n' -> DG_LoadInitNar (animation data)
```

## GCL 64-bit Fixes

The GCL interpreter (`source/libgcl/`) stores pointers in `int` variables, which truncates on 64-bit. The port provides patched versions in `port/libgcl_fix/`:

- `parse.c`: Pointer arithmetic fixed for 64-bit
- `expr.c`: Expression evaluator pointer handling
- `variable.c`: Variable table using proper pointer types
- `basic.c`, `command.c`, `gcl_init.c`: Related fixes

These files replace the originals via Makefile rules (libgcl is compiled from `libgcl_fix/` instead of `source/libgcl/`).

## KMD Model Loader (64-bit Safe)

KMD binary format uses 32-bit offsets in pointer fields. On PSX, the game cast the raw buffer directly as `DG_DEF`/`DG_MDL` structs. On 64-bit, pointers are 8 bytes, so the port parses the raw binary and rebuilds native structs:

```c
// Raw format on disc (32-bit, 76 bytes per model):
typedef struct {
    int32_t  flags, n_faces;
    DG_VECTOR min, max, pos;
    int32_t  parent, extend, n_verts;
    uint32_t vertices_off;     // 32-bit offset, not pointer
    uint32_t vindices_off;
    int32_t  n_normals;
    uint32_t normals_off, nindices_off;
    uint32_t texcoords_off, materials_off;
    int32_t  padding;
} KMD_MDL_RAW;

// Port converts to native DG_MDL with real 64-bit pointers
```

## HZD Collision Loader

Similar to KMD -- the HZD binary format uses 32-bit pointer fields that are reinterpreted as offsets and converted to 64-bit pointers by `port/libhzd/hzd_loader.c`.

## Sound System

`port/sound/spu_emu.c` provides a software SPU:

- **512 KB sound RAM** (BSS array)
- **24 voice channels** with ADPCM decode, pitch, volume, ADSR envelopes
- **SDL2 audio callback** mixes all active voices to stereo PCM
- **SpuSetVoiceAttr / SpuSetKey** implemented
- Most `sd_*` sound driver functions in `port/sound/sd_stubs.c` are stubbed

## Pre-Game Menu (port_menu.cpp / port_config.{c,h})

Before any PSX engine code runs, `main.c` enters a pre-game ImGui loop
that handles startup configuration. It owns its own ImGui frame
(`ImGui_ImplOpenGL3_NewFrame` or `ImGui_ImplSDLRenderer2_NewFrame`
depending on backend) and exits when the user clicks **Start Game**,
**Exit**, or one of the menu-skip env vars is set.

| Page      | What it does |
|-----------|--------------|
| Splash    | Logo + version, auto-advances after ~1.5 s or on any key |
| Main      | Start / Options / Controls / Exit |
| Options   | Video (resolution preset, fullscreen, vsync, GL backend, FBO scale, widescreen), Audio (master volume), Game (language) |
| Controls  | Press-key-to-bind table for all 14 PSX buttons, separate keyboard + gamepad columns |

`PortConfig` (in `port_config.h`) is a plain struct of ints. It's
populated from compile-time defaults at startup, then optionally
overwritten by parsing `./port_config.ini`. The Controls page edits a
working copy; **Save & Apply** commits to `g_port_config` and rewrites
the INI. Live-tunable settings (widescreen, volume) apply immediately;
window-state changes (resolution, fullscreen, GL backend) take effect
on next launch.

INI sections: `[video]`, `[audio]`, `[game]`, `[keyboard]`, `[gamepad]`.
Key/button bindings store raw SDL enum integers so the file is portable
across SDL versions; human-readable names are decoded only for display
via `SDL_GetScancodeName` / `SDL_GameControllerGetStringForButton`.

### GL safe-defaults fallback

If the saved video config produces a broken GL window/context (user
picked 4K fullscreen but the driver refuses, GL is unavailable on the
host, etc.), `main.c` catches the init failure, resets the config to
`1280x896 / windowed / GL scale 4 / no widescreen`, rewrites the INI,
and retries. This stops a bad save from permanently locking the user
out of the menu.

### Caveat: `port_overrides.h` clobbers `fprintf`

The force-included `port_overrides.h` has a
`#define fprintf(stream, ...) printf(...)` macro that papers over PSX
code passing an `int` stream ID. Port-native files that need to write
to a real `FILE*` (config save, screenshot, etc.) must `#undef fprintf`
at the top.

## ImGui Debug Overlay

`port/imgui_debug.cpp` renders a Dear ImGui debug window with tabs for:

- Renderer (FBO scale, blit filter, widescreen, blur strength, …)
- Camera (free-fly override, eye_inv dump, near/far)
- Actors (all 9 priority levels, per-actor act/die fn ptrs, tick counts)
- Stage (current stage code, load state, area history)
- Game (`GV_Clock`, `GV_Time`, `GM_GameStatus`, `GM_AlertMode`, …)
- Demo (per-frame scrubber, dump-snake-render-state, direct-from-disk
  feeder for cutscene debugging)
- Lighting (DG_Ambient, DG_LightMatrix / DG_ColorMatrix, fixed-light
  list, dynamic-light slots)
- Sound (SPU voice state, master volume, voice mute)

Toggle with **F1**. ImGui events are processed in the SDL event loop;
rendering happens after the game's VRAM blit but before
`SDL_GL_SwapWindow` / `SDL_RenderPresent`. **F5** in the Renderer tab
hot-reloads the GLSL shaders from `port/libdg/shaders/`.

## Build System

The Makefile compiles three categories of code:

1. **Port sources** (`port/`): Compiled with `-include psx/port_overrides.h`, SDL2 flags
2. **Original game sources** (`source/`): Compiled with the same override header, `-Wno-everything`
3. **ImGui** (C++): Compiled separately with `-w -O1`

### Compiler Flags

```makefile
CC = clang
CFLAGS = -Wno-everything -O0 -g -DINTEGRAL -include psx/port_overrides.h \
         -Wno-int-conversion -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
         -Wno-incompatible-pointer-types
LDFLAGS = $(shell sdl2-config --libs) -lm -lc++
```

Key defines:
- `INTEGRAL`: Always set (matches PSX build)
- `PORT_BUILD`: Set by port_overrides.h, used for port-specific `#ifdef` branches
- `DEV_EXE`: Set by port_overrides.h, enables assertions and debug output

### Stage Symbol Deconfliction

Each stage file defines `_StageCharacterEntries`. Since all stages are compiled into one binary, the Makefile renames each:

```makefile
$(OBJDIR)/stage_$(1).o: $(SRCDIR)/stage/$(1).c
    $(CC) $(CFLAGS) -D_StageCharacterEntries=_StageCharacterEntries_$(1) -c -o $$@ $$<
```

At runtime, `mts_get_bss_tail()` returns the default stage's table (`_StageCharacterEntries_select`), and the stage loader resolves the correct table by stage name.

### Code Signing

The binary is ad-hoc signed with entitlements for the `mmap` at `0x1F800000`:

```makefile
codesign -s - --entitlements entitlements.plist -f $@ 2>/dev/null || true
```

## File Map

| Port File                  | Replaces                        | Purpose                          |
|----------------------------|---------------------------------|----------------------------------|
| `main.c`                  | PSX boot sequence               | SDL init, event loop, render     |
| `main_game.c`             | `source/main/main.c`           | game_init, game_tick, camera     |
| `port_memory.c`           | Hardcoded PSX RAM               | malloc'd memory pools            |
| `psx/port_overrides.h`    | PSX SDK headers                 | Type fixes, macro redirects      |
| `psx/gte_math.c`          | COP2 hardware                   | Software GTE implementation      |
| `psx/inline_n.h`          | MIPS COP2 inline asm            | C macro expansions               |
| `psx/gpu_stubs.c`         | GPU hardware registers          | DrawSync/VSync/PutDrawEnv stubs  |
| `psx/libgte.h`            | `<libgte.h>` PSX SDK            | SVECTOR/MATRIX/RECT types        |
| `psx/libgpu.h`            | `<libgpu.h>` PSX SDK            | Primitive structs, OT macros     |
| `libdg/vram.c`            | GPU VRAM hardware               | Software VRAM + rasterizer       |
| `libdg/kmd_loader.c`      | `source/libdg/loader.c` (cast)  | 64-bit safe KMD parser           |
| `libdg/pcx_loader.c`      | `source/libdg/loader.c` (cast)  | PCX -> VRAM texture upload       |
| `libdg/libdg_stub.c`      | `source/libdg/dgd.c`           | DG_StartDaemon, port_RenderObjects |
| `libfs/libfs.c`           | `source/libfs/` (CD-ROM)        | stdio file I/O, DATACNF parser   |
| `mts/mts.c`               | `source/mts/` (kernel)          | Single-threaded stubs + pad      |
| `sound/spu_emu.c`         | SPU hardware                    | ADPCM decoder, voice mixer       |
| `sound/sd_stubs.c`        | `source/sound/` (driver)        | Sound driver function stubs      |
| `memcard/memcard_stub.c`  | `source/memcard/`               | Memory card no-ops               |
| `libgcl_fix/*.c`          | `source/libgcl/`               | 64-bit safe GCL interpreter      |
| `libhzd/hzd_loader.c`     | HZD binary cast                 | 64-bit safe collision loader     |
| `imgui_debug.cpp`         | (new)                           | Actor inspector overlay          |
| `game_stubs.c`            | Various game functions           | Stub unimplemented game funcs    |
| `asm_stubs.c`             | `asm/*.s` (MIPS assembly)       | Stub assembly-only functions     |
| `extern_stubs.c`          | External/library functions       | Stub PSX library calls           |
| `link_stubs.c`            | Linker-required symbols          | Stub unresolved symbols          |
