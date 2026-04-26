# Asset formats: writer reference

Documentation for the on-disk byte layouts used by the OBJ→KMD pipeline.
Reading-side documentation lives elsewhere
([port/doc/04-rendering.md](../04-rendering.md),
[port/doc/06-filesystem.md](../06-filesystem.md),
[port/doc/08-collision.md](../08-collision.md)); this page covers what
the importer's writers emit, with offsets and quirks discovered while
building the pipeline.

All multi-byte fields are **little-endian** unless noted.

---

## KMD

Reference: [`port/libdg/kmd_loader.c`](../../libdg/kmd_loader.c). Note
that the comment `/* 76 bytes, matches PSX DG_MDL */` in that file is
**wrong** — `sizeof(KMD_MDL_RAW)` under `#pragma pack(1)` is **88
bytes** (the struct has three 12-byte `DG_VECTOR`s for min/max/pos,
not two).

### File header (`KMD_DEF_RAW`, 32 bytes)

| Offset | Type    | Field        | Notes                                |
| ------ | ------- | ------------ | ------------------------------------ |
| 0x00   | int32   | `n_visible`  | Usually equal to `n_models`.         |
| 0x04   | int32   | `n_models`   | Max 256.                             |
| 0x08   | DG_VECTOR | `min`      | int32 × 3, global bbox lower bound. |
| 0x14   | DG_VECTOR | `max`      | int32 × 3, global bbox upper bound. |

Followed immediately by `n_models × KMD_MDL_RAW`.

### Per-model header (`KMD_MDL_RAW`, 88 bytes)

| Offset | Type    | Field           | Notes                              |
| ------ | ------- | --------------- | ---------------------------------- |
| 0x00   | int32   | `flags`         | OR of `DG_MODEL_*`. `0x400 = DG_MODEL_BOTHFACE` is the safe default for new geometry. |
| 0x04   | int32   | `n_faces`       | Quad count.                        |
| 0x08   | DG_VECTOR | `min`         | Per-model bbox.                    |
| 0x14   | DG_VECTOR | `max`         | Per-model bbox.                    |
| 0x20   | DG_VECTOR | `pos`         | Local translation (rarely used for static maps; set to 0). |
| 0x2C   | int32   | `parent`        | Parent model index, or -1 for top-level. |
| 0x30   | int32   | `extend`        | Extension model index, or -1.      |
| 0x34   | int32   | `n_verts`       | Max 128 (7-bit indices).           |
| 0x38   | uint32  | `vertices_off`  | Byte offset from file start.       |
| 0x3C   | uint32  | `vindices_off`  | Byte offset from file start.       |
| 0x40   | int32   | `n_normals`     | 0 if no normals (unlit).           |
| 0x44   | uint32  | `normals_off`   | 0 if `n_normals == 0`.             |
| 0x48   | uint32  | `nindices_off`  | 0 if no normals.                   |
| 0x4C   | uint32  | `texcoords_off` | Byte offset.                       |
| 0x50   | uint32  | `materials_off` | Byte offset.                       |
| 0x54   | int32   | `padding`       | Set to 0.                          |

Followed by per-model arrays at the offsets above.

### Vertex array

`n_verts × 8 bytes`:

| Offset | Type  | Field |
| ------ | ----- | ----- |
| +0     | int16 | `vx`  |
| +2     | int16 | `vy`  |
| +4     | int16 | `vz`  |
| +6     | int16 | pad — set to 0 for top-level models. The engine's parent-link logic uses non-zero values to attach child verts to a parent's mesh. |

Coordinate range: full `int16` (±32767). MGS stages typically span
±25000 PSX world units; the importer's default `scale = 100.0` makes
OBJ units of `±50` map to PSX `±5000`.

### Vertex indices (`vindices`)

`n_faces × 4 bytes`. Each face is one `uint32` packing four 7-bit
vertex indices:

```
bits 0..6   = i0     bit 7   = parent-link flag (must be 0)
bits 8..14  = i1     bit 15  = parent-link flag
bits 16..22 = i2     bit 23  = parent-link flag
bits 24..30 = i3     bit 31  = parent-link flag
```

For a triangle, emit a degenerate quad: `(i0, i1, i2, i2)`. The
renderer's tri-2 (verts 1, 2, 3) collapses to a zero-area triangle.

### Texture coordinates (`texcoords`)

`n_faces × 8 bytes`. Per face, four `(u, v)` pairs as unsigned bytes
in `0..255` mapping linearly into the texture (255 = right/bottom of
the page).

