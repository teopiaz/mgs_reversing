# PSX Hardware Architecture (as Relevant to MGS)

## CPU: R3000A (MIPS I)

- **Clock**: 33.8688 MHz
- **Architecture**: 32-bit MIPS I, little-endian
- **Registers**: 32 general-purpose (r0-r31), HI/LO for multiply/divide
- **Main RAM**: 2 MB at `0x80000000`-`0x801FFFFF` (KSEG0, cached)
  - Same physical memory mirrored at `0x00000000` (KUSEG) and `0xA0000000` (KSEG1, uncached)
- **Scratchpad**: 1 KB fast SRAM at `0x1F800000`-`0x1F8003FF`
  - Used by MGS for temporary collision/rendering scratch data
  - Accessed via `getScratchAddr2(n)` which returns `0x1F800000 + n*4`
- **Instruction cache**: 4 KB
- **Data cache**: 1 KB (the scratchpad IS the data cache, repurposed)

### Memory Map (MGS Layout)

```
0x80000000  +-----------------------+
            | Kernel / MTS          |
0x80010000  +-----------------------+
            | Main executable code  |
            | (.text section)       |
0x80090000  +-----------------------+  (approx)
            | .data / .rdata        |
0x800A0000  +-----------------------+  (approx)
            | .sbss / .bss          |
            | (actor lists,         |
            |  global state)        |
0x800B0000  +-----------------------+
            | GV normal memory pool |  428 KB (GV_NORMAL_MEMORY)
            | (dynamic allocations, |
            |  stage data, actors)  |
0x80117000  +-----------------------+  GV_NORMAL_MEMORY_TOP
            | ...                   |
0x80182000  +-----------------------+  GV_PACKET_MEMORY0_TOP
            | Packet memory 0       |  188 KB (OT + primitives, even frames)
0x801B1000  +-----------------------+  GV_PACKET_MEMORY1_TOP
            | Packet memory 1       |  188 KB (OT + primitives, odd frames)
0x801E0000  +-----------------------+  (approx)
            | Stack / overlay area  |
0x801FFFFF  +-----------------------+  End of RAM

0x1F800000  +-----------------------+
            | Scratchpad (1 KB)     |  Collision scratch, temp vertex buffers
0x1F8003FF  +-----------------------+
```

## GPU

- **VRAM**: 1 MB organized as a 1024x512 framebuffer of 16-bit pixels
- **Pixel format**: 1-bit semi-transparency + 5-bit blue + 5-bit green + 5-bit red (MSB to LSB)

### VRAM Layout (MGS)

```
       0       320     640          1024
  0    +-------+-------+------------+
       | FB 0  | FB 1  | Texture    |
       | 320x  | 320x  | pages      |
  224  | 224   | 224   |            |
       +-------+-------+            |
       | CLUT area     |            |
  256  | (palettes)    |            |
       +-------+-------+            |
       | Texture pages (continued)  |
       |                            |
  512  +----------------------------+
```

- **Double-buffered framebuffer**: Two 320x224 regions at (0,0) and (320,0)
  - `DRAWENV` sets the drawing area, `DISPENV` sets the display area
  - Each frame: draw to one buffer, display the other, then swap
- **Texture pages**: 64 pages total across VRAM, MGS uses pages in the right half
  - 4bpp mode: 64x256 pixels per tpage (each pixel is 4-bit index into CLUT)
  - 8bpp mode: 128x256 pixels per tpage (each pixel is 8-bit index into CLUT)
  - 16bpp mode: 256x256 pixels per tpage (direct color, no CLUT)
- **CLUT (Color Look-Up Table)**: Palettes stored in VRAM, typically in the 224-256 row gap
  - 4bpp CLUT: 16 entries x 16-bit = 32 bytes
  - 8bpp CLUT: 256 entries x 16-bit = 512 bytes

### Texture Page Encoding

The `tpage` value encodes format and position:

```
Bits 0-3:   X base (in 64-pixel units)  -> tpage_x = (tpage & 0xF) * 64
Bits 4:     Y base (0=top half, 1=bottom half) -> tpage_y = ((tpage >> 4) & 1) * 256
Bits 5-6:   Semi-transparency mode (ABR: 0=50%blend, 1=add, 2=sub, 3=add25%)
Bits 7-8:   Color mode (0=4bpp, 1=8bpp, 2=16bpp)
```

