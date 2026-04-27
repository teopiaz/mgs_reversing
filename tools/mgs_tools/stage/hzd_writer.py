"""Convert a collision OBJ into a PSX HZD blob.

Reference: port/libhzd/hzd_loader.c (HZD_MAP_RAW + HZD_GRP_RAW packed
24-byte structs at the file head, then offsets into the same buffer for
the floor/wall arrays).

Layout this writer emits:
  HZD_MAP_RAW (28 B)
  HZD_GRP_RAW (24 B)            ← single group
  HZD_SEG[W]  (each = 2 × HZD_VEC = 2 × 8 B = 16 B)
  walls_flags (W bytes, padded to 4)
  HZD_FLR[F]  (each = 6 × HZD_VEC = 6 × 8 B = 48 B)
  HZD_TRG[T]  (each = 32 B; traps come first then cameras)

HZD_VEC is `{short x, z, y, h}` — note the **z/y swap** vs. PSX SVECTOR
(see port/editor/ed_hzd.c:14 where the editor reads the same layout).

Floors come from collision OBJ triangles. Walls come from collision OBJ
`l` line primitives — each segment is a perfectly vertical wall (PSX
HZD walls are always vertical, so the y component is just the floor
level for visualization purposes).

Trigger volumes (HZD_TRP) are authored as named OBJ groups whose name
starts with `trap_`. Each `o trap_<name>` cube becomes one HZD_TRP with
b1/b2 = AABB of the cube's vertices and `name = <name>` (truncated to
12 characters; the engine's HZD_BIND wires the name to a GCL handler).
This mirrors the floor / wall convention (`o wall_*` already routes to
HZD_SEG) so a single collision OBJ describes the full hazard map.

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


def is_wall_object(name: str | None) -> bool:
    """True if a face's `o name` marks it as a collision wall.
    Convention: any object whose name starts with `wall` (case-
    insensitive, optionally followed by `_`/digits/etc.) becomes
    HZD_SEG segments; everything else is treated as a floor."""
    if not name:
        return False
    return name.lower().startswith("wall")


def is_trap_object(name: str | None) -> bool:
    """True if a face's `o name` marks it as a trigger volume.
    Convention: any object whose name starts with `trap_`
    (case-insensitive) becomes one HZD_TRP record; the part after
    the prefix is the trap name (truncated to 12 chars)."""
    if not name:
        return False
    return name.lower().startswith("trap_") or name.lower() == "trap"


def trap_name_from_object(name: str) -> str:
    """Strip the `trap_` prefix and clamp to the 12-byte HZD_TRP name
    field. Empty source name → "trap" (always-fire bind). Lowercase
    so HZD_BIND lookups in GCL stay convention-consistent (engine
    name_id uses GV_StrCode which is case-sensitive)."""
    s = name
    low = s.lower()
    if low.startswith("trap_"):
        s = s[5:]
    return (s[:12] or "trap")


def is_camera_object(name: str | None) -> bool:
    """True if a face's `o name` marks it as a camera-zone trigger.
    Convention: any object whose name starts with `cam_`. Inside the
    HZD_TRG array, cameras follow the traps and are flagged by
    `id2 == 0xFF`; the writer encodes that via orient.y's high byte
    so the runtime's HZD_ProcessTraps loop terminates correctly."""
    if not name:
        return False
    return name.lower().startswith("cam_") or name.lower() == "cam"