The renderer reads them at indices 0..3 and pairs them with vertex
indices 0..3, then submits two triangles: `(v0, v1, v3)` and
`(v1, v2, v3)`. So for a real quad authored as `(p0, p1, p2, p3)`, the
texcoord array order matches: `u_p0 v_p0 u_p1 v_p1 u_p2 v_p2 u_p3 v_p3`.

The writer **does** apply a swap for legacy reasons (positions 2 and 3
in the file are swapped to match the engine's `POLY_GT4` color-pack
ordering used elsewhere in the renderer); for triangles emitted as
degenerate quads `(i0, i1, i2, i2)`, both file slots 2 and 3 hold the
same UV value, so the swap is a no-op in practice.

UV gotcha: `u % 1.0` in Python returns 0 for `u == 1.0`, collapsing
V=1 onto V=0. The writer clamps to `[0, 1]` instead of using modulo —
fixing this was load-bearing for textures to render correctly across
faces.

### Materials

`n_faces × 2 bytes`. Each face's `uint16` is the GV `StrCode` hash of
the texture's name, matching what the runtime gets out of
`DG_GetTexture(materials[fi])`.

`GV_StrCode` is a 16-bit rotate-left-5 + add hash. See
[`port/gcl_tools/constants.py`](../../gcl_tools/constants.py) for the
canonical Python implementation.

### Layout

```
+0x000  KMD_DEF_RAW (32 B)
+0x020  KMD_MDL_RAW[0] (88 B)
+0x078  KMD_MDL_RAW[1] (88 B)            (only if n_models > 1)
...
        vertices[0]   (n_verts0 × 8 B)
        vindices[0]   (n_faces0 × 4 B)
        texcoords[0]  (n_faces0 × 8 B)
        materials[0]  (n_faces0 × 2 B)
        vertices[1]   ...
        ...
```

The writer lays out each model's arrays contiguously after the per-model
headers — order between models doesn't matter as long as every
`*_off` field points where the data actually lives.

---

## PCX (custom variant)

Reference: [`port/libdg/pcx_loader.c`](../../libdg/pcx_loader.c) and
[`source/include/fmt_tex.h`](../../../source/include/fmt_tex.h).

The first 128 bytes are a **standard PCX header**, but offsets 74..87
are reused as a `PCXINFO` block carrying VRAM coords:

### Standard fields (offsets 0..73)

| Offset | Type  | Value emitted by the writer |
| ------ | ----- | --------------------------- |
| 0x00   | u8    | `0x0A` (manufacturer)       |
| 0x01   | u8    | `5` (PCX 3.0)               |
| 0x02   | u8    | `1` (RLE encoding)          |
| 0x03   | u8    | `8` (bits per pixel per plane) |
| 0x04   | u16   | `min_x = 1`                 |
| 0x06   | u16   | `min_y = 1`                 |
| 0x08   | u16   | `max_x = width`  (loader does `width = max_x − min_x + 1`) |
| 0x0A   | u16   | `max_y = height`            |
| 0x0C   | u16   | `dpi_x = 72`                |
| 0x0E   | u16   | `dpi_y = 72`                |
| 0x10   | 48 B  | `header_palette[16][3]`. Unused for 8bpp; left zero. |
| 0x40   | u8    | `reserved = 0`              |
| 0x41   | u8    | `n_planes = 1`              |
| 0x42   | u16   | `bytes_per_line = width`    |
| 0x44   | u16   | `header_palette_class = 1`  |
| 0x46   | u16   | `screen_width = 320`        |
| 0x48   | u16   | `screen_height = 224`       |

### PCXINFO (offsets 0x4A..0x57, 14 bytes)

| Offset | Type  | Field        | Notes                                          |
| ------ | ----- | ------------ | ---------------------------------------------- |
| 0x4A   | u16   | `magic`      | Always `12345` — the loader checks this.       |
| 0x4C   | u16   | `flags`      | bit 0: 0 = 4bpp, 1 = 8bpp; bits 4..5 = ABR.    |
| 0x4E   | u16   | `px`         | Texture top-left X in VRAM (16-bit-pixel units). |
| 0x50   | u16   | `py`         | Texture top-left Y in VRAM.                    |
| 0x52   | u16   | `cx`         | CLUT top-left X in VRAM.                       |
| 0x54   | u16   | `cy`         | CLUT top-left Y in VRAM.                       |
| 0x56   | u16   | `n_colors`   | 16 for 4bpp, 256 for 8bpp.                     |