### CLUT Encoding

```
Bits 0-5:   X position (in 16-pixel units)  -> clut_x = (clut & 0x3F) * 16
Bits 6-14:  Y position                       -> clut_y = (clut >> 6) & 0x1FF
```

### Ordering Table (OT)

The PSX GPU processes primitives via an ordering table -- a singly-linked list used for Z-sorting:

```c
// OT is an array of u_long, one entry per Z-depth level (typically 1024 or 4096 entries)
u_long ot[OT_LENGTH];

// Each entry's low 24 bits point to the next node (or 0xFFFFFF = end)
// High 8 bits = packet length in 32-bit words

// Primitives are added to the OT at their Z depth:
addPrim(&ot[z], &poly);  // inserts poly at depth z

// GPU processes the list via DrawOTag:
DrawOTag(&ot[OT_LENGTH - 1]);  // start from farthest Z
```

OT entries chain together via 24-bit pointers. On PSX (32-bit), these are real memory addresses with the high byte used for packet length. The port uses a handle table (`port_ot_table[]`) to map 24-bit indices to 64-bit pointers.

### GPU Primitive Types (Used by MGS)

| Code  | Type      | Description                    | Words |
|-------|-----------|--------------------------------|-------|
| 0x20  | POLY_F3   | Flat-shaded triangle           | 4     |
| 0x24  | POLY_FT3  | Flat-textured triangle         | 7     |
| 0x28  | POLY_F4   | Flat-shaded quad               | 5     |
| 0x2C  | POLY_FT4  | Flat-textured quad             | 9     |
| 0x30  | POLY_G3   | Gouraud-shaded triangle        | 6     |
| 0x34  | POLY_GT3  | Gouraud-textured triangle      | 9     |
| 0x38  | POLY_G4   | Gouraud-shaded quad            | 8     |
| 0x3C  | POLY_GT4  | Gouraud-textured quad          | 12    |
| 0x60  | TILE      | Variable-size rectangle        | 3     |
| 0x64  | SPRT      | Variable-size textured sprite  | 4     |
| 0x68  | TILE_1    | 1x1 rectangle                  | 2     |
| 0x70  | TILE_8    | 8x8 rectangle                  | 2     |
| 0x74  | SPRT_8    | 8x8 textured sprite            | 3     |
| 0x78  | TILE_16   | 16x16 rectangle                | 2     |
| 0x7C  | SPRT_16   | 16x16 textured sprite          | 3     |
| 0xE0  | DR_TPAGE  | Set texture page               | 1     |
| 0xE4  | DR_AREA   | Set drawing area               | 2     |
| 0xE8  | DR_OFFSET | Set drawing offset             | 1     |

### Primitive Memory Layout (POLY_GT4 example)

```c
typedef struct {
    u_long tag;             // [len:8][next_ptr:24]
    u_char r0, g0, b0, code; // code = 0x3C
    short  x0, y0;
    u_char u0, v0; u_short clut;
    u_char r1, g1, b1, pad1;
    short  x1, y1;
    u_char u1, v1; u_short tpage;
    u_char r2, g2, b2, pad2;
    short  x2, y2;
    u_char u2, v2; u_short pad3;
    u_char r3, g3, b3, pad4;
    short  x3, y3;
    u_char u3, v3; u_short pad5;
} POLY_GT4;  // 48 bytes (12 words), len=12
```

### Transparency

- **STP bit** (bit 15 of a 16-bit pixel): Semi-Transparency Processing flag
- Texel value `0x0000` = fully transparent (never drawn)
- If a primitive has semi-transparency enabled AND the texel has bit 15 set, alpha blending is performed using one of four ABR modes:
  - ABR 0: `(B + F) / 2` (50% blend)
  - ABR 1: `B + F` (additive)
  - ABR 2: `B - F` (subtractive)
  - ABR 3: `B + F/4` (25% additive)

## GTE (Geometry Transformation Engine)

