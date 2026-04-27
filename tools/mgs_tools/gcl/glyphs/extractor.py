"""Extract and render glyph bitmaps from MGS font data.

Three font banks (see source/font/font.c font_draw_string):

  bank 0 → font.res      (ASCII + common glyphs)
  bank 1 → unused in this codebase
  bank 2 → stage trailing blob (per-.gcx font data)

For a 2-byte MGS code, `glyph_index_for_code` mirrors
`font_get_glyph_index_80044FF4`. The high nibble of the returned index
is the bank; the low 12 bits are the 1-indexed glyph number within the
bank (so byte offset = `(index - 1) * 36`).

Glyphs are 12×12 pixels, 2 bits per pixel, packed MSB-first inside each
byte. 12 rows × 3 bytes per row = 36 bytes.
"""

GLYPH_BYTES = 36
GLYPH_SIZE = 12

# Greyscale characters indexed by 2-bit pixel value.
_SHADE = ["  ", "░░", "▓▓", "██"]


def glyph_index_for_code(code: int) -> int:
    """Inverse-free port of font.c:font_get_glyph_index_80044FF4."""
    code &= ~0x6000                                       # strip display flags
    if code < 0x8200:
        v = code - 0x8101
    elif code < 0x8300:
        v = code - 0x81AE
    elif code < 0x9600:
        v = (code - 0x8F71 - (code // 256)) + 0xA9
    elif code < 0x9A00:
        v = ((code - 0x956B) - (code // 256)) | 0x1000
    else:
        hi = (code - 0x9A00) // 512
        lo = (code - 0x9A00) - (hi * 512)
        bank = hi + 2
        v = (lo - 1 - (lo // 256)) | (bank * 0x1000)
    return v + 1


def bank_and_offset(code: int) -> tuple[int, int]:
    """Return (bank_number, byte_offset_within_bank) for a 2-byte code."""
    idx = glyph_index_for_code(code)
    bank = idx >> 12
    glyph_no = (idx & 0xFFF) - 1
    return bank, glyph_no * GLYPH_BYTES


def strip_font_header(trailing: bytes) -> bytes:
    """Return the glyph-data region of the .gcx trailing blob.

    The runtime (GCL_LoadScript, command.c:165) passes
    `script_body + script_region_len + sizeof(int)` to the font system —
    i.e. it skips one 4-byte length prefix. That prefix holds the glyph-
    data size, and is not part of the pixel data.
    """
    if len(trailing) < 4:
        return b""
    # Optional: validate that the prefix matches the blob length.
    # font_len = struct.unpack('>I', trailing[:4])[0]
    return trailing[4:]


def extract_glyph(bank_blob: bytes, offset: int) -> bytes | None:
    """Return the 36-byte glyph at `offset`, or None if out of range.

    `bank_blob` is expected to already be the pixel-data region; pass the
    result of `strip_font_header` for a stage's trailing bytes.
    """
    if offset < 0 or offset + GLYPH_BYTES > len(bank_blob):
        return None
    return bank_blob[offset:offset + GLYPH_BYTES]


def render_ascii(glyph: bytes) -> str:
    """12-line ASCII-art view. Each pixel = 2 chars for square aspect."""
    rows = []
    for row in range(GLYPH_SIZE):
        line = ""
        # 12 pixels per row = 24 bits = 3 bytes. MSB-first, pairs of 2 bits.
        for byte in glyph[row * 3:(row + 1) * 3]:
            for shift in (6, 4, 2, 0):
                line += _SHADE[(byte >> shift) & 0x3]
        rows.append(line)
    return "\n".join(rows)


def glyph_to_pixels(glyph: bytes) -> list[list[int]]:
    """Return 12×12 grid of 2-bit pixel values (0..3)."""
    rows = []
    for row in range(GLYPH_SIZE):
        r = []
        for byte in glyph[row * 3:(row + 1) * 3]:
            for shift in (6, 4, 2, 0):
                r.append((byte >> shift) & 0x3)
        rows.append(r)
    return rows


def write_sheet_pgm(path: str, glyphs: list[tuple[str, bytes]],
                     cols: int = 24, gutter: int = 2) -> None:
    """Write a P5 PGM grid of all glyphs. Each cell is 12×12 pixels with
    `gutter` pixels of white separator. 2-bit values are expanded to
    0x00 / 0x55 / 0xAA / 0xFF greyscale, inverted so ink is dark."""
    cell = GLYPH_SIZE + gutter
    rows = (len(glyphs) + cols - 1) // cols
    w = cols * cell + gutter
    h = rows * cell + gutter
    # Start with white background
    img = bytearray([0xFF]) * (w * h)
    level = [0xFF, 0xAA, 0x55, 0x00]        # inverted: 0 → white, 3 → black
    for i, (_label, g) in enumerate(glyphs):
        gx = (i % cols) * cell + gutter
        gy = (i // cols) * cell + gutter
        pixels = glyph_to_pixels(g)
        for y in range(GLYPH_SIZE):
            for x in range(GLYPH_SIZE):
                img[(gy + y) * w + (gx + x)] = level[pixels[y][x]]
    with open(path, "wb") as f:
        f.write(f"P5\n{w} {h}\n255\n".encode("ascii"))
        f.write(bytes(img))


def collect_codes_from_string(payload: str) -> list[int]:
    """Walk an m"..."-style payload and yield each 2-byte code.

    Supports `{XXXX}` markers, `\\xNN` escapes (for half of a pair — rare),
    table-mapped chars, and plain ASCII (skipped).
    """
    from .chars import CHAR_TO_CODE

    codes: list[int] = []
    i = 0
    pending_hi: int | None = None
    while i < len(payload):
        c = payload[i]
        if c == "\\" and i + 3 < len(payload) and payload[i + 1] == "x":
            b = int(payload[i + 2:i + 4], 16)
            if b >= 0x80 and pending_hi is None:
                pending_hi = b
            elif pending_hi is not None:
                codes.append((pending_hi << 8) | b)
                pending_hi = None
            i += 4
            continue
        if c == "{" and i + 5 < len(payload) and payload[i + 5] == "}":
            codes.append(int(payload[i + 1:i + 5], 16))
            i += 6
            continue
        if c in CHAR_TO_CODE:
            codes.append(CHAR_TO_CODE[c])
            i += 1
            continue
        # ASCII — ignored for glyph purposes.
        i += 1
    return codes