### Pixel data (offset 0x80 onward)

Standard PCX RLE. Each row is independently encoded: a byte ≥ `0xC0`
starts a run of `(byte & 0x3F)` of the next literal byte; otherwise
the byte is a literal. Bytes ≥ `0xC0` must always be encoded as a
run of length 1. After the pixel data:

```
0x0C        # palette signal byte
256 × 3 B   # RGB triplets, palette index order
```

The writer:
1. Quantises the input PNG via `Pillow.Image.quantize(colors=256)`.
2. RLE-encodes each row independently.
3. Appends the `0x0C` signal + 768-byte RGB palette.

### VRAM layout

PSX VRAM is 1024×512 16-bit pixels. The port's framebuffer occupies
`y=0..223`. The writer's defaults
([`tools/stage_assets/constants.py`](../../../tools/stage_assets/constants.py)):

| Region         | VRAM coords (16-bit pixels)        |
| -------------- | ---------------------------------- |
| Framebuffer    | x=0..319, y=0..223                 |
| **CLUT** (default) | x=0..255, y=240..240          |
| **Texture** (default 256×256, 8bpp) | x=0..127, y=256..511 |

The CLUT slot at `y=240` lives in the unused band between the
framebuffer and the texture page region. An earlier default of
`y=480` overlapped the bottom 32 rows of a 256-wide 8bpp texture
(which spans `x=0..127`, `y=256..511`); the texture's `LoadImage`
runs after the CLUT's, clobbering the lower half of the palette.
Symptom: every face renders as one colour, even though the texture
in VRAM looks correct.

The PSX hardware encodes texture/CLUT positions with these formulas
(from [`source/libdg/text.c`](../../../source/libdg/text.c)):

```c
tpage = (px / 64) | ((py / 256) << 4) | (tp << 7) | (abr << 5)
clut  = (cy << 6) | (cx >> 4)
```

Hence `cx` must be a multiple of 16.

---

## HZD

Reference: [`port/libhzd/hzd_loader.c`](../../libhzd/hzd_loader.c) and
[`source/include/fmt_hzd.h`](../../../source/include/fmt_hzd.h).

### File header (`HZD_MAP_RAW`, 24 bytes)

| Offset | Type   | Field        | Notes                            |
| ------ | ------ | ------------ | -------------------------------- |
| 0x00   | int16  | `version`    | Use `2` to skip the loader's "old version" warning. |
| 0x02   | int16  | `min_x`      | Map bounding bbox (XZ plane).    |
| 0x04   | int16  | `min_y`      | Y in **HZD coords** — see swap note below. |
| 0x06   | int16  | `max_x`      |                                  |
| 0x08   | int16  | `max_y`      |                                  |
| 0x0A   | int16  | `n_groups`   | Usually 1 for an authored map.   |
| 0x0C   | int16  | `n_zones`    | 0 — no nav mesh in the importer. |
| 0x0E   | int16  | `n_routes`   | 0 — no patrol routes.            |
| 0x10   | uint32 | `groups_off` | File offset (writer puts groups at 0x18). |
| 0x14   | uint32 | `zones_off`  | 0 if `n_zones = 0`.              |
| 0x18   | uint32 | `routes_off` | 0 if `n_routes = 0`.             |

### Per-group header (`HZD_GRP_RAW`, 24 bytes)

| Offset | Type   | Field          | Notes                          |
| ------ | ------ | -------------- | ------------------------------ |
| 0x00   | int16  | `n_triggers`   | 0                              |
| 0x02   | int16  | `n_walls`      | 0                              |
| 0x04   | int16  | `n_floors`     | One per collision triangle.    |
| 0x06   | int16  | `n_flat_walls` | 0                              |
| 0x08   | uint32 | `walls_off`    | 0                              |
| 0x0C   | uint32 | `floors_off`   | File offset to the `HZD_FLR` array. |
| 0x10   | uint32 | `triggers_off` | 0                              |
| 0x14   | uint32 | `wallsFlags_off` | 0                            |

### `HZD_FLR` (48 bytes)

Six `HZD_VEC`s in order:

```
b1, b2     # AABB of the floor (used by the broadphase)
p1, p2, p3, p4   # the floor's four corners
```

Each `HZD_VEC` is **8 bytes**: `int16 x, z, y, h`. Note the **z/y
swap** vs. PSX `SVECTOR` — fields appear in the order `x, z, y` in
memory. Forgetting this gives upside-down or sideways floors. The
field `h` is unused by the writer (set to 0).