The GTE is a coprocessor (COP2) that performs hardware-accelerated vector/matrix math. It is critical to MGS's 3D rendering pipeline.

### Registers

**Data Registers (COP2 r0-r31):**

| Reg    | Name     | Description                          |
|--------|----------|--------------------------------------|
| r0-r1  | VXY0/VZ0 | Input vector 0 (SVECTOR)            |
| r2-r3  | VXY1/VZ1 | Input vector 1                      |
| r4-r5  | VXY2/VZ2 | Input vector 2                      |
| r6     | RGBC     | Input color (CVECTOR: r,g,b,code)   |
| r7     | OTZ      | Average Z result (for OT insertion) |
| r8     | IR0      | Intermediate result 0 (16-bit)      |
| r9-r11 | IR1-IR3  | Intermediate results 1-3            |
| r12-r14| SXY0-SXY2| Screen XY FIFO (projected coords)  |
| r16-r19| SZ0-SZ3  | Screen Z FIFO                       |
| r20-r22| RGB0-RGB2| Color FIFO (lighting results)       |
| r24    | MAC0     | Sum of products (32-bit)            |
| r25-r27| MAC1-MAC3| Sum of products (44-bit accum)      |
| r30-r31| LZCS/LZCR| Leading zero count                  |

**Control Registers (COP2 c0-c31):**

| Reg    | Name     | Description                          |
|--------|----------|--------------------------------------|
| c0-c4  | R11-R33  | Rotation matrix (3x3, 1.3.12 fixed) |
| c5-c7  | TRX/TRY/TRZ | Translation vector (32-bit)      |
| c8-c12 | L11-L33  | Light source matrix                  |
| c13-c15| RBK/GBK/BBK | Background color                 |
| c16-c20| LR11-LR33| Light color matrix                  |
| c21-c23| RFC/GFC/BFC | Far color                         |
| c24-c25| OFX/OFY  | Screen offset (16.16 fixed-point)   |
| c26    | H        | Projection plane distance            |
| c27    | DQA      | Depth queuing parameter A            |
| c28    | DQB      | Depth queuing parameter B            |
| c29-c30| ZSF3/ZSF4| Average Z scale factors             |

### Key Operations

- **RTPS** (Rotate-Translate-Perspective Single): Transform one vertex through the rotation matrix + translation, then apply perspective projection. Result goes to SXY2 (screen coords) and SZ3 (depth).
- **RTPT** (Rotate-Translate-Perspective Triple): Same as RTPS but for three vertices at once (V0, V1, V2 -> SXY0, SXY1, SXY2).
- **NCLIP** (Normal Clip): Compute cross product of screen triangle (SXY0,SXY1,SXY2). Positive = front-facing, negative = back-facing.
- **AVSZ3/AVSZ4**: Average Z of 3 or 4 values from SZ FIFO, scaled by ZSF3/ZSF4. Used to compute OT insertion depth.
- **NCS/NCT**: Normal Color Single/Triple. Compute lighting: light_matrix * normal -> light_color_matrix * result + background -> color FIFO.
- **NCDS/NCDT**: Normal Color Depth Single/Triple. Like NCS but with depth cueing (fog).

### Fixed-Point Convention

All GTE math uses **4.12 fixed-point**: `ONE = 4096 = 1.0`. Matrix entries are `short` (1.3.12), translation is `int` (19.12).

```c
// Example: setting up rotation matrix
MATRIX m;
m.m[0][0] = 4096;  // 1.0
m.m[1][1] = 4096;  // 1.0
m.m[2][2] = 4096;  // 1.0
// m.t[0], m.t[1], m.t[2] = translation (also 4.12 but stored as int)
```

### Trig Tables

PSX GTE angles are measured in 4096 units per full revolution (not degrees or radians):

```
0    = 0 degrees
1024 = 90 degrees
2048 = 180 degrees
3072 = 270 degrees
```

`rcos(angle)` and `rsin(angle)` return 4.12 fixed-point values.

## SPU (Sound Processing Unit)

- **Voices**: 24 independent channels
- **Sound RAM**: 512 KB, separate from main RAM
  - Address `0x0000`-`0x07FFFF`
  - First `0x1000` bytes reserved (capture buffers, CD audio)