def _face_to_wall_segment(face_verts_xyz):
    """Convert a single face's vertices to (p1, p2, height) for HZD_SEG.

    Walls in Blender are authored as vertical quads: two verts on the
    floor (smallest |Y|, since Y=0 is the floor plane) and two at wall
    height. We pick the bottom edge (the two verts closest to Y=0,
    keeping their order in the face) as the segment endpoints, and use
    the vertical extent of the face as the height.

    Returns (p1, p2, h_pixels) or None if the face is degenerate (fewer
    than 3 verts, the bottom edge collapses to a point, or the face is
    horizontal — common for ceiling/roof quads that share a `wall_*`
    object with the surrounding walls; we silently skip those rather
    than emit fake floor-height walls.
    """
    if len(face_verts_xyz) < 3:
        return None
    # Vertical extent — face must span some Y range to be a wall.
    ys = [v[1] for v in face_verts_xyz]
    height = max(ys) - min(ys)
    # Threshold of 1 PSX unit (= 0.01 OBJ unit at scale=100) — below
    # that the face is effectively horizontal and not a wall.
    if height < 1:
        return None
    # Sort verts by |y| ascending — first two are "on the floor".
    by_y = sorted(face_verts_xyz, key=lambda v: abs(v[1]))
    bottom = by_y[:2]
    if bottom[0][0] == bottom[1][0] and bottom[0][2] == bottom[1][2]:
        return None  # degenerate (both bottom verts coincide in xz)
    return bottom[0], bottom[1], int(round(height))


def _scaled_vert(pos, scale):
    x, y, z = pos
    sx = max(-32768, min(32767, int(round(x * scale))))
    sy = max(-32768, min(32767, int(round(y * scale))))
    sz = max(-32768, min(32767, int(round(z * scale))))
    return sx, sy, sz