For a triangle `(p0, p1, p2)` the writer emits `p1=p0, p2=p1, p3=p2,
p4=p2` (degenerate quad).

---

## DATACNF

Reference: [`source/libfs/datacnf.h`](../../../source/libfs/datacnf.h)
and the live tag walker in
[`port/libfs/libfs.c`](../../libfs/libfs.c) (`FS_LoadStageRequest`).

A complete stage blob is a sequence of sector-aligned (2048 B) regions:

```
sector 0    DATACNF header + tag table (+ pad to 2048)
sector N    'r' resident DAR  — KMD + HZD here
            'c' cache region  — scenerio.gcx (NOT sector-padded; only 4-byte aligned)
sector M    'n' nocache DAR   — PCX texture(s)
```

### `DATACNF` header (4 bytes + tag table)

| Offset | Type  | Field      | Notes                                   |
| ------ | ----- | ---------- | --------------------------------------- |
| 0x00   | u8    | `version`  | Always 1.                               |
| 0x01   | u8    | `pad`      | 0.                                      |
| 0x02   | u16   | `size`     | Total stage size in **sectors**.        |
| 0x04   | tags  | `tags[]`   | `DATACNF_TAG` array, terminated by `mode == 0`. |

### `DATACNF_TAG` (8 bytes)

| Offset | Type | Field   |
| ------ | ---- | ------- |
| 0x00   | u16  | `id`    |
| 0x02   | i8   | `mode`  |
| 0x03   | i8   | `ext`   |
| 0x04   | i32  | `size`  |

Tag modes:

| `mode` | Region kind               | `size` field meaning                  |
| ------ | ------------------------- | ------------------------------------- |
| `'r'`  | Resident DAR              | byte size of the region (sector-padded after) |
| `'c'`  | Cache entry               | byte offset within the c-region       |
| `'c'` + `ext == 0xFF` | Terminator | total c-region byte size (4-byte aligned, **NOT sector-padded**) |
| `'n'`  | Nocache DAR (textures)    | byte size of the region (sector-padded after) |
| `'s'`  | Sound / overlay           | byte size; tag-specific behaviour by `ext` |
| `0`    | End of tag table          | 0                                     |

The importer emits the minimum that boots in the editor: one `'r'`,
two `'c'` (entry + terminator), one `'n'`, the end marker.

### Sector alignment trap

A bug in early iterations: the writer was sector-padding the `'c'`
region before the `'n'` region. The loader only **4-byte-aligns**
the `'c'` region (`data_ptr = c_base + ((c_total + 3) & ~3)`), so the
extra padding pushed the `'n'` data ahead of where the loader looked
for it. The DAR walker then read whatever happened to be at the
expected offset, found values that decoded as plausible
`DARFILE_TAG`s, and "processed" hundreds of fake entries before
hitting the buffer end. Symptom: `Processed 249 nocache entries` in
the log instead of `Processed 1`. Fix:
[`tools/stage_assets/datacnf_writer.py`](../../../tools/stage_assets/datacnf_writer.py)
only 4-byte-aligns `c_bytes`.

### `DARFILE_TAG` (8 bytes)

The per-entry header inside an `'r'` or `'n'` archive:

| Offset | Type | Field |
| ------ | ---- | ----- |
| 0x00   | u16  | `id`  |
| 0x02   | i16  | `ext` |
| 0x04   | i32  | `size` |

Followed by `size` bytes of payload, no padding between entries. The
runtime cache id is `cache_id = ((ext - 'a') << 16) | id`. Looking
that id up via `GV_GetCache` returns the parsed asset (e.g. a
`DG_DEF *` for a KMD).

---

## scenerio.gcx (minimum)

Reference: [`port/gcl_tools/`](../../gcl_tools/) (the existing GCL
toolchain).

The importer hand-crafts a tiny `.gcl` source and runs it through
`gcl_compile.py`:

```gcl
proc sub_XXXX {
    eval($f:000001 = b:0)
}

script {
    map -d $s:HHHH
}
```

Where:

- `XXXX` — any 4-hex proc id; the importer derives a stable one from
  the stage name's hash.
- `HHHH` — `gv_strcode(stage_name)` matching the KMD's DAR id (so
  `map -d` finds the correct cached `DG_DEF`).

The bytecode lands in the DATACNF `'c'` region. The runtime executes
it during stage load, populating `GM_CurrentStage*` pointers.

Anything more elaborate (player spawn position, NPCs, lighting,
camera zones, demos) requires writing real GCL — out of scope for the
authoring pipeline today.
