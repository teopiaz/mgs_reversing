"""GCX bytecode buffer — big-endian read/write primitives.

Adapted from mgs_compilation_tools by vberthiaume / lab313ru lineage
(see README.md).
"""
import struct
import sys


class GcxData(bytearray):
    """GCX bytecode buffer with a cursor and BE read/write helpers."""

    offset: int

    def __init__(self, *arg, **kw):
        self.offset = 0
        if len(arg) > 0 and isinstance(arg[0], str):
            super().__init__()
            self.load_gcx_file(arg[0])
        else:
            super().__init__(*arg, **kw)

    def load_gcx_file(self, gcx_file):
        try:
            with open(gcx_file, "rb") as f:
                self[:] = f.read()
        except OSError as err:
            print("Error reading gcx file:", err, file=sys.stderr)
            sys.exit(1)

    # -- read -----------------------------------------------------------------

    def read_byte(self, offset=None):
        if offset is not None:
            return self[offset]
        v = self[self.offset]
        self.offset += 1
        return v

    def read_short(self, offset=None):
        if offset is not None:
            return struct.unpack_from(">H", self, offset)[0]
        v = struct.unpack_from(">H", self, self.offset)[0]
        self.offset += 2
        return v

    def read_int(self, offset=None):
        if offset is not None:
            return struct.unpack_from(">I", self, offset)[0]
        v = struct.unpack_from(">I", self, self.offset)[0]
        self.offset += 4
        return v

    def read_string(self, length=0):
        """NUL-terminated MGS text.

        - Byte < 0x80  → single ASCII char (one byte per char)
        - Byte >= 0x80 → part of a 2-byte big-endian glyph code

        2-byte codes render via the mgs_chars table when known; otherwise
        as `{XXXX}` literal markers. Round-trip is always lossless.
        Literal '"', '\\', '{' bytes escape as \\xNN.
        """
        from .glyphs.chars import CODE_TO_CHAR

        chars = []
        length = 0xFF
        remaining = length - 1
        while remaining > 0 and self.read_byte(self.offset) != 0:
            b = self.read_byte()
            if b < 0x80:
                # Single-byte ASCII. Escape chars that can't sit raw in
                # an m"..." literal.
                if b in (0x22, 0x5C, 0x7B) or b < 0x20 or b >= 0x7F:
                    chars.append("\\x%02X" % b)
                else:
                    chars.append(chr(b))
                remaining -= 1
                continue
            # 2-byte code. Consume the trailing byte regardless of table hit.
            if self.read_byte(self.offset) == 0:
                # Truncated pair — keep the head byte as an escape.
                chars.append("\\x%02X" % b)
                remaining -= 1
                continue
            nb = self.read_byte()
            code = (b << 8) | nb
            ch = CODE_TO_CHAR.get(code)
            if ch is not None:
                chars.append(ch)
            else:
                chars.append("{%04X}" % code)
            remaining -= 2
        self.read_byte()  # consume NUL
        return "".join(chars)

    def read_hex_string(self, length):
        return "".join("%02x" % self.read_byte() for _ in range(length))

    # -- write (kept for future compiler) ------------------------------------

    def push_byte(self, value):
        self.append(value & 0xFF)

    def push_short(self, value):
        self.extend(value.to_bytes(length=2, byteorder="big"))

    def push_int(self, value):
        self.extend(value.to_bytes(length=4, byteorder="big"))


class GclNode(dict):
    """GCL AST node — single-key dict of {TYPE: value}."""

    def get_pair(self):
        node_type = next(iter(self))
        return node_type, self[node_type]

    def browse(self, callback, node=None):
        if node is None:
            node = self
        if "PROC_DATA" in self:
            return self.browse(callback, node["PROC_DATA"])
        if isinstance(node, list):
            for child in node:
                self.browse(callback, child)
        elif isinstance(node, GclNode):
            t, v = node.get_pair()
            callback(t, v)
            if isinstance(v, (GclNode, list)):
                self.browse(callback, v)
