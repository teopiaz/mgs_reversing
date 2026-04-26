# Stage authoring: OBJ + PNG → playable PSX stage

The editor can load any custom stage produced by
`tools/import_stage.py`. The pipeline takes hand-authored Blender/OBJ
geometry, turns it into the PSX-native `KMD` + `HZD` + `PCX` formats,
packs them into a `DATACNF` blob the engine's stage loader expects, and
drops the result into `port/editor/extra_stages/<name>/`.

The original ISO is never touched. Custom stages live alongside the
disc's 95 and appear in the editor's stage picker automatically.

## Inputs

A custom stage takes three files:

| File                   | Purpose                                                |
| ---------------------- | ------------------------------------------------------ |
| `<name>.obj` + `.mtl`  | Visual geometry and texture binding. Blender exports OBJ natively. |
| `<name>.png`           | Diffuse texture, any size — quantised to 8-bit indexed and tiled into VRAM. |
| `<name>_collision.obj` | Low-poly collision mesh. Object naming drives wall/floor classification (see [Collision authoring](#collision-authoring) below). Optional — falls back to a flat plane covering the visual mesh's XZ bbox. |

Recommended Blender flow:

1. Build the visual mesh in Blender. Apply all transforms before
   export (`Object` → `Apply` → `All Transforms`).
2. Build a separate, low-poly collision mesh. Walkable surfaces only.
3. UV-unwrap the visual mesh to a single texture; export as a PNG
   (any resolution — the importer downsamples to 256×256 max).
4. Export both meshes via `File → Export → Wavefront (.obj)`.
   - Include UVs.
   - Include normals if you have them; the importer ignores them
     (the editor renders unlit).
   - Export each mesh as its own `.obj` file.

A demo set ships at `port/editor/assets/s99a/{s99a.obj, s99a.mtl,
s99a.png, s99a_collision.obj}`.

## Run the importer

```
python3 tools/import_stage.py \
    --name s99a \
    --input    port/editor/assets/s99a/s99a.obj \
    --texture  port/editor/assets/s99a/s99a.png \
    --collision port/editor/assets/s99a/s99a_collision.obj
```

| Flag              | Default                                          | Notes |
| ----------------- | ------------------------------------------------ | ----- |
| `--name`          | basename of `--input`                            | becomes the stage name in the picker |
| `--input`         | required                                         | visual OBJ |
| `--texture`       | none                                             | PNG (untextured if omitted) |
| `--collision`     | flat XZ-bbox quad                                | collision OBJ |
| `--out`           | `port/editor/extra_stages/<name>/`               | output directory |
| `--scale`         | `100.0`                                          | OBJ-units → PSX world units multiplier |
| `--no-bothface`   | off                                              | omit `DG_MODEL_BOTHFACE` from the KMD model flags |
| `--manifest <p>`  | —                                                | re-run the build using settings recorded by a previous run; what the editor's `Reimport` button uses |

Console output lists every stage of the build:

```
[import] stage      : s99a
[import] visual obj : .../s99a.obj
[import] texture    : .../s99a.png
[import] collision  : .../s99a_collision.obj
[import] visual: 8 verts, 12 tris, 1 mats
[import] hzd: 2 floors
[import] pcx: 5193 bytes
[import] gcx: 56 bytes  (kmd hash 0x6BBB)
[import] wrote      : .../extra_stages/s99a/datacnf.bin  (12288 bytes, 6 sectors)
```

Outputs:

```
port/editor/extra_stages/s99a/
  datacnf.bin     # what the editor's libfs reads at startup
  manifest.json   # records the input paths so Reimport can re-run
```

## Loading the new stage

1. **Restart the editor** — `FS_StartDaemon` scans `extra_stages/`
   once at startup. Or:
2. **Click `Reload`** in the Scene tab if you re-imported the same
   stage; the editor wipes the GV heap + cache + VRAM and re-reads
   `datacnf.bin` from disk.
3. **Click `Reimport`** if you re-exported from Blender — it calls the
   Python pipeline first, then reloads.

The picker dropdown lists `s99a` alongside `s01a`, `s02a`, …. Selecting
it works exactly like a built-in stage: free-cam, screenshot, save
position, etc.

## Pipeline internals

```
.obj/.mtl  ─►  obj_reader.parse_obj   ─►  ObjMesh
                                          ├─►  kmd_writer.write_kmd        ─►  KMD bytes
                                          └─►  pcx_writer.write_pcx (PNG)  ─►  PCX bytes (custom)
.obj (collision) ─►  obj_reader.parse_obj ─► hzd_writer.write_hzd          ─►  HZD bytes
                                                gcl_writer.write_gcx       ─►  scenerio.gcx
                                          ┌────  ─────────────────────────┘
                                          ▼
                                    datacnf_writer.build_datacnf   ─► datacnf.bin
```

Every module lives in `tools/stage_assets/`. They're intentionally
narrow — one file, one format. See [06-asset-formats.md](06-asset-formats.md)
for the byte layouts each writer produces.

## Collision authoring

The collision OBJ classifies each face as a **wall** or a **floor**
based on the Blender object (`o name`) it belongs to:

| Object name pattern    | Classification | Importer behaviour                            |
| ---------------------- | -------------- | --------------------------------------------- |
| `wall_*` / `wall*`     | wall           | Each face → one `HZD_SEG`. Bottom edge → `p1`/`p2`; vertical extent → wall height (used both by collision and by the soliton radar). |
| `trap_<name>`          | trigger volume | All faces in the group → one `HZD_TRP`. AABB of the group's verts → `b1` / `b2`; `<name>` (≤ 12 chars) goes into the trap's name field. Bind it from GCL with `HZD_BIND` to fire a handler when an actor enters. |
| Anything else (`floor`, `floor_*`, default unnamed) | floor | Each face → one `HZD_FLR` quad. |

Comparison is case-insensitive on the `wall` and `trap_` prefixes, so
`Wall_Bunker`, `WALL_NORTH`, or `Trap_Alarm` all work.

### Authoring walls in Blender

Model each wall as a **vertical quad** inside a `wall_*` object:

- Two verts on the floor plane (Y = 0).
- Two verts at wall top (e.g. Y = 30 in your authoring units).
- The face normal can point either way — the engine renders walls
  from both sides anyway.

The importer takes the two verts with the smallest |Y| as the
bottom edge (the segment that appears on the radar minimap) and
uses `(max_y − min_y)` as the per-wall height (the range that
blocks Snake's collision box). Long walls are split into multiple
quads; each becomes its own `HZD_SEG`.

**Skipped faces.** Faces inside a `wall_*` object whose vertical
extent is < 1 PSX unit are silently dropped. This is intentional —
ceiling / roof quads commonly share the `wall_*` group with the
surrounding walls (e.g. a bunker modelled as one `wall_room` object
with a roof on top), and treating them as walls would emit
zero-height segments that don't correspond to any real geometry.

### Authoring floors

Anything *not* in a `wall_*` or `trap_*` object becomes floor
geometry. Floors are emitted as `HZD_FLR` quads; the vertices' Y is
preserved (walkable height comes from the floor surface, not a
separate field).

### Authoring trigger volumes (`trap_*`)

Trap zones are AABBs that fire a GCL handler when an actor enters
or leaves. Author one per zone as a Blender object whose name
starts with `trap_`:

```
o trap_alarm        # → HZD_TRP { name = "alarm" }
v -2000 0 -2000
v  2000 0 -2000
...                 # rest of the cube
```

The importer takes the AABB of every vertex inside the group as
the volume bounds — model the cube any way you like, the writer
only cares about the min/max corners. Multiple faces under the
same `o trap_<name>` block merge into a single trigger; you don't
need to keep the geometry to a single quad.

The trap name (everything after `trap_`) is truncated to 12
characters and used as the lookup key for `HZD_BIND` in your
stage's `scenerio.gcl`:

```
bind alarm, sub_OnAlarmEnter, sub_OnAlarmExit ;
```

Without a `bind`, the trap volume still exists but does nothing
when entered. The editor's `HZD` tab lists every emitted trigger
with its name and AABB so you can spot-check placement before
running the engine.

### Hand-authored fallback: `l` line primitives

For OBJ files written by hand (no Blender involved), the legacy
convention still works:

```
v -100 0 -100
v  100 0 -100
v -100 0  100
...
l 1 2          # one wall from vert 1 to vert 2
l 2 3
```

`l` lines emit `HZD_SEG` walls with a default 3000-unit height. The
demo cube (`port/editor/assets/s99a/s99a_collision2.obj`) shows the
hand-authored shape; modern Blender-driven stages should prefer the
`wall_*` object convention above.

## Limits and conventions

- **Verts per model**: 128 (KMD's 7-bit indices). The KMD writer auto-
  splits a single OBJ object into multiple internal models when
  needed.
- **Texture format**: 8-bit indexed PCX with the engine's custom
  `PCXINFO` block. The importer always emits 256-colour 8bpp; 4bpp
  isn't generated yet.
- **Texture size**: any size up to 256×224 with the default VRAM
  placement. Wider/taller textures collide with the framebuffer or
  CLUT — see [06-asset-formats.md §VRAM layout](06-asset-formats.md).
- **One material/texture per stage**: the importer references all
  KMD faces at the texture whose name matches the stage. Mixed
  materials work in the OBJ but resolve to the same PCX.
- **Geometry winding**: the importer sets `DG_MODEL_BOTHFACE` by
  default so back-face culling never silently drops triangles.
  Disable with `--no-bothface` once the mesh is verified CCW from
  outside.
- **Coordinate system**: PSX uses +Y down. OBJ uses +Y up. The
  importer doesn't flip Y — the demo cube renders "upside down" by
  Blender intuition. If you author with +Y up in Blender, expect
  the world to feel inverted in the editor (which uses PSX +Y down
  conventions for the camera).
- **Triangulation**: OBJ polygons of any vertex count are
  fan-triangulated, then each triangle is emitted as a degenerate
  KMD quad `(i0, i1, i2, i2)`.
- **UV scale**: OBJ UVs in `[0, 1]` map to PSX 0..255 unsigned bytes,
  with `flip_v=True` (Blender's bottom-up V convention).
- **HZD floors / walls**: classification driven by Blender object
  name (see [Collision authoring](#collision-authoring) above).
  `wall_*` objects emit one `HZD_SEG` per face (bottom edge); other
  objects emit `HZD_FLR` quads. Hand-authored OBJs can also use `l`
  line primitives for walls (legacy fallback).
- **HZD triggers / camera zones / nav zones / patrol routes**: not
  generated yet — see [08-roadmap.md](08-roadmap.md).

## Editing in place

Once a stage loads, the editor's existing edit features apply:
- Move actors with the floating Selected card.
- Save the modified TSV.
- Click `Reimport` after re-exporting from Blender.

There is no "edit the geometry of an extra stage" feature — geometry
edits roundtrip through Blender → OBJ → import.