- **Sample format**: ADPCM, 4-bit compressed (28 samples per 16-byte block)
  - Each 16-byte block: 1 byte header (shift + filter), 1 byte flags (loop/end), 14 bytes data
  - Filter coefficients: `{0,0}, {60,0}, {115,-52}, {98,-55}, {122,-60}`
  - Flags: bit 0 = loop end, bit 1 = loop (jump to loop addr), bit 2 = loop start
- **Pitch**: 0x1000 = 44100 Hz playback. Pitch register is 16.16 fixed-point relative to 44100 Hz.
- **ADSR envelopes**: Attack, Decay, Sustain level, Sustain rate, Release (hardware per-voice)
- **Hardware reverb**: Configurable reverb buffer in sound RAM
- **Key On/Key Off**: 24-bit registers, one bit per voice, trigger attack/release

### MGS Sound Architecture

MGS uses a custom sound driver (`source/sound/`) that runs in an MTS task:
- `sd_main`: Main sound task, processes commands from the game
- `sd_int`: Interrupt handler task, feeds SPU data
- Sound effects are loaded as VH+VB pairs (header + body) into SPU RAM
- BGM is streamed from CD as XA audio

## CD-ROM

- **Speed**: 2x (300 KB/s), sector size = 2048 bytes
- **MGS data layout**: All game data packed into 7 large files (STAGE.DIR, RADIO.DAT, FACE.DAT, etc.)
- **STAGE.DIR**: Master archive containing all stage data
  - Directory table at sector 0: array of {8-byte name, 4-byte sector offset}
  - Each stage entry is a DATACNF (data configuration) followed by DAR archives
- **Asynchronous reads**: PSX CD reads are async with callbacks; the port replaces these with synchronous `fread()`

### DATACNF Format

Each stage in STAGE.DIR starts with a DATACNF header:

```c
typedef struct {
    int version;
    int size;      // total size in sectors
    DATACNF_TAG tags[];  // variable-length tag array
} DATACNF;

typedef struct {
    short id;      // resource ID (hashed name)
    char  mode;    // 'r' = resident, 'c' = cache, 'n' = nocache, 's' = sound/binary
    char  ext;     // file type: 'k' = KMD, 'p' = PCX, 'h' = HZD, 'e' = GCL, etc.
    int   size;    // size in bytes (or offset for 'c' mode)
} DATACNF_TAG;
```

### DAR Archive Format

```c
typedef struct {
    short id;      // resource ID
    short ext;     // file extension code
    int   size;    // data size in bytes
    // followed by `size` bytes of file data
} DARFILE_TAG;
```

## Scratchpad

The 1 KB scratchpad at `0x1F800000` is used by MGS as fast temporary storage:
- Collision system stores intermediate results
- Rendering pipeline uses it for temporary vertex buffers
- Accessed via `getScratchAddr2(n)` = `(void*)(0x1F800000 + n * 4)`

The port provides a 1024-byte BSS array (`port_scratchpad`) and attempts to `mmap` it at `0x1F800000`. The `SCRPAD_ADDR` macro redirects to the array's address.

## MTS (Multi-Tasking System)

MTS is a custom cooperative multitasking kernel built on top of the PSX BIOS thread API. It provides:

- **6 tasks** (configured in MGS):
  - Task 0: System/idle
  - Task 1: Game logic (main game loop)
  - Task 2: Sound main (`sd_main`)
  - Task 3: Sound interrupt (`sd_int`)
  - Task 4: CD BIOS (`CDBIOS_TaskStart`)
  - Task 5-11: Available for additional tasks

- **Cooperative scheduling**: Tasks yield via `mts_slp_tsk()` / `mts_wait_vbl()`
- **Message passing**: `mts_send()` / `mts_receive()` for inter-task communication
- **Semaphores**: `mts_lock_sem()` / `mts_unlock_sem()` for mutual exclusion
- **V-sync synchronization**: `mts_wait_vbl(count)` sleeps until N vblanks pass
- **Controller polling**: `mts_PadRead()` / `mts_get_pad()` read hardware controller state

The port replaces MTS with a single-threaded implementation where all task functions are called directly.

