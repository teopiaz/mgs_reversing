"""Minimal Wavefront OBJ + MTL parser for the stage authoring pipeline.

Only what we need: positions (`v`), texcoords (`vt`), faces (`f`) with
`v/vt` syntax, optional `usemtl` blocks. No normals, no quads-only
restriction (we triangulate fan-style at parse time, then the KMD writer
re-pairs into quads where possible).
"""

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class ObjFace:
    verts: list           # list of (pos_idx, uv_idx) tuples after fan-triangulation
    material: str | None  # name from `usemtl`; None if untextured


@dataclass
class ObjMesh:
    positions:  list = field(default_factory=list)   # [(x, y, z), ...]
    uvs:        list = field(default_factory=list)   # [(u, v), ...]
    faces:      list = field(default_factory=list)   # [ObjFace, ...]  — fan-triangulated
    poly_faces: list = field(default_factory=list)   # [ObjFace, ...]  — original n-gons (3..N verts)
    lines:      list = field(default_factory=list)   # [(p0_idx, p1_idx), ...]
    materials:  dict = field(default_factory=dict)   # name -> texture path (str | None)


def _parse_face_vert(token: str) -> tuple[int, int]:
    """Parse `v`, `v/vt`, `v//vn`, `v/vt/vn` — return (pos_idx, uv_idx)."""
    parts = token.split("/")
    pos = int(parts[0]) - 1
    uv  = int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1
    return pos, uv


def parse_mtl(path: Path) -> dict:
    """Parse a Wavefront MTL file. Returns {material_name: texture_path_or_None}."""
    out = {}
    if not path.is_file():
        return out
    cur = None
    for raw in path.read_text(errors="replace").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        toks = s.split()
        if toks[0] == "newmtl" and len(toks) > 1:
            cur = toks[1]
            out[cur] = None
        elif toks[0] in ("map_Kd", "map_kd") and cur and len(toks) > 1:
            tex_path = " ".join(toks[1:])
            out[cur] = tex_path
    return out


def parse_obj(path: Path) -> ObjMesh:
    """Parse an OBJ file. Triangulates polygons via fan from vertex 0."""
    mesh = ObjMesh()
    cur_mat = None

    for raw in path.read_text(errors="replace").splitlines():
        # Strip trailing inline comments before tokenising — Blender's
        # OBJ exporter doesn't emit any but our hand-authored demo does.
        if "#" in raw:
            raw = raw.split("#", 1)[0]
        s = raw.strip()
        if not s:
            continue
        toks = s.split()
        kw = toks[0]

        if kw == "v" and len(toks) >= 4:
            mesh.positions.append((float(toks[1]), float(toks[2]), float(toks[3])))
        elif kw == "vt" and len(toks) >= 3:
            # OBJ Y-down convention: V=0 is top in most exports already, but
            # Blender flips it. We store the raw value; the KMD writer maps
            # 0..1 → 0..255 unsigned. Caller-facing Y orientation is handled
            # in the writer if needed.
            mesh.uvs.append((float(toks[1]), float(toks[2])))
        elif kw == "mtllib" and len(toks) >= 2:
            mtl_path = path.parent / " ".join(toks[1:])
            mesh.materials.update(parse_mtl(mtl_path))
        elif kw == "usemtl" and len(toks) >= 2:
            cur_mat = toks[1]
        elif kw == "l" and len(toks) >= 3:
            # OBJ line primitive: `l v1 v2 [v3 ...]` is a polyline. Emit
            # one segment per consecutive vertex pair. Tokens may carry a
            # `v/vt` form too — we only need the position index.
            idxs = []
            for t in toks[1:]:
                pi = int(t.split("/")[0]) - 1
                idxs.append(pi)
            for i in range(len(idxs) - 1):
                mesh.lines.append((idxs[i], idxs[i + 1]))
        elif kw == "f" and len(toks) >= 4:
            verts = [_parse_face_vert(t) for t in toks[1:]]
            # Keep the original n-gon (consumers like hzd_writer want to
            # emit a real quad rather than two degenerate triangles).
            mesh.poly_faces.append(ObjFace(verts=list(verts), material=cur_mat))
            # Fan-triangulate any polygon with >3 verts for consumers that
            # want triangles (kmd_writer).
            for i in range(1, len(verts) - 1):
                mesh.faces.append(ObjFace(
                    verts=[verts[0], verts[i], verts[i + 1]],
                    material=cur_mat,
                ))
    return mesh