def write_hzd(collision: ObjMesh, scale: float = 100.0) -> bytes:
    """Encode the collision mesh as an HZD blob with one floor per
    triangle, one wall per OBJ `l` segment, and one trigger volume per
    `o trap_*` group. Returns raw bytes ready for the DAR archive."""
    floors = []
    walls = []
    triggers = []
    # Per-name AABB accumulators so multiple faces of the same volume
    # merge into a single record. Trap and camera groups go into
    # separate dicts because the engine layout puts traps first then
    # cameras (id2==0xFF marks where cameras begin).
    trap_aabb = {}    # trap-name → [min_x, min_y, min_z, max_x, max_y, max_z]
    cam_aabb  = {}    # cam-name  → same
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
    #
    # Object-name routing: faces inside an `o wall_*` block become
    # HZD_SEG walls (bottom edge of each face → segment, vertical extent
    # → wall height). Everything else is treated as floors.
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

        if is_wall_object(face.object):
            # Wall face → bottom edge becomes one HZD_SEG.
            seg_data = _face_to_wall_segment(all_p)
            if seg_data is None:
                continue
            p1, p2, h = seg_data
            seg = bytearray()
            seg += _vec(p1[0], p1[1], p1[2], h)
            seg += _vec(p2[0], p2[1], p2[2], h)
            walls.append(bytes(seg))
            continue

        if is_trap_object(face.object) or is_camera_object(face.object):
            # Trap / camera face → expand the running per-name AABB.
            # Multiple faces under the same `o trap_<name>` (or
            # `o cam_<name>`) group merge into one trigger record.
            is_cam = is_camera_object(face.object)
            target = cam_aabb if is_cam else trap_aabb
            tname  = (face.object[4:] if is_cam and face.object.lower().startswith("cam_")
                      else trap_name_from_object(face.object))
            tname  = tname[:12] or ("camera" if is_cam else "trap")
            box = target.get(tname)
            for (x, y, z) in all_p:
                if box is None:
                    box = [x, y, z, x, y, z]
                else:
                    if x < box[0]: box[0] = x
                    if y < box[1]: box[1] = y
                    if z < box[2]: box[2] = z
                    if x > box[3]: box[3] = x
                    if y > box[4]: box[4] = y
                    if z > box[5]: box[5] = z
            target[tname] = box
            continue

        # Floor face — emit quads for every (v0, vi, vi+1, vi+2) fan
        # slice; if a slice has only three verts left at the end, emit
        # it as a triangle.
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

    # Materialise the accumulated per-name trigger AABBs. Engine
    # convention (source/libhzd/hzdd.c HZD_GetMap):
    #   triggers[0..n_traps-1]   = HZD_TRP (id2 != 0xFF)
    #   triggers[n_traps..]      = HZD_CAM (id2 == 0xFF)
    # In a HZD_CAM record there's no `id2` field — the byte at offset
    # 29 is the high byte of orient.y. We force it to 0xFF by packing
    # orient.y = -1, which keeps orient.y nonzero (so the editor's
    # heuristic still recognises the record as a camera) and signals
    # "camera record" to the engine's process-traps loop.
    for tname in sorted(trap_aabb):
        bx0, by0, bz0, bx1, by1, bz1 = trap_aabb[tname]
        rec = bytearray()
        rec += _vec(bx0, by0, bz0)            # b1
        rec += _vec(bx1, by1, bz1)            # b2
        # name[12]: ASCII, NUL-padded. The runtime computes name_id from
        # this in HZD_ProcessTraps, so we just leave name_id zero here.
        nb = tname.encode("ascii", errors="replace")[:12]
        rec += nb + b"\x00" * (12 - len(nb))
        rec += struct.pack("<BB", 0, 0)       # id1, id2 (=0 → trap)
        rec += struct.pack("<H", 0)           # name_id (engine recomputes)
        triggers.append(bytes(rec))

    for cname in sorted(cam_aabb):
        bx0, by0, bz0, bx1, by1, bz1 = cam_aabb[cname]
        cx, cy, cz = (bx0 + bx1) // 2, (by0 + by1) // 2, (bz0 + bz1) // 2
        rec = bytearray()
        rec += _vec(bx0, by0, bz0)                # b1
        rec += _vec(bx1, by1, bz1)                # b2
        rec += _vec(cx, cy, cz, 0)                # cam (position; defaults to AABB centre)
        # orient: (0, 0, 0, 0) but orient.y forced to -1 (= 0xFFFF) so
        # byte 29 (id2 in the trap view) is 0xFF — the loader's camera
        # marker. The editor's trap_looks_like_camera() heuristic
        # checks (cam != b1) AND (orient nonzero), both of which hold.
        rec += struct.pack("<hhhh", 0, 0, -1, 0)  # orient.x, .z, .y, .h
        triggers.append(bytes(rec))

    if not floors and not walls and not triggers:
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

    # Compute byte offsets. Layout is walls → walls_flags → floors →
    # triggers. Order isn't load-bearing (offsets are explicit) but
    # matching disc-layout conventions makes the binary easier to
    # diff-eyeball when troubleshooting.
    map_off         = 0
    grp_off         = map_off + HZD_MAP_RAW_SIZE
    walls_off       = grp_off + HZD_GRP_RAW_SIZE
    walls_flags_off = walls_off + len(walls) * HZD_SEG_SIZE
    floors_off      = walls_flags_off + len(walls_flags_padded)
    triggers_off    = floors_off + len(floors) * HZD_FLR_SIZE

    # HZD_MAP_RAW (28 B).
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
    out += struct.pack("<h", len(triggers))     # n_triggers
    out += struct.pack("<h", len(walls))        # n_walls
    out += struct.pack("<h", len(floors))       # n_floors
    out += struct.pack("<h", 0)                 # n_flat_walls
    out += struct.pack("<I", walls_off if walls else 0)
    out += struct.pack("<I", floors_off if floors else 0)
    out += struct.pack("<I", triggers_off if triggers else 0)
    out += struct.pack("<I", walls_flags_off if walls else 0)

    # HZD_SEG array (walls).
    for w in walls:
        out += w
    # HZD wallsFlags array (one byte per wall, padded to 4 bytes).
    out += walls_flags_padded
    # HZD_FLR array (floors).
    for f in floors:
        out += f
    # HZD_TRG array (triggers — traps, plus cameras once authored).
    for t in triggers:
        out += t

    return bytes(out)
