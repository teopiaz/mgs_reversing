# Roadmap

A snapshot of what's done, what's deliberately out of scope, and what
ideas are queued. Update this file when major features land.

## Done

### Inspection

- ✅ Load any of the disc's 95 stages via the picker, or any custom
  stage discovered in `extra_stages/`.
- ✅ Render every map-sized KMD unlit (handles multi-room stages like
  `s02a` with several KMDs).
- ✅ HZD overlay: walls (vertical line strips), floors (quads),
  triggers (AABBs + 12-char names), camera zones (AABB + frustum),
  nav zones, patrol routes.
- ✅ Per-stage actor markers: cube + colour swatch + rotation arrow,
  parsed from `scenerio.gcl` and `demo.gcl` via Python.
- ✅ World-axes gizmo, wireframe / untextured / VRAM viewer toggles.
- ✅ Stage-data tab: enumerate every entry in the GV cache.

### Camera

- ✅ Free-fly: WASD + Q/E + RMB-drag look + arrow rotation.
- ✅ Top-down preset (Home) and first-person preset.
- ✅ Goto-XYZ modal (`G` shortcut), bookmarks (right-click to save,
  left-click to load).
- ✅ Mouse cursor's world-floor coordinate read-out.
- ✅ Screenshot (PPM) of the hi-res FBO.

### Selection

- ✅ Click in viewport → ray-pick actors and HZD trap/camera AABBs.
- ✅ Tab / Shift-Tab cycle through filtered actors.
- ✅ Click rows in any tab to focus the camera + select.
- ✅ Floating *Selected* card (top-right) with full actor info.

### Editing

- ✅ Edit selected actor's `pos[3]` and rotation in the Selected card;
  changes apply live to the marker.
- ✅ Save edits back to `data/<stage>_actors.tsv`. Dirty marker on the
  Save button until persisted.

### Authoring

- ✅ `tools/import_stage.py` end-to-end pipeline: OBJ + PNG +
  collision OBJ → KMD + custom PCX + HZD + stub `scenerio.gcx` →
  DATACNF. Drops into `port/editor/extra_stages/<name>/`.
- ✅ Editor's libfs auto-discovers `extra_stages/` at startup; custom
  stages appear in the picker alongside disc stages.
- ✅ `Reimport` button in the editor shells out to the Python pipeline
  using `manifest.json` and reloads on success.
- ✅ Demo stage `s99a` ships as a textured open courtyard with
  perimeter wall, gate, and a small bunker with a closed door.
- ✅ HZD walls authored via OBJ `l` line primitives in the collision
  mesh; one HZD_SEG per segment.

## Deliberately out of scope (today)

- **Editing existing stages' geometry.** Stages from the disc are
  read-only as KMD. To change geometry you must use the import
  pipeline against your own OBJ.
- **Writing actor edits back to GCL.** The Save button writes the
  per-stage TSV. Round-tripping into `scenerio.gcl` text — and then
  back through `gcl_tools/gcl_compile.py` to bytecode — is its own
  feature.
- **HZD triggers / camera zones / nav mesh from authored meshes.**
  Floors (triangles) and walls (`l` lines) are emitted today; trigger
  volumes, camera zones, navmesh and patrol routes are still TODO.
- **Animation / skinning.** KMD parent/extend chains aren't generated.
- **Multi-PCX stages.** The importer ships one diffuse texture; mixing
  several materials within one OBJ all bind to the same PCX.
- **Modifying the original ISO.** Custom stages live in a side
  directory; the disc image is treated as immutable.

## Ideas queued (no commitment)

- **HZD trigger / camera-zone authoring**: named cubes in the
  collision OBJ (`o trap_alarm` + a box → `HZD_TRP` with that name).
  Same pattern for camera zones. Walls already work via `l` lines.
- **Round-trip GCL editing**: parse an existing `scenerio.gcl` into
  AST, mutate `chara -n x y z` lines from the editor, write back.
- **In-editor primitive draw**: add walls / boxes / spawn markers by
  clicking in the viewport and dragging.
- **Stage transition graph**: parse `load "stage_name"` calls in every
  scenerio.gcl and render a graph of which stages connect to which.
- **Screenshots → PNG** instead of PPM (the editor today writes PPM
  to avoid pulling in libpng).
- **Persistent editor state**: save current stage / camera /
  bookmarks to a config file so the layout survives restarts (more
  than just imgui.ini).
- **Multi-LOD KMDs**: KMD format has `n_visible` separate from
  `n_models`; never wired up in the writer.
- **Texture atlas import**: a single PNG with multiple sub-textures
  laid out, plus a JSON mapping material name → atlas region.
- **glTF support**: PR-friendly addition once OBJ flow is stable.
- **Live game preview**: launch the main `port/mgs` against the
  editor's `extra_stages/` so you can play the custom stage instead
  of just inspecting it.

## Known issues

See [11-known-issues.md](../11-known-issues.md) for port-wide issues.
Editor-specific quirks:

- **HZD `version >= 2`** required by the loader; importer always
  writes `2` to skip the warning.
- **PCX VRAM placement constraints**: 256×256 8-bit textures eat
  rows 256..511 columns 0..127. Don't put the CLUT in that range.
  See [06-asset-formats.md §VRAM layout](06-asset-formats.md).
- **OBJ winding**: the importer enables `DG_MODEL_BOTHFACE` by
  default. Disable with `--no-bothface` only after verifying the
  mesh is consistently CCW-from-outside.
- **GV memory pool**: 2 MiB. A single stage uses ~1 MiB; stage
  switches reset the heap so this is fine in practice. Custom stages
  with very large textures may hit allocation limits — split into
  multiple PCXes once that's supported.
