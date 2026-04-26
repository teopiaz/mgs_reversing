"""Convert a collision OBJ into a PSX HZD blob.

Reference: port/libhzd/hzd_loader.c (HZD_MAP_RAW + HZD_GRP_RAW packed
24-byte structs at the file head, then offsets into the same buffer for
the floor/wall arrays).

Layout this writer emits:
  HZD_MAP_RAW (24 B)
  HZD_GRP_RAW (24 B)            ← single group, n_walls=W, n_floors=F
  HZD_SEG[W]  (each = 2 × HZD_VEC = 2 × 8 B = 16 B)
  HZD_FLR[F]  (each = 6 × HZD_VEC = 6 × 8 B = 48 B)

HZD_VEC is `{short x, z, y, h}` — note the **z/y swap** vs. PSX SVECTOR
(see port/editor/ed_hzd.c:14 where the editor reads the same layout).

Floors come from collision OBJ triangles. Walls come from collision OBJ
`l` line primitives — each segment is a perfectly vertical wall (PSX
HZD walls are always vertical, so the y component is just the floor
level for visualization purposes).

For each floor triangle (p0, p1, p2) we emit one HZD_FLR with:
  b1 = b2 = bbox of the triangle in HZD coords (used for broad-phase)
  p1, p2, p3, p4 = (p0, p1, p2, p2)   ← degenerate quad
"""

import struct
from pathlib import Path

from .obj_reader import ObjMesh
from .constants import gv_strcode  # noqa: F401  (re-exported for callers)


# HZD_MAP_RAW packs to 28 bytes under #pragma pack(1):
#   h version, hh min_xy, hh max_xy, h n_groups, h n_zones, h n_routes,
#   I groups_off, I zones_off, I routes_off  →  2+4+4+2+2+2+4+4+4 = 28.
# HZD_GRP_RAW is 24 bytes (4 shorts + 4 uint32). See hzd_loader.c.
HZD_MAP_RAW_SIZE = 28
HZD_GRP_RAW_SIZE = 24
HZD_VEC_SIZE     = 8
HZD_SEG_SIZE     = 2 * HZD_VEC_SIZE
HZD_FLR_SIZE     = 6 * HZD_VEC_SIZE


def _vec(x: int, y: int, z: int, h: int = 0) -> bytes:
    """Pack one HZD_VEC. Field order is (x, z, y, h) per fmt_hzd.h.
    For wall endpoints, `h` is the wall-height delta — collide.c's
    PointTestSegment_inline reads it to bound the wall vertically; with
    h=0 the wall has zero height and never blocks anything."""
    return struct.pack("<hhhh", x, z, y, h)


# Default wall height (PSX units). Any actor whose vertical position is
# in [wall.y, wall.y + WALL_DEFAULT_HEIGHT] gets blocked. 3000 covers
# crouch/stand/jump ranges around the floor; bump if a stage has very
# tall walls or vertical actors.
WALL_DEFAULT_HEIGHT = 3000


def _scaled_vert(pos, scale):
    x, y, z = pos
    sx = max(-32768, min(32767, int(round(x * scale))))
    sy = max(-32768, min(32767, int(round(y * scale))))
    sz = max(-32768, min(32767, int(round(z * scale))))
    return sx, sy, sz


