"""Convert a PNG (or any Pillow-readable image) to MGS's custom PCX variant.

Reference layouts:
  source/include/fmt_tex.h         PCXDATA + PCXINFO
  port/libdg/pcx_loader.c          DG_LoadInitPcx (decode side)

Standard PCX header is 128 bytes; the MGS engine repurposes 14 of them
for a `PCXINFO` block at offset 74 (after the standard header fields)
carrying the destination VRAM coords for the texture and CLUT, plus
flags. We only emit 8-bit indexed PCX files.

We use RLE that the loader's `DG_PcxRead8Bpp` accepts: a byte ≥ 0xC0 is a
"run of (byte & 0x3F) of the next byte"; otherwise it's a literal. We
emit literals for values < 0xC0 with run==1 and runs for everything else.
"""

import struct
from pathlib import Path

try:
    from PIL import Image
except ImportError as e:                                 # noqa: F841
    raise SystemExit("import_stage requires Pillow: pip install pillow")

from .constants import (DEFAULT_TEXTURE_PX, DEFAULT_TEXTURE_PY,
                        DEFAULT_CLUT_CX, DEFAULT_CLUT_CY)


def _rle_encode_8bpp(line: bytes) -> bytes:
    """Encode one row as PCX RLE. Output bytes consumable by
    DG_PcxRead8Bpp in port/libdg/pcx_loader.c."""
    out = bytearray()
    i = 0
    n = len(line)
    while i < n:
        b = line[i]
        # Find run length, max 63 (PCX run count fits in 6 bits).
        run = 1
        while i + run < n and line[i + run] == b and run < 63:
            run += 1
        if run > 1 or b >= 0xC0:
            out.append(0xC0 | run)
            out.append(b)
        else:
            out.append(b)
        i += run
    return bytes(out)


def write_pcx(image_path: Path,
              px: int = DEFAULT_TEXTURE_PX,
              py: int = DEFAULT_TEXTURE_PY,
              cx: int = DEFAULT_CLUT_CX,
              cy: int = DEFAULT_CLUT_CY) -> bytes:
    """Read `image_path`, quantize to 8-bit, emit MGS-PCX bytes."""
    img = Image.open(image_path).convert("RGBA")

    # Force 8-bit indexed via Pillow's quantizer (median-cut, 256 colours).
    quant = img.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    palette = quant.getpalette()[:256 * 3]
    if len(palette) < 256 * 3:
        palette += [0] * (256 * 3 - len(palette))

    w, h = quant.size
    pixels = quant.tobytes()  # row-major, one byte per pixel

    # Build the 128-byte header.
    hdr = bytearray(128)
    struct.pack_into("<B", hdr, 0,  0x0A)             # manufacturer
    struct.pack_into("<B", hdr, 1,  5)                # version (PCX 3.0)
    struct.pack_into("<B", hdr, 2,  1)                # encoding (RLE)
    struct.pack_into("<B", hdr, 3,  8)                # bits per pixel per plane
    struct.pack_into("<H", hdr, 4,  1)                # min_x
    struct.pack_into("<H", hdr, 6,  1)                # min_y
    struct.pack_into("<H", hdr, 8,  w)                # max_x (loader does max - min + 1)
    struct.pack_into("<H", hdr, 10, h)                # max_y
    struct.pack_into("<H", hdr, 12, 72)               # dpi_x
    struct.pack_into("<H", hdr, 14, 72)               # dpi_y
    # offsets 16..63 are header_palette (16 * RGB) — unused for 8bpp; leave 0
    struct.pack_into("<B", hdr, 64, 0)                # reserved
    struct.pack_into("<B", hdr, 65, 1)                # n_planes
    struct.pack_into("<H", hdr, 66, w)                # bytes_per_line == width for 8bpp
    struct.pack_into("<H", hdr, 68, 1)                # header_palette_class
    struct.pack_into("<H", hdr, 70, 320)              # screen_width (informational)
    struct.pack_into("<H", hdr, 72, 224)              # screen_height (informational)

    # PCXINFO at offset 74 (12 bytes).
    flags = 1                                         # bit 0 = 8bpp
    n_colors = 256
    struct.pack_into("<H", hdr, 74, 12345)            # magic
    struct.pack_into("<H", hdr, 76, flags)
    struct.pack_into("<H", hdr, 78, px)
    struct.pack_into("<H", hdr, 80, py)
    struct.pack_into("<H", hdr, 82, cx)
    struct.pack_into("<H", hdr, 84, cy)
    struct.pack_into("<H", hdr, 86, n_colors)
    # offsets 88..127 are padding — already zeroed by bytearray(128)

    # RLE-encoded pixel data, row by row.
    body = bytearray()
    for y in range(h):
        row = pixels[y * w:(y + 1) * w]
        body += _rle_encode_8bpp(row)

    # 256-colour palette: 0x0C signal byte then 768 RGB bytes.
    body.append(0x0C)
    body += bytes(palette)

    return bytes(hdr) + bytes(body)
