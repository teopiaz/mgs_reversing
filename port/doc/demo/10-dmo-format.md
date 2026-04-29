# DMO binary format

This is a deep dive on the on-disc bytes of MGS's streamed-cinematic
data — the format the engine consumes via `FS_StreamGetData(5)` and the
format the editor's DMO inspector + authoring tool produce. Read this
*after* [09-streamed-demos.md](09-streamed-demos.md), which explains
when and why the format is used.

The runtime structs live in
[`source/include/fmt_dmo.h`](../../../source/include/fmt_dmo.h). This
file documents the *wire layout* — what's on disc — which differs from
the in-memory C struct on 64-bit hosts due to pointer-size growth.

## Container: block stream

A `.dmo` is a contiguous sequence of fixed-format blocks packed back
to back, starting at a sector boundary inside `MGS/DEMO.DAT`. There is
no per-`.dmo` outer header; the first block at the dmo's sector is
itself a `DMO_DEF` (header) and consumers walk from there.

Every block begins with a 4-byte tag:

```
   0   1   2   3
 ┌───┬───┬───┬───┐
 │ T │ Sa│ Sb│ Sc│      T   = block type (uint8)
 └───┴───┴───┴───┘      S   = block size in bytes (uint24, little-endian)
                              size INCLUDES the tag itself
```

In code:

```c
unsigned int hdr  = *(uint32_t *)raw;
unsigned int type = hdr & 0xFF;
unsigned int size = hdr >> 8;
unsigned char *next_block = raw + size;
```

Block types observed in disc data:

| Type | Name | Used for |
| ---- | ---- | -------- |
| `0x01` | VOX  | Voice / sync audio packets — fed into the SPU stream |
| `0x02` | SUB? | Short event (subtitle / mesg dispatch) |
| `0x03` | ?    | Larger event packet |
| `0x04` | ?    | Small marker |
| `0x05` | **DMO** | The cinematic payload — first occurrence is `DMO_DEF`, all subsequent are `DMO_DAT` |
| `0x06` | ?    | Variable-length |
| `0x07` | ?    | Marker |
| `0x10` | TICK | 8-byte clock / sync marker — one per stream-tick |
| `0xF0` | END  | End-of-stream |
| `0xFF` | WRAP | Live-game streamer's ring-buffer wrap (not seen at the start of a fresh `.dmo`) |

The runtime calls `FS_StreamGetData(target_type)` to consume blocks
matching a given type — `target_type=5` for the cinematic record,
`target_type=2/3` for HUD events, etc. Each block stays in the ring
buffer until the consumer calls `FS_StreamClear`.

## Block 0x05: DMO data

The first type-5 block in a `.dmo` is the header (`DMO_DEF`). Every
subsequent type-5 block is a per-frame record (`DMO_DAT`). The two
share the same outer block tag; consumers distinguish by counting:
"the first one I see is the header, the rest are frames."

### `DMO_DEF` — header

Total size is variable; the size field in the tag covers the whole
block including the maps[] and models[] sub-tables that follow the
fixed-position fields.

```
offset  size  field        notes
   0      4   tag          (type=5, size includes everything below)
   4      4   frame        always 0 in a header (s32, LE)
   8      4   n_frames     total cinematic length in ticks
  12      4   n_maps       count of DMO_MAP entries
  16      4   n_models     count of DMO_MDL entries
  20      4   maps_off     byte offset to maps[]   (relative to block start)
  24      4   models_off   byte offset to models[] (relative to block start)
  28      …   maps[]       n_maps × 8  bytes  — DMO_MAP records
   …      …   models[]     n_models × 20 bytes — DMO_MDL records
```

`maps_off` and `models_off` are typically 28 (immediately after the
header) and `28 + n_maps*8`, but they're stored explicitly so the
authoring tool can pad freely. On the original PSX, the runtime fixes
these up to real pointers via `OFFSET_TO_PTR(header, &header->maps)`.

### `DMO_MAP` — 8 bytes

