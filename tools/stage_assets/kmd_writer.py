"""Pack an `ObjMesh` into a PSX KMD binary blob.

Layout reference (port/libdg/kmd_loader.c):
  KMD_DEF_RAW   32 B header (n_visible, n_models, min, max)
  KMD_MDL_RAW   76 B per-model header (flags, n_faces, min, max, pos,
                                       parent, extend, n_verts,
                                       offsets to vertices/vindices/
                                       normals/nindices/texcoords/
                                       materials)
  vertices      n_verts × 8 B  (3 shorts + 2 pad)
  vindices      n_faces × 4 B  (4 × 7-bit indices packed in u32)
  texcoords     n_faces × 8 B  (4 × (u8 u, u8 v) — KMD vertex order 0,1,3,2)
  materials     n_faces × 2 B  (u16 GV_StrCode hash)

A model maxes out at 128 vertices (7-bit indices). We split larger meshes
across multiple models. Triangles are emitted as degenerate quads
(i0, i1, i2, i2) — the renderer handles them.
"""

import struct
from dataclasses import dataclass

from .constants import gv_strcode, DG_MODEL_BOTHFACE
from .obj_reader import ObjMesh


HEADER_SIZE = 32     # KMD_DEF_RAW
MODEL_SIZE  = 88     # KMD_MDL_RAW (the comment in kmd_loader.c says "76"
                     # but real sizeof under #pragma pack(1) is 88: the
                     # struct has min+max+pos as three 12-byte DG_VECTORs,
                     # not two like the comment suggested.)
VERT_BYTES  = 8      # SVECTOR
FACE_VINDICES_BYTES  = 4
FACE_TEXCOORDS_BYTES = 8
FACE_MATERIALS_BYTES = 2

MAX_VERTS_PER_MODEL = 128


@dataclass
class _SubFace:
    """One quad as it'll appear in the KMD: 4 vertex indices + 4 UVs +
    a material id. Triangles arrive as (i0, i1, i2, i2)."""
    idx: tuple    # (i0, i1, i2, i3) into the model's vertex list
    uvs: tuple    # ((u0,v0), (u1,v1), (u2,v2), (u3,v3)) in 0..255
    mat: int      # u16 hash


def _scale_uv(u: float, v: float, flip_v: bool) -> tuple[int, int]:
    """OBJ UVs are float in [0, 1]. Map to PSX 0..255 unsigned bytes.
    OBJ historically uses V=0 at bottom; PSX texture-page V=0 is top, so
    `flip_v=True` (Blender's default export) inverts it. We clamp instead
    of modulo — `1.0 % 1.0` returns 0 in Python, which would collapse
    V=1 onto V=0 and ruin the second half of every texture. """
    uu = max(0.0, min(1.0, u))
    vv = max(0.0, min(1.0, v))
    if flip_v:
        vv = 1.0 - vv
    return (int(round(uu * 255)), int(round(vv * 255)))


def _pack_quad_indices(i0: int, i1: int, i2: int, i3: int) -> int:
    """Pack four 7-bit indices into a little-endian u32. High bits stay 0."""
    for i in (i0, i1, i2, i3):
        if not (0 <= i < 128):
            raise ValueError(f"vertex index {i} out of 7-bit range")
    return (i0 & 0x7F) | ((i1 & 0x7F) << 8) | ((i2 & 0x7F) << 16) | ((i3 & 0x7F) << 24)


def _build_models(mesh: ObjMesh, default_texture: str, flip_v: bool):
    """Group faces into KMD-sized models. Each model gets its own vertex
    list (so a single mesh that exceeds 128 verts splits cleanly).
    Returns: list of (verts: list[(x,y,z)], faces: list[_SubFace])."""
    models = []
    cur_verts: list = []
    cur_uv_dedup: dict = {}
    cur_pos_dedup: dict = {}
    cur_faces: list = []

    def flush():
        if cur_faces:
            models.append((cur_verts.copy(), cur_faces.copy()))
        cur_verts.clear()
        cur_uv_dedup.clear()
        cur_pos_dedup.clear()
        cur_faces.clear()

    def remap_pos(pos_idx: int) -> int:
        if pos_idx in cur_pos_dedup:
            return cur_pos_dedup[pos_idx]
        x, y, z = mesh.positions[pos_idx]
        cur_pos_dedup[pos_idx] = len(cur_verts)
        cur_verts.append((x, y, z))
        return cur_pos_dedup[pos_idx]

    def uv_or_zero(uv_idx: int) -> tuple:
        if uv_idx < 0 or uv_idx >= len(mesh.uvs):
            return (0, 0)
        return _scale_uv(mesh.uvs[uv_idx][0], mesh.uvs[uv_idx][1], flip_v)

    for face in mesh.faces:
        if len(face.verts) != 3:
            # OBJ reader fan-triangulates everything to 3 verts.
            continue
        # Pre-flight: would adding this triangle's three new vertices push
        # the model over 128? If so, flush first.
        new_count = sum(1 for (pi, _) in face.verts if pi not in cur_pos_dedup)
        if len(cur_verts) + new_count > MAX_VERTS_PER_MODEL:
            flush()

        i0 = remap_pos(face.verts[0][0])
        i1 = remap_pos(face.verts[1][0])
        i2 = remap_pos(face.verts[2][0])
        uv0 = uv_or_zero(face.verts[0][1])
        uv1 = uv_or_zero(face.verts[1][1])
        uv2 = uv_or_zero(face.verts[2][1])

        mat_name = face.material if face.material else default_texture
        mat_id   = gv_strcode(mat_name) if mat_name else 0

        # Degenerate quad — the renderer skips zero-area tri 2 cleanly.
        cur_faces.append(_SubFace(
            idx=(i0, i1, i2, i2),
            uvs=(uv0, uv1, uv2, uv2),
            mat=mat_id,
        ))

    flush()
    if not models:
        # Always emit at least one (empty) model so the loader is happy.
        models.append(([(0, 0, 0)], []))
    return models


