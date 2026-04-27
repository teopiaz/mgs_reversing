"""Minimal ISO-9660 reader for MGS PSX disc images.

Handles the three sector layouts the project encounters in the wild:
  - Mode2/2352 (.bin from common rips), data offset 24
  - Mode1/2352 (.bin alternative),       data offset 16
  - plain ISO  (.iso, no Mode header),   data offset 0

Lifted out of the original tools/extract_disc_assets.py so any tool that
needs to read a disc — the disc-asset extractor today, future "dump
every stage's HZD" / "diff a build's overlays" tools tomorrow — can
share one implementation."""

from pathlib import Path
import struct


ISO_USER_SIZE = 2048


class IsoImage:
    def __init__(self, path: Path):
        self.path = path
        self.fp = open(path, "rb")
        self._detect_format()

    def _detect_format(self):
        # Try Mode2/2352 (data offset 24), Mode1/2352 (offset 16), plain (0).
        for sector_size, data_offset in ((2352, 24), (2352, 16), (2048, 0)):
            self.sector_size = sector_size
            self.data_offset = data_offset
            buf = self.read_sector(16)
            if buf and buf[1:6] == b"CD001":
                return
        raise RuntimeError(f"no ISO-9660 PVD in {self.path}")

    def read_sector(self, lba: int) -> bytes:
        off = lba * self.sector_size + self.data_offset
        self.fp.seek(off)
        return self.fp.read(ISO_USER_SIZE)

    def read_extent(self, lba: int, size: int) -> bytes:
        n = (size + ISO_USER_SIZE - 1) // ISO_USER_SIZE
        out = bytearray()
        for i in range(n):
            out.extend(self.read_sector(lba + i))
        return bytes(out[:size])

    def read_file_at(self, lba: int, byte_offset: int, length: int) -> bytes:
        first = byte_offset // ISO_USER_SIZE
        last = (byte_offset + length - 1) // ISO_USER_SIZE
        chunk = bytearray()
        for i in range(first, last + 1):
            chunk.extend(self.read_sector(lba + i))
        start = byte_offset % ISO_USER_SIZE
        return bytes(chunk[start:start + length])

    def find_file(self, path: str) -> tuple[int, int]:
        """Return (lba, size) for /<path>. ISO names may end with ;1."""
        pvd = self.read_sector(16)
        # Root directory record at PVD offset 156: { ..., +2 lba LE32,
        # +10 size LE32, ... }
        root = pvd[156:156 + 34]
        dir_lba = struct.unpack_from("<I", root, 2)[0]
        dir_size = struct.unpack_from("<I", root, 10)[0]

        components = [c for c in path.replace("\\", "/").split("/") if c]
        for ci, comp in enumerate(components):
            is_last = (ci == len(components) - 1)
            data = self.read_extent(dir_lba, dir_size)
            i = 0
            found = False
            while i < dir_size:
                rec_len = data[i]
                if rec_len == 0:
                    i = ((i // ISO_USER_SIZE) + 1) * ISO_USER_SIZE
                    continue
                f_lba = struct.unpack_from("<I", data, i + 2)[0]
                f_size = struct.unpack_from("<I", data, i + 10)[0]
                flags = data[i + 25]
                name_len = data[i + 32]
                name = bytes(data[i + 33:i + 33 + name_len]).decode("ascii", "replace")
                # Strip ;version and trailing dots.
                if ";" in name:
                    name = name.split(";", 1)[0]
                while name.endswith("."):
                    name = name[:-1]
                is_dir = (flags & 2) != 0
                if name.upper() == comp.upper():
                    if is_last and not is_dir:
                        return f_lba, f_size
                    if not is_last and is_dir:
                        dir_lba, dir_size = f_lba, f_size
                        found = True
                        break
                i += rec_len
            if not found:
                raise FileNotFoundError(f"ISO: {path} not found at component '{comp}'")
        raise FileNotFoundError(path)