Background-map references. The runtime asserts each map's `cache_id`
is already in `GV_CacheSystem` before the cinematic plays; if not,
`CreateDemo` returns 0 and the demo is destroyed.

```
offset  size  field
   0      4   cache_id      hash + ext byte; matches the KMD's GV_CacheID
   4      4   filename      cache_id with ext byte cleared (rarely used)
```

### `DMO_MDL` — 20 bytes

Character / prop model references. Like `DMO_MAP`, requires the KMD
to be in cache.

```
offset  size  field
   0      4   type          sequential 1..N, used as the join key with DMO_ADJ
   4      4   flag          bit 0 = one-piece (skip motion player); other bits unused
   8      4   cache_id      KMD lookup
  12      4   filename      file-name hash (special-cases `m1e1`, `hind`)
  16      4   name          motion-segment name; usually 0 for cinematic puppets
```

Filename special-casing in `demothrd_8007CFE8`:

- `m1e1` / `m1e1demo` → handled by `demothrd_m1e1_8007D404` (extra
  caterpillar tracks + smoke trails on the M1E1 tank).
- `hind` / `hinddemo` → handled by `demothrd_hind_8007D9C8` (rotor
  blade animation + door state).

All other models follow the generic `GM_ActMotion` → `GM_ActControl`
→ `GM_ActObject` path.

### `DMO_DAT` — per-frame record

One block per cinematic frame. Frames are linear in the stream — a
strict run from 0 to `header.n_frames - 1`. The runtime advances by
calling `FS_StreamGetData(5)` once per stream-tick (typically every
2 engine ticks → 30 fps).

```
offset  size  field
   0      4   tag           (type=5)
   4      4   frame         this record's frame index (s32, LE)
   8      2   eye_x         camera position (s16, PSX world units)
  10      2   eye_y
  12      2   eye_z
  14      2   center_x      lookat target
  16      2   center_y
  18      2   center_z
  20      2   roll          camera roll (s16, PSX 4096 = 360°)
  22      2   clip_dist     PSX H register / FOV. 200 nominal; smaller = wider FOV
  24      2   pad           always 0 in disc data — likely alignment for chara_off
  26      2   n_charas      DMO_CHA spawn/despawn events this frame
  28      4   chara_off     byte offset to chara[] (relative to block start)
  32      2   n_adjusts     DMO_ADJ poses this frame
  34      2   pad
  36      4   adjust_off    byte offset to adjust[] (relative to block start)
  40      …   chara[]       n_charas × 52 bytes
   …      …   adjust[]      n_adjusts × 24 bytes
   …      …   rots[]        adjust-owned skeletal data (variable)
```

The 28/36-byte offsets align the pointer-sized fields on PSX; on
64-bit hosts the in-memory struct is 56 bytes, but the wire layout is
40 bytes for the fixed-position part.

The runtime applies a `DMO_DAT` by:

1. Setting `gUnkCameraStruct2_800B7868.eye/center` from the camera
   fields (and computing `clip_distance` directly into `DG_Chanl(0)`).
2. Building `DG_Chanls[0].eye_inv` via `DG_SetPos2` / `ReadRotMatrix`
   / `DG_TransposeMatrix` — bypassing `camera.c::Act` for streamed
   cinematics (see [04-camera-pipeline.md](04-camera-pipeline.md)).
3. Iterating `chara[]` to spawn / despawn cinematic characters.
4. Iterating `adjust[]` to update each `DEMO_MODEL`'s pose.

### `DMO_CHA` — 52 bytes

Spawn / despawn events for cinematic characters. A `DMO_CHA` is
emitted in the frame where a chara should start or stop existing —
not on every frame. Most cutscenes have zero `chara` entries per
frame after the opening setup.

```
offset  size  field            notes
   0      4   field_0          spawn id (used to dedupe across frames)
   4      4   field_4_type     chara hash (matches `&DEMODOLL` etc.)
   8      6   field_8_xyz      6 shorts — likely two SVECTORs
  14      6   field_E_xyz
  20      4   field_14_type    secondary type (usually = field_4_type)
  24      4   field_18_xy
  28      4   field_1C_zpad
  32      4   field_20
  36      4   field_24-27      4 chars
  40      4   field_28/2A      2 shorts
  44      4   field_2C
  48      4   field_30/32      2 shorts
```