def _bbox(positions, scale: float) -> tuple:
    """Tight bbox (min, max) over scaled positions, integer-rounded."""
    if not positions:
        return ((0, 0, 0), (0, 0, 0))
    xs = [int(round(p[0] * scale)) for p in positions]
    ys = [int(round(p[1] * scale)) for p in positions]
    zs = [int(round(p[2] * scale)) for p in positions]
    return ((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs)))


def write_kmd(mesh: ObjMesh,
              default_texture: str = "",
              scale: float = 100.0,
              flip_v: bool = True,
              flags: int = 0) -> bytes:
    """Convert `mesh` to a KMD blob.

    `scale` multiplies OBJ coords (which Blender-default to ~metres) into
    PSX world units (stages span ±25000). `flip_v` accounts for Blender's
    bottom-up V convention. `flags` is OR'd into every model's flags;
    DG_MODEL_BOTHFACE is useful while debugging unfamiliar winding.
    """
    sub_models = _build_models(mesh, default_texture, flip_v)

    # Layout pass: compute every offset before writing bytes.
    blob_offset = HEADER_SIZE + MODEL_SIZE * len(sub_models)

    model_layouts = []
    global_min = [ 1 <<  31] * 3
    global_max = [-(1 << 31)] * 3
    for verts, faces in sub_models:
        n_verts = max(len(verts), 1)
        n_faces = len(faces)
        verts_off    = blob_offset
        vindices_off = verts_off + n_verts * VERT_BYTES
        # Skip normals — n_normals=0 means no normal block written.
        texcoords_off = vindices_off + n_faces * FACE_VINDICES_BYTES
        materials_off = texcoords_off + n_faces * FACE_TEXCOORDS_BYTES
        end_off       = materials_off + n_faces * FACE_MATERIALS_BYTES
        blob_offset = end_off

        (mn, mx) = _bbox(verts, scale)
        for k in range(3):
            if mn[k] < global_min[k]: global_min[k] = mn[k]
            if mx[k] > global_max[k]: global_max[k] = mx[k]

        model_layouts.append(dict(
            verts=verts, faces=faces,
            n_verts=n_verts, n_faces=n_faces,
            verts_off=verts_off, vindices_off=vindices_off,
            texcoords_off=texcoords_off, materials_off=materials_off,
            min=mn, max=mx,
        ))

    if not any(m["n_faces"] for m in model_layouts):
        # Empty mesh — give the global bbox sensible zeros.
        global_min = [0, 0, 0]
        global_max = [0, 0, 0]

    out = bytearray()

    # KMD_DEF_RAW header.
    out += struct.pack("<ii", len(sub_models), len(sub_models))
    out += struct.pack("<iii", *global_min)
    out += struct.pack("<iii", *global_max)

    # Per-model headers.
    for m in model_layouts:
        flags_v   = flags
        n_faces_v = m["n_faces"]
        out += struct.pack("<ii", flags_v, n_faces_v)
        out += struct.pack("<iii", *m["min"])
        out += struct.pack("<iii", *m["max"])
        out += struct.pack("<iii", 0, 0, 0)        # pos
        out += struct.pack("<i", -1)                # parent
        out += struct.pack("<i", -1)                # extend
        out += struct.pack("<i", m["n_verts"])
        out += struct.pack("<II", m["verts_off"], m["vindices_off"])
        out += struct.pack("<i", 0)                 # n_normals
        out += struct.pack("<II", 0, 0)             # normals_off, nindices_off
        out += struct.pack("<II", m["texcoords_off"], m["materials_off"])
        out += struct.pack("<i", 0)                 # padding

    # Per-model arrays.
    for m in model_layouts:
        # Vertices.
        for (x, y, z) in m["verts"]:
            sx = int(round(x * scale))
            sy = int(round(y * scale))
            sz = int(round(z * scale))
            # Clamp to int16 to be safe.
            sx = max(-32768, min(32767, sx))
            sy = max(-32768, min(32767, sy))
            sz = max(-32768, min(32767, sz))
            out += struct.pack("<hhhh", sx, sy, sz, 0)
        # Pad if model has zero verts (we forced n_verts>=1).
        for _ in range(m["n_verts"] - len(m["verts"])):
            out += b"\x00" * VERT_BYTES

        # vindices.
        for f in m["faces"]:
            out += struct.pack("<I", _pack_quad_indices(*f.idx))
        # texcoords. KMD vertex order for the four UVs is v0,v1,v3,v2
        # (libdg_stub.c per-face read swaps the third/fourth pair). We
        # have (uv0,uv1,uv2,uv3) where idx3==idx2 for triangles, so the
        # last two entries match either way; for true quads the caller
        # must already supply (uv0,uv1,uv3,uv2) order.
        for f in m["faces"]:
            (u0, v0), (u1, v1), (u2, v2), (u3, v3) = f.uvs
            out += struct.pack("<BBBBBBBB", u0, v0, u1, v1, u3, v3, u2, v2)
        # materials.
        for f in m["faces"]:
            out += struct.pack("<H", f.mat)

    return bytes(out)