## MGS Engine Architecture

### Library Stack

```
+------------------------------------------+
| Game Logic (source/game/, source/chara/, |
| source/enemy/, source/weapon/, etc.)     |
+------------------------------------------+
| libgcl - GCL Scripting Interpreter       |
+------------------------------------------+
| libdg - 3D Rendering Pipeline            |
| libhzd - Collision Detection             |
| libfs - Filesystem / CD-ROM Access       |
| libsio - Serial I/O (debug)             |
+------------------------------------------+
| libgv - Core Engine Services             |
| (memory, actors, cache, messages, math)  |
+------------------------------------------+
| MTS - Multitasking Kernel                |
+------------------------------------------+
| PSX BIOS + Hardware                      |
+------------------------------------------+
```

### Actor System

The actor system (`source/libgv/actor.c`) is the central execution framework. Every game entity (Snake, enemies, cameras, map objects, daemons) is an actor.

```c
typedef struct _GV_ACT {
    struct _GV_ACT *prev;       // doubly-linked list
    struct _GV_ACT *next;
    GV_ACTFUNC      act;        // per-frame update callback
    GV_ACTFUNC      die;        // destruction callback
    GV_FREEFUNC     free;       // memory free callback
    const char     *filename;   // source file name (debug)
    int             runtime;    // total ticks executed
    int             count;      // frame counter
} GV_ACT;  // 32 bytes on PSX, larger on 64-bit
```

**9 priority levels** (executed in order each frame):

| Level | Name            | Pause | Kill | Purpose                                |
|-------|-----------------|-------|------|----------------------------------------|
| 0     | GV_ACTOR_DAEMON | 0     | 7    | System daemons (game manager, stage loader) |
| 1     | GV_ACTOR_MANAGER| 0     | 7    | Manager actors (camera manager)        |
| 2     | GV_ACTOR_LEVEL2 | 9     | 4    | Assist (loader, map)                   |
| 3     | GV_ACTOR_LEVEL3 | 9     | 4    | Prepare (collision setup)              |
| 4     | GV_ACTOR_LEVEL4 | 15    | 4    | Main actors (Snake, enemies)           |
| 5     | GV_ACTOR_LEVEL5 | 15    | 4    | Modify (lighting, effects)             |
| 6     | GV_ACTOR_AFTER  | 15    | 4    | Post-process (refer)                   |
| 7     | GV_ACTOR_AFTER2 | 9     | 4    | Late post-process (pause menu)         |
| 8     | GV_ACTOR_DAEMON2| 0     | 7    | Late daemons                           |

- **Pause level**: When `GV_PauseLevel >= actor_list.pause`, actors in that list are skipped
- **Kill level**: When `GV_DestroyActorSystem(level)` is called, lists with `kill <= level` are destroyed

### Stage Overlay System

MGS dynamically loads per-stage code at runtime. Stage codes map to game locations:

- `s00a` through `s20a`: Playable stages (Shadow Moses)
- `d00a` through `d18a`: Cutscene/demo stages
- `r` suffix: RED variants (e.g., `s03ar` = alert version of s03a)
- `select`, `title`, `opening`, `ending`: Menu/transition stages

Each stage's overlay contains:
- **Character entry table** (`_StageCharacterEntries`): Maps GCL `chara` IDs to actor constructor functions
- **Stage-specific actors**: Unique gameplay elements, triggers, scripted events

In the port, all overlays are compiled statically. Each stage's entry table is given a unique symbol via `-D_StageCharacterEntries=_StageCharacterEntries_stagename`.

### GCL Scripting

GCL (Game Command Language) is a bytecode scripting language used for stage logic. GCL scripts are stored in STAGE.DIR with extension `'e'` and control:

- `chara`: Spawn character actors at positions
- `map`: Load map geometry (KMD models)
- `camera`: Set up camera positions/paths
- `pad`: Configure controller input behavior
- `start`: Entry point / initialization commands
- `trap`: Trigger zones
- `radio`: Radio codec conversations

GCL bytecode reads 32-bit values in big-endian format. The interpreter (`source/libgcl/`) evaluates expressions, manages a variable table, and dispatches commands to registered handlers.