The fields are largely undecompiled — this is the most opaque part
of the format. Disc cinematics keep the chara list lean (≤16 per
frame), and `MakeChara` in `demo.c` deduplicates by `field_0`.

### `DMO_ADJ` — 24 bytes

Per-character pose for this frame. Every character that should be
visible *or hidden* in the current frame gets one record. The
match-up to a `DMO_MDL` is by the `type` field — the runtime walks
`work->header->models[]` looking for a matching `model_file->type`.

```
offset  size  field        notes
   0      4   type         joins with DMO_MDL.type
   4      2   visible      0 = DG_InvisibleObjs(model->object.objs); 1 = visible + apply pose
   6      2   rot_x        root rotation (Euler, PSX 4096 = 360°)
   8      2   rot_y
  10      2   rot_z
  12      2   pos_x        world-space position (s16, PSX units)
  14      2   pos_y
  16      2   pos_z
  18      2   n_rots       per-bone Euler triplets in rots[]
  20      4   rots_off     byte offset to rots[], **relative to this DMO_ADJ struct**
                           (not the block start) — matches OFFSET_TO_PTR(adjust, &adjust->rots)
```

The `rots` payload is `n_rots * 3` shorts — one Euler XYZ triplet per
KMD bone. Bone order matches `DG_DEF.model[]` (parents come first, by
convention). The runtime applies them via:

```c
rots = adjust->rots;
for (i = 0; i < adjust->n_rots; i++, rots += 3) {
    model->object.rots[i].vx = rots[0];
    model->object.rots[i].vy = rots[1];
    model->object.rots[i].vz = rots[2];
}
```

— direct copy, no scaling. The KMD's render pipeline composes them
into a world matrix per bone via the bone hierarchy (see
`port/editor/ed_dmo.c::build_bone_matrices` for the editor's
equivalent).

### Typical block layout

For a DMO_DAT with one chara event and three adjusts (each with 30
joint rotations):

```
offset  size       contents
   0     40         tag + camera fields + counts/offsets
  40     52         chara[0]      (pointed to by chara_off = 40)
  92     24         adjust[0]     (pointed to by adjust_off = 92)
 116     24         adjust[1]
 140     24         adjust[2]
 164     180        adjust[0].rots[]   (90 shorts, pointed to by adjust[0].rots_off)
 344     180        adjust[1].rots[]
 524     180        adjust[2].rots[]
 704     —          (block ends; size in tag = 704)
```

`adjust[i].rots_off` is the offset *from `&adjust[i]`*, so for the
example above:

- adjust[0].rots_off = 164 - 92  = 72
- adjust[1].rots_off = 344 - 116 = 228
- adjust[2].rots_off = 524 - 140 = 384

Authoring tools must remember to compute these offsets relative to
each adjust, not to the block.

## End-of-stream

```
offset  size  field
   0      4   tag           type=0xF0, size=4 (just the tag itself)
```

A DMO terminates with a single 0xF0 block. The runtime's
`StreamAct` / `FileAct` use the frame counter (`work->frame >=
work->header->n_frames`) as the actual termination signal, so the
0xF0 marker is mostly redundant — but the inspector and authoring
tools emit it for parity.

## Endianness

All multi-byte fields are little-endian. PSX is LE-native; nothing
is byteswapped. The 4-byte tag is read as `u32 LE` then unpacked as
`type | (size << 8)`.

## Sector keys (DEMO.DAT only)

When a `.dmo` lives inside `DEMO.DAT` (the disc-shipped path), the
GCL `demo -s t:NNNNNNNN` directive identifies it by its **sector
offset** within DEMO.DAT — `NNNNNNNN` is a hex sector number (0x1441
= sector 5185 of the file → byte offset `0x1441 * 2048 = 0xA0_2000`).
The "filename" in the matching `demo -f "X.dmo"` is informational;
the runtime uses the sector offset.

