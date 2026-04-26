#!/usr/bin/env python3
"""Build a custom MGS stage from OBJ + PNG inputs.

Usage:
  python3 tools/import_stage.py \\
      --name s99a \\
      --input  port/editor/assets/s99a/s99a.obj \\
      --texture port/editor/assets/s99a/s99a.png \\
      --collision port/editor/assets/s99a/s99a_collision.obj

Output: port/editor/extra_stages/<name>/datacnf.bin (and a manifest.json
recording the inputs so the editor's Reimport button can re-run us).

The output is a self-contained DATACNF blob the editor's runtime
override (port/libfs/libfs.c) discovers at startup and exposes through
the stage picker.
"""

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from stage_assets.constants import gv_strcode, DG_MODEL_BOTHFACE, texture_slot  # noqa: E402
from stage_assets.obj_reader   import parse_obj  # noqa: E402
from stage_assets.kmd_writer   import write_kmd  # noqa: E402
from stage_assets.pcx_writer   import write_pcx  # noqa: E402
from stage_assets.hzd_writer   import write_hzd  # noqa: E402
from stage_assets.gcl_writer   import write_gcx  # noqa: E402
from stage_assets.datacnf_writer import dar_archive, build_datacnf  # noqa: E402


def build_stage(name: str,
                obj_path: Path,
                png_path: Path | None,
                collision_path: Path | None,
                output_dir: Path,
                scale: float = 100.0,
                bothface: bool = True,
                flip_v: bool = True):
    print(f"[import] stage      : {name}")
    print(f"[import] visual obj : {obj_path}")
    print(f"[import] texture    : {png_path or '(none)'}")
    print(f"[import] collision  : {collision_path or '(flat fallback)'}")

    # --- visual mesh → KMD ---------------------------------------------
    visual = parse_obj(obj_path)
    print(f"[import] visual: {len(visual.positions)} verts, "
          f"{len(visual.faces)} tris, {len(visual.materials)} mats")
    # DG_MODEL_BOTHFACE = render front + back faces. Useful default while
   # an authored mesh's winding hasn't been verified — without it
   # back-face culling silently drops half the triangles.
    kmd_flags = DG_MODEL_BOTHFACE if bothface else 0
    kmd_blob = write_kmd(visual, default_texture=name, scale=scale,
                         flip_v=flip_v, flags=kmd_flags)

    # --- collision mesh → HZD ------------------------------------------
    if collision_path and collision_path.is_file():
        collision = parse_obj(collision_path)
    else:
        # Fall back to a flat plane covering the visual's XZ bbox.
        if visual.positions:
            xs = [p[0] for p in visual.positions]
            zs = [p[2] for p in visual.positions]
            minx, maxx = min(xs), max(xs)
            minz, maxz = min(zs), max(zs)
        else:
            minx, maxx, minz, maxz = -10, 10, -10, 10
        from stage_assets.obj_reader import ObjMesh, ObjFace
        collision = ObjMesh()
        collision.positions = [
            (minx, 0, minz), (maxx, 0, minz),
            (maxx, 0, maxz), (minx, 0, maxz),
        ]
        collision.faces = [
            ObjFace(verts=[(0, -1), (1, -1), (2, -1)], material=None),
            ObjFace(verts=[(0, -1), (2, -1), (3, -1)], material=None),
        ]
    hzd_blob = write_hzd(collision, scale=scale)
    from stage_assets.hzd_writer import (    # noqa: E402
        is_wall_object, is_trap_object, is_camera_object,
    )
    poly_faces = collision.poly_faces if collision.poly_faces else collision.faces
    n_wall_faces  = sum(1 for f in poly_faces if is_wall_object(f.object))
    n_trap_groups = len({f.object for f in poly_faces if is_trap_object(f.object)})
    n_cam_groups  = len({f.object for f in poly_faces if is_camera_object(f.object)})
    n_floor_polys = sum(1 for f in poly_faces
                        if not is_wall_object(f.object)
                        and not is_trap_object(f.object)
                        and not is_camera_object(f.object))
    n_wall_lines  = len(collision.lines)
    print(f"[import] hzd: {n_floor_polys} floor polys, "
          f"{n_wall_faces + n_wall_lines} walls "
          f"(faces={n_wall_faces} lines={n_wall_lines}), "
          f"{n_trap_groups} traps, {n_cam_groups} cameras")

    # --- textures → custom PCX entries (one per material) --------------
    # MTL `map_Kd` entries take priority. The legacy --texture argument
    # is a fallback: if the OBJ's MTL has no map_Kd for the default
    # material (named after the stage), we use --texture for that slot.
    materials_to_emit: list[tuple[str, Path]] = []
    seen = set()
    for mat_name, tex_rel in (visual.materials or {}).items():
        if not tex_rel:
            continue
        tex_path = (obj_path.parent / tex_rel).resolve() if not Path(tex_rel).is_absolute() else Path(tex_rel)
        if tex_path.is_file():
            materials_to_emit.append((mat_name, tex_path))
            seen.add(mat_name)
    if not materials_to_emit and png_path and png_path.is_file():
        # Legacy single-texture path: no MTL material had a map_Kd, so the
        # face references default to the stage-named material — match it.
        materials_to_emit.append((name, png_path))

    nocache_entries = []
    for slot, (mat_name, tex_path) in enumerate(materials_to_emit):
        px, py, cx, cy = texture_slot(slot)
        pcx_blob = write_pcx(tex_path, px=px, py=py, cx=cx, cy=cy)
        mat_hash = gv_strcode(mat_name)
        nocache_entries.append((mat_hash, 'p', pcx_blob))
        print(f"[import] pcx slot {slot}: '{mat_name}' (hash 0x{mat_hash:04X}) "
              f"→ vram px=({px},{py}) clut=({cx},{cy})  {len(pcx_blob)} B")
    if not materials_to_emit:
        print("[import] no textures resolved — model will render untextured")

    # --- scenerio.gcx --------------------------------------------------
    # Use a hand-authored scenerio.gcl if present alongside the OBJ;
    # otherwise generate the minimal stub that boots a stage that just
    # sits idle. A real scenerio is needed for the live game (./mgs) to
    # actually spawn the player and run the stage; the stub is enough
    # for editor inspection.
    kmd_hash = gv_strcode(name)
    scenerio_path = obj_path.parent / "scenerio.gcl"
    gcx_blob = write_gcx(name, kmd_hash,
                         source_path=scenerio_path if scenerio_path.is_file() else None)
    src_kind = "hand-authored" if scenerio_path.is_file() else "stub"
    print(f"[import] gcx: {len(gcx_blob)} bytes ({src_kind}, kmd hash 0x{kmd_hash:04X})")

    # --- assemble DATACNF ----------------------------------------------
    resident_entries = [
        (kmd_hash, 'k', kmd_blob),
        (kmd_hash, 'h', hzd_blob),
    ]

    # Bundle item KMDs (box_01..box_08) into every custom stage. Without
    # them `chara &ITEM ... -i b:13` (and any other item id) silently
    # drops the actor — see source/game/item.c GetResources, which now
    # NULL-guards a missing body.objs and returns -1. The blobs were
    # extracted from the disc once via tools/extract_disc_assets.py.
    bundled_dir = REPO / "tools" / "stage_assets" / "bundled"
    for i in range(1, 9):
        blob_path = bundled_dir / f"box_{i:02d}.kmd"
        if not blob_path.is_file():
            continue
        h = gv_strcode(f"box_{i:02d}")
        resident_entries.append((h, 'k', blob_path.read_bytes()))
        print(f"[import] bundled  : box_{i:02d}.kmd "
              f"(hash 0x{h:04X}, {blob_path.stat().st_size} B)")

    cache_blobs = [(gv_strcode("scenerio"), 'g', gcx_blob)]
    datacnf = build_datacnf(
        resident_dar = dar_archive(resident_entries),
        cache_blobs  = cache_blobs,
        nocache_dar  = dar_archive(nocache_entries),
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    out_path = output_dir / "datacnf.bin"
    out_path.write_bytes(datacnf)
    print(f"[import] wrote      : {out_path}  ({len(datacnf)} bytes, "
          f"{len(datacnf) // 2048} sectors)")

    manifest = {
        "name":      name,
        "obj":       str(obj_path),
        "texture":   str(png_path) if png_path else None,
        "collision": str(collision_path) if collision_path else None,
        "scale":     scale,
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))

    # --- editor actor TSV/JSON ----------------------------------------
    # If the asset dir has a hand-authored scenerio.gcl, run the
    # standard extractor so the editor's actor list / ed_actors_pick
    # cube markers see custom-stage entities. Without this the editor
    # silently shows zero actors for any `extra_stages/<name>/` stage.
    if scenerio_path.is_file():
        try:
            from extract_gcl_actors import extract  # type: ignore
        except ImportError:
            sys.path.insert(0, str(REPO / "tools"))
            from extract_gcl_actors import extract  # type: ignore
        editor_data_dir = REPO / "port" / "editor" / "data"
        editor_data_dir.mkdir(parents=True, exist_ok=True)
        json_out = editor_data_dir / f"{name}_actors.json"
        try:
            extract(scenerio_path, json_out)
            print(f"[import] actors: wrote {json_out.name} (+ .tsv)")
        except Exception as e:                       # noqa: BLE001
            print(f"[import] actors: extractor failed ({e}); editor list will be empty")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--name", required=False,
                    help="stage name (e.g. s99a). Defaults to the obj basename.")
    ap.add_argument("--input", help="path to visual .obj")
    ap.add_argument("--texture", help="path to .png to bake into PCX")
    ap.add_argument("--collision", help="path to collision .obj (defaults to flat)")
    ap.add_argument("--out", default=None,
                    help="output dir (defaults to port/editor/extra_stages/<name>/)")
    ap.add_argument("--scale", type=float, default=100.0,
                    help="multiply OBJ coords by this when baking to short16 PSX units")
    ap.add_argument("--no-bothface", action="store_true",
                    help="don't set DG_MODEL_BOTHFACE (only safe if the mesh winds CCW from outside)")
    ap.add_argument("--no-flip-v", action="store_true",
                    help="don't flip V on UV import (default flips because Blender's V=0 is bottom). "
                         "Try this if textures appear vertically wrong on sub-rect (atlas) UVs.")
    ap.add_argument("--manifest", help="re-run from a manifest.json instead of CLI args")
    args = ap.parse_args()

    if args.manifest:
        m = json.loads(Path(args.manifest).read_text())
        args.name      = args.name      or m["name"]
        args.input     = args.input     or m["obj"]
        args.texture   = args.texture   or m.get("texture")
        args.collision = args.collision or m.get("collision")
        args.scale     = m.get("scale", args.scale)

    if not args.input:
        ap.error("--input is required (or --manifest pointing at a previous run)")

    obj_path  = Path(args.input)
    name      = args.name or obj_path.stem
    out_dir   = Path(args.out) if args.out else (
                    REPO / "port" / "editor" / "extra_stages" / name)
    png_path  = Path(args.texture) if args.texture else None
    coll_path = Path(args.collision) if args.collision else None

    # GCL parse / lex errors carry a rendered "path:line:col + snippet"
    # message via GclSyntaxError. Print just that and exit non-zero so
    # the editor's Reimport button surfaces a single clear message
    # instead of a Python traceback.
    try:
        from gcl_parser import GclSyntaxError       # type: ignore
    except ImportError:
        sys.path.insert(0, str(REPO / "port" / "gcl_tools"))
        from gcl_parser import GclSyntaxError       # type: ignore
    try:
        build_stage(name, obj_path, png_path, coll_path, out_dir,
                    scale=args.scale, bothface=not args.no_bothface,
                    flip_v=not args.no_flip_v)
    except GclSyntaxError as e:
        # Flush the [import] progress lines we printed before the error so
        # stdout and stderr don't interleave on the user's terminal.
        sys.stdout.flush()
        print(f"\n[import] GCL syntax error:\n{e}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
