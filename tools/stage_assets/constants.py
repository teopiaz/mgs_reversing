"""Shared constants and small helpers for the stage authoring pipeline.

Hash function and DAR/DATACNF byte layouts mirror the runtime defaults
the engine expects. Keep these in sync with port/libfs/libfs.c and
port/libdg/kmd_loader.c — comments next to each constant point at the
file the value comes from.
"""

# PSX VRAM is 1024×512 16-bit pixels.
VRAM_W = 1024
VRAM_H = 512

# Sector size used by FS_LoadStageRequest (port/libfs/libfs.c:303).
SECTOR_SIZE = 2048

# Default VRAM placement for the diffuse texture + CLUT.
#
# PSX VRAM layout (1024×512 16-bit pixels):
#     y=0..223       : framebuffer (320×224 game render target)
#     y=224..255     : unused band — safe for CLUT
#     y=256..511     : texture pages
#
# An 8-bit 256×256 texture at (px=0, py=256) packs into VRAM columns 0..127
# (since 256 8bpp pixels fit in 128 16-bit words) across rows 256..511.
#
# Putting the CLUT at y=480 (where we had it before) overlaps the bottom
# 32 rows of the texture and corrupts the palette — every face then
# renders as one solid colour. Place the CLUT in the y=224..255 band so
# it never collides with either the framebuffer or texture pages.
DEFAULT_TEXTURE_PX = 0
DEFAULT_TEXTURE_PY = 256
# CLUT row in the gap band y=224..255. The live game loads its
# own font/menu CLUTs at y=240, so a stage's CLUTs there get
# clobbered (textures load fine in VRAM but every face renders
# black or with wrong colours). Park ours one row below at 241
# — far enough not to conflict with the engine's own font data
# yet still inside the safe gap. Bump if a future stage clashes.
DEFAULT_CLUT_CX    = 0
DEFAULT_CLUT_CY    = 241
DEFAULT_TEXTURE_W  = 256
DEFAULT_TEXTURE_H  = 256


# Multiple 256x256 8bpp textures pack across the y=256..511 page region.
# Each one occupies 128 16-bit columns (256 8bpp pixels = 128 16-bit
# words). 1024-wide VRAM ÷ 128 = 8 horizontal slots.
#
# CLUT layout: a 256-color CLUT is 256 16-bit pixels wide × 1 row tall.
# Striding adjacent CLUTs by only 16 cx units (the older layout) made
# every slot >= 1 overlap slot 0 by 240 pixels — so PCX uploads
# clobbered each other and only slot 0 rendered correctly. We instead
# give each CLUT its own row inside the y=224..255 safe band, starting
# at cy=241 (the live game uses cy=240 for its font/menu CLUT).
TEXTURE_SLOT_STRIDE_PX = 128       # 16-bit-pixel columns per 256x256 8bpp slot
CLUT_SLOT_STRIDE_CY    = 1         # cy step per CLUT (one row per slot)
MAX_TEXTURE_SLOTS      = 8         # 1024 / 128 horizontal; 14 vertical CLUT rows
                                   # available (y=241..254) — plenty.


def texture_slot(idx: int) -> tuple[int, int, int, int]:
    """Return (px, py, cx, cy) for the idx-th 256x256 8bpp texture slot.

    Slot 0 matches the legacy single-texture defaults so existing
    single-PCX stages re-import to the same VRAM coords."""
    if idx < 0 or idx >= MAX_TEXTURE_SLOTS:
        raise ValueError(f"texture slot {idx} out of range 0..{MAX_TEXTURE_SLOTS - 1}")
    px = DEFAULT_TEXTURE_PX + idx * TEXTURE_SLOT_STRIDE_PX
    py = DEFAULT_TEXTURE_PY
    cx = DEFAULT_CLUT_CX
    cy = DEFAULT_CLUT_CY + idx * CLUT_SLOT_STRIDE_CY
    return (px, py, cx, cy)

# DG_MODEL_FLAGS — bit set in KMD_MDL_RAW.flags. From source/libdg/libdg.h.
DG_MODEL_TRANS    = 0x00002
DG_MODEL_UNLIT    = 0x00004
DG_MODEL_BOTHFACE = 0x00400


def gv_strcode(s: str) -> int:
    """Replicates source/libgv/strcode.c GV_StrCode(): 16-bit rol-5 + add."""
    h = 0
    for b in s.encode("utf-8"):
        h = ((h << 5) | (h >> 11)) & 0xFFFF
        h = (h + b) & 0xFFFF
    return h


def pad_to(buf: bytearray, multiple: int) -> bytearray:
    """Pad `buf` (in place) with zeros until its length is a multiple of N."""
    rem = len(buf) % multiple
    if rem:
        buf.extend(b"\x00" * (multiple - rem))
    return buf