Custom-stage cinematics produced by the editor live as standalone
`.dmo` files at `port/editor/data/dmo/custom/<name>.dmo`. The
inspector's `ed_dmo_open_file` reads them directly from disk —
sector keys aren't relevant for that path.

## Reference implementations

| Read / parse | Code |
| ------------ | ---- |
| Live-game runtime | [`source/kojo/demothrd.c::StreamAct`](../../../source/kojo/demothrd.c) → [`FrameRunDemo`](../../../source/kojo/demo.c#L385) |
| Editor inspector | [`port/editor/ed_dmo.c::parse_dmo_blocks`](../../../port/editor/ed_dmo.c) (`parse_def_block`, `parse_dat_block`) |
| Python catalogue tool | [`tools/mgs_tools/cli/extract_dmo.py::read_header_block`](../../../tools/mgs_tools/cli/extract_dmo.py) |

| Write / encode | Code |
| -------------- | ---- |
| Editor authoring tool | [`port/editor/ed_dmo.c::ed_dmo_timeline_save`](../../../port/editor/ed_dmo.c) |

## Sizes you can rely on

These are wire sizes — what's on disc — not C `sizeof()` on the host:

| Struct | Wire size | Notes |
| ------ | --------- | ----- |
| Block tag | 4 | type:8 \| size:24 LE |
| `DMO_DEF` (fixed part) | 28 | maps[] / models[] follow |
| `DMO_MAP` | 8 | |
| `DMO_MDL` | 20 | |
| `DMO_DAT` (fixed part) | 40 | chara[] / adjust[] follow; rots[] last |
| `DMO_CHA` | 52 | |
| `DMO_ADJ` | 24 | |
| `rots[]` element | 6 | 3 × s16 (Euler XYZ) |

On 64-bit hosts the C structs grow because of the trailing pointer
fields (`maps`, `models`, `chara`, `adjust`, `rots`) — these are
4-byte offsets on disc that the runtime fixes up to 8-byte pointers
in memory via `OFFSET_TO_PTR`. The port's helpers
(`port_dmo_fixup_def` / `port_dmo_fixup_dat` / `parse_dmo_blocks`)
copy field-by-field from the wire layout into 64-bit-aligned local
structs to avoid ARM64 alignment faults.

## Fields whose interpretation is unclear

The format is mostly understood, but a few quirks remain:

- `DMO_DAT.pad @ 24` and `pad @ 34`: always 0 in disc data, exists
  for chara/adjust pointer alignment on the PSX in-memory layout.
  Authoring tools should write 0.
- `DMO_CHA` field_0 / field_4_type / field_14_type semantics: the
  spawn id, primary chara hash, and a duplicate hash. Why the
  duplicate exists is unclear — possibly version remnants.
- `DMO_MDL.flag` bit 0 = "one-piece" (skip GM_ConfigMotionControl,
  use GM_ActObject2 path). Other bits never set in disc data.
- `DMO_MDL.name`: usually 0 for cinematic puppets; non-zero on
  GCL-spawned actors that need a script-bind. Cinematic data hardcodes 0.

## Authoring notes

When emitting custom dmos:

- Set `n_maps = 0` if you don't need extra rooms loaded — the
  cinematic still works using whatever stage geometry was already in
  the loader's cache.
- `n_models = 0` produces a camera-only cinematic (the editor uses
  this when no doll tracks are present).
- Model `type` should be 1, 2, 3, ... (sequential, monotonic). The
  runtime joins by exact match, so re-using a `type` confuses things.
- `clip_dist` of 0 in a record will produce a degenerate frustum.
  The runtime's `FrameRunDemo` writes `DG_Chanl(0)->clip_distance`
  directly from this field — keep it ≥ 64. Editor's bake clamps to
  200 if a key has clip = 0.
- `roll` outside the range [-2048, 2048] produces over-rotation;
  most disc cinematics use 0.
- The end-of-stream 0xF0 block is optional but recommended; the
  editor and disc both emit it.