def write_hzd(collision: ObjMesh, scale: float = 100.0) -> bytes:
    """Encode the collision mesh as an HZD blob with one floor per
    triangle and one wall per OBJ `l` segment. Returns raw bytes ready
    for the DAR archive."""
    floors = []
    walls = []
    min_x = min_y = min_z =  1 << 30
    max_x = max_y = max_z = -(1 << 30)

    def track_bounds(sx, sy, sz):
        nonlocal min_x, min_y, min_z, max_x, max_y, max_z
        if sx < min_x: min_x = sx
        if sy < min_y: min_y = sy
        if sz < min_z: min_z = sz
        if sx > max_x: max_x = sx
        if sy > max_y: max_y = sy
        if sz > max_z: max_z = sz

    # Walk the original n-gon faces (not the fan-triangulated copies) so
    # an OBJ quad emits a single HZD_FLR with all four corners instead of
    # two degenerate triangles. Polygons with >4 verts are fan-split into
    # quads here — quad pieces use real p4, triangle pieces use p4=p3.
    poly_faces = collision.poly_faces if collision.poly_faces else collision.faces
    for face in poly_faces:
        n = len(face.verts)
        if n < 3:
            continue
        # Convert face vertices to scaled HZD coords up-front.
        all_p = []
        for (pi, _) in face.verts:
            sx, sy, sz = _scaled_vert(collision.positions[pi], scale)
            track_bounds(sx, sy, sz)
            all_p.append((sx, sy, sz))
        # Emit quads for every (v0, vi, vi+1, vi+2) fan slice; if a slice
        # has only three verts left at the end, emit it as a triangle.
        i = 1
        while i < n - 1:
            slice_verts = [all_p[0], all_p[i], all_p[i + 1]]
            if i + 2 < n:
                slice_verts.append(all_p[i + 2])
                step = 2
            else:
                step = 1
            bx0 = min(v[0] for v in slice_verts); bx1 = max(v[0] for v in slice_verts)
            by0 = min(v[1] for v in slice_verts); by1 = max(v[1] for v in slice_verts)
            bz0 = min(v[2] for v in slice_verts); bz1 = max(v[2] for v in slice_verts)
            floor = bytearray()
            floor += _vec(bx0, by0, bz0)        # b1
            floor += _vec(bx1, by1, bz1)        # b2
            floor += _vec(*slice_verts[0])      # p1
            floor += _vec(*slice_verts[1])      # p2
            floor += _vec(*slice_verts[2])      # p3
            if len(slice_verts) == 4:
                floor += _vec(*slice_verts[3])  # p4 (real)
            else:
                floor += _vec(*slice_verts[2])  # p4 (degenerate)
            floors.append(bytes(floor))
            i += step

    for (i0, i1) in collision.lines:
        if i0 < 0 or i1 < 0:
            continue
        if i0 >= len(collision.positions) or i1 >= len(collision.positions):
            continue
        sx0, sy0, sz0 = _scaled_vert(collision.positions[i0], scale)
        sx1, sy1, sz1 = _scaled_vert(collision.positions[i1], scale)
        track_bounds(sx0, sy0, sz0)
        track_bounds(sx1, sy1, sz1)
        seg = bytearray()
        # Endpoint y is the wall TOP, h is the wall-height delta (live
        # game uses both to bound the wall vertically). Floor is at y=0
        # in our authored stages; walls extend WALL_DEFAULT_HEIGHT
        # downward in PSX +Y-down terms, which covers Snake's stance
        # range at the floor.
        seg += _vec(sx0, sy0, sz0, WALL_DEFAULT_HEIGHT)   # p1
        seg += _vec(sx1, sy1, sz1, WALL_DEFAULT_HEIGHT)   # p2
        walls.append(bytes(seg))

    if not floors and not walls:
        # Empty collision still needs a valid map. Use a tiny dummy.
        min_x = min_y = min_z = 0
        max_x = max_y = max_z = 0

    # Per-wall flags array (one byte per wall). HZD_PointCheck reads
    # these as `*pFlags & exclude` to skip walls in certain modes — if
    # this pointer is NULL the live game NULL-derefs in CheckNear during
    # the very first sna_act tick. We emit zeros (no special flags).
    # Pad to 4-byte alignment so the floor offset that follows stays
    # aligned for the int16 fields inside HZD_FLR.
    walls_flags_blob = bytes(len(walls)) if walls else b""
    walls_flags_padded = walls_flags_blob + bytes((-len(walls_flags_blob)) & 3)

    # Compute byte offsets. Walls come before floors so the HZD_SEG
    # array sits closer to the header — matches the layout shipped on
    # disc, though the offsets are explicit so order isn't load-bearing.
    map_off    = 0
    grp_off    = map_off + HZD_MAP_RAW_SIZE
    walls_off  = grp_off + HZD_GRP_RAW_SIZE
    walls_flags_off = walls_off + len(walls) * HZD_SEG_SIZE
    floors_off = walls_flags_off + len(walls_flags_padded)

    # HZD_MAP_RAW (24 B).
    out = bytearray()
    out += struct.pack("<h", 2)                 # version (>=2 to skip the loader's warning)
    out += struct.pack("<hh", min_x, min_y)
    out += struct.pack("<hh", max_x, max_y)
    out += struct.pack("<h", 1)                 # n_groups
    out += struct.pack("<h", 0)                 # n_zones
    out += struct.pack("<h", 0)                 # n_routes
    out += struct.pack("<I", grp_off)           # groups_off
    out += struct.pack("<I", 0)                 # zones_off
    out += struct.pack("<I", 0)                 # routes_off

    # HZD_GRP_RAW (24 B).
    out += struct.pack("<h", 0)                 # n_triggers
    out += struct.pack("<h", len(walls))        # n_walls
    out += struct.pack("<h", len(floors))       # n_floors
    out += struct.pack("<h", 0)                 # n_flat_walls
    out += struct.pack("<I", walls_off if walls else 0)
    out += struct.pack("<I", floors_off if floors else 0)
    out += struct.pack("<I", 0)                 # triggers_off
    out += struct.pack("<I", walls_flags_off if walls else 0)

    # HZD_SEG array (walls).
    for w in walls:
        out += w
    # HZD wallsFlags array (one byte per wall, padded to 4 bytes).
    out += walls_flags_padded
    # HZD_FLR array (floors).
    for f in floors:
        out += f

    return bytes(out)
