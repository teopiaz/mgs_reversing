"""Assemble the four-region DATACNF blob the engine's stage loader expects.

Reference: source/libfs/datacnf.h, port/libfs/libfs.c (FS_LoadStageRequest).

The stage buffer shape is:

  Sector 0:   DATACNF header + tag table
  Sector 1+:  per-tag regions, each sector-aligned, in the same order
              the tags appear:
                'r' resident DAR (KMD + HZD here)
                'c' cached blobs (scenerio.gcx)
                'n' nocache DAR (PCX texture)

The 's' (sound) region is omitted — the loader treats it as optional.
DATACNF_TAG layout (from datacnf.h):
    u16 id; u8 mode; u8 ext; u32 size

DARFILE_TAG layout (the per-entry header inside an 'r' or 'n' archive):
    u16 id; u8 ext; u8 _pad; u32 size  ; then `size` bytes of payload
"""

import struct

from .constants import SECTOR_SIZE


# ---------- DAR archive helpers --------------------------------------------

def dar_archive(entries: list) -> bytes:
    """Pack [(id:int, ext:str, payload:bytes), ...] as a DAR archive.

    Each entry is a DARFILE_TAG (8 bytes) + payload, no padding between.
    The outer DATACNF tag stores the total size."""
    out = bytearray()
    for (id_, ext, payload) in entries:
        out += struct.pack("<HBB", id_ & 0xFFFF, ord(ext), 0)
        out += struct.pack("<I", len(payload))
        out += payload
    return bytes(out)


# ---------- DATACNF blob ----------------------------------------------------

def _pad_to_sector(buf: bytearray) -> int:
    """Pad to the next 2048-byte boundary; return the byte count added."""
    rem = len(buf) % SECTOR_SIZE
    if rem == 0:
        return 0
    pad = SECTOR_SIZE - rem
    buf.extend(b"\x00" * pad)
    return pad


def build_datacnf(*,
                  resident_dar: bytes,
                  cache_blobs: list,    # list of (id:int, ext:str, payload:bytes) for 'c' tags
                  nocache_dar: bytes) -> bytes:
    """Build a complete DATACNF blob ready to write to disk.

    The shape mirrors the live tag walker in port/libfs/libfs.c:

      DATACNF{ version=1, size=<sectors>, tags={
          {mode='r', ext=0,    id=0, size=<resident_size>},     # r region
          {mode='c', ext='g', id=<gcx_id>, size=0},             # c base
          {mode='c', ext=0xFF, id=0, size=<c_total_size>},      # c terminator
          {mode='n', ext=0,    id=0, size=<nocache_size>},      # n region
          {mode=0,   ext=0,    id=0, size=0}                    # end
      }}

    Followed by sector-aligned regions in the same order.
    """
    DATACNF_TAG_SIZE = 8

    # First, lay out the tag table to know its size, so we can place data.
    # Tags: r, c-base, c-terminator, n, terminator. The 'c' base tag's
    # `size` is the offset (0) of scenerio.gcx within the c region; the
    # 'c' terminator's `size` is the total cache region byte count.
    if cache_blobs:
        gcx_id, gcx_ext, gcx_payload = cache_blobs[0]
        c_total = len(gcx_payload)
    else:
        gcx_id, gcx_ext, gcx_payload = 0, 'g', b""
        c_total = 0

    tag_table = bytearray()
    # 'r' tag.
    tag_table += struct.pack("<HBBI", 0, ord('r'), 0, len(resident_dar))
    # 'c' base tag (entry).
    tag_table += struct.pack("<HBBI", gcx_id, ord('c'), ord(gcx_ext), 0)
    # 'c' terminator (mode='c', ext=0xFF). Its `size` is the total c
    # region size in bytes (sector-aligned to a multiple of 4).
    tag_table += struct.pack("<HBBI", 0, ord('c'), 0xFF, (c_total + 3) & ~3)
    # 'n' tag.
    tag_table += struct.pack("<HBBI", 0, ord('n'), 0, len(nocache_dar))
    # End-of-tags marker (mode=0).
    tag_table += struct.pack("<HBBI", 0, 0, 0, 0)

    # Sector 0 = DATACNF header + tag table.
    sector0 = bytearray()
    sector0 += struct.pack("<BB", 1, 0)            # version, padding
    # `size` field — number of sectors the entire stage occupies.
    # We patch it after we know the total length.
    sector0 += struct.pack("<H", 0)
    sector0 += tag_table
    _pad_to_sector(sector0)

    # 'r' region.
    r_bytes = bytearray(resident_dar)
    _pad_to_sector(r_bytes)

    # 'c' region — the cache walker advances `data_ptr` by 4-byte-aligned
    # `(c_total + 3) & ~3`, NOT sector-aligned. Padding to a sector here
    # would shift every region after it relative to where the loader
    # thinks it should be (causing the DAR parser in 'n' to read
    # garbage). Match what libfs.c expects: 4-byte align only.
    c_bytes = bytearray(gcx_payload)
    while len(c_bytes) % 4:
        c_bytes.append(0)

    # 'n' region — sector-padded after.
    n_bytes = bytearray(nocache_dar)
    _pad_to_sector(n_bytes)

    # Concatenate.
    blob = bytearray()
    blob += sector0
    blob += r_bytes
    blob += c_bytes
    blob += n_bytes
    _pad_to_sector(blob)

    # Patch the `size` field with the final sector count.
    n_sectors = len(blob) // SECTOR_SIZE
    struct.pack_into("<H", blob, 2, n_sectors)

    return bytes(blob)
