"""Build the DMO catalogue (port/editor/data/dmo/_index.json).

Each entry in the catalogue is one `demo -f "X.dmo" -s t:NNNN` reference
harvested from the decompiled GCL scripts, paired with the DMO_DEF
header read from DEMO.DAT at that sector. The editor's DMO Inspector
reads this index, then loads per-frame data on demand by reading
DEMO.DAT directly — no per-dmo JSON files.

Usage:

    python3 tools/extract_dmo.py             # rebuild the index
    python3 tools/extract_dmo.py --list      # print refs, no I/O
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path
from typing import NamedTuple, Optional

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))
from mgs_tools.common.iso import IsoImage  # noqa: E402

DEMO_DAT_LBA_DEFAULT = 2820
SECTOR_SIZE = 2048

# DMO_DEF is the first type=5 block in the stream, but may be preceded
# by audio (type=1, ~8KB) and clock blocks. Read 64KB so we always find
# it within the first sector cluster.
HEADER_READ_BYTES = 64 * 1024


class DmoRef(NamedTuple):
    name: str
    sector: int
    gcl_path: str
    proc: Optional[str]


_RE_DEMO_BLOCK = re.compile(r"demo\s+(?:\\?\s*-[a-zA-Z]\s*[^\n]*\n)+", re.M)
_RE_OPT_F      = re.compile(r'-f\s+"([^"]+)"')
_RE_OPT_S      = re.compile(r"-s\s+t:([0-9A-Fa-f]+)")
_RE_OPT_P      = re.compile(r"-p\s+([A-Za-z_][A-Za-z0-9_]*)")


def find_dmo_refs(gcl_root: Path) -> list[DmoRef]:
    """Walk decompiled GCL files; return one DmoRef per (name, sector).

    Skips entries whose sector is the FFFFFFFF sentinel — those are the
    Japanese-only paths the English disc doesn't ship data for."""
    refs: list[DmoRef] = []
    for gcl in sorted(gcl_root.rglob("[ds]*.gcl")):
        text = gcl.read_text(errors="replace")
        for m in _RE_DEMO_BLOCK.finditer(text):
            block = m.group(0)
            f = _RE_OPT_F.search(block)
            s = _RE_OPT_S.search(block)
            p = _RE_OPT_P.search(block)
            if not s or not f:
                continue
            sec = int(s.group(1), 16)
            if sec == 0xFFFFFFFF:
                continue
            refs.append(DmoRef(
                name=f.group(1),
                sector=sec,
                gcl_path=str(gcl.relative_to(gcl_root.parent)),
                proc=p.group(1) if p else None,
            ))
    seen: dict[tuple[str, int], DmoRef] = {}
    for r in refs:
        seen.setdefault((r.name, r.sector), r)
    return list(seen.values())


def read_header_block(iso: IsoImage, demo_lba: int, sec_off: int) -> Optional[dict]:
    """Read the first DMO data block at the given sector and parse its
    DMO_DEF fields. Returns a dict with n_frames / n_maps / n_models, or
    None if no valid block was found."""
    n_sectors = (HEADER_READ_BYTES + SECTOR_SIZE - 1) // SECTOR_SIZE
    raw = bytearray()
    for i in range(n_sectors):
        raw.extend(iso.read_sector(demo_lba + sec_off + i))
    buf = bytes(raw)

    off = 0
    while off + 4 <= len(buf):
        hdr = struct.unpack_from("<I", buf, off)[0]
        t, s = hdr & 0xFF, hdr >> 8
        if t == 0 or s == 0 or s > 0x10000:
            return None
        if t in (0xF0, 0xFF):
            return None
        if t == 0x05 and off + s <= len(buf):
            payload = buf[off:off + s]
            if len(payload) < 28:
                return None
            return {
                "n_frames":  struct.unpack_from("<i", payload, 8)[0],
                "n_maps":    struct.unpack_from("<i", payload, 12)[0],
                "n_models":  struct.unpack_from("<i", payload, 16)[0],
            }
        if off + s > len(buf):
            return None
        off += s
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", default="port/ISO/mgs.bin",
                    help="path to mgs.bin or .iso (default: %(default)s)")
    ap.add_argument("--demo-lba", type=int, default=DEMO_DAT_LBA_DEFAULT,
                    help="DEMO.DAT lba within the ISO (default: %(default)d)")
    ap.add_argument("--gcl-root", default="port/gcl/decompiled",
                    help="root for finding `demo -s` references")
    ap.add_argument("--out", default="port/editor/data/dmo/_index.json",
                    help="output index path")
    ap.add_argument("--list", action="store_true",
                    help="list refs without writing the index")
    args = ap.parse_args()

    refs = find_dmo_refs(Path(args.gcl_root))

    if args.list:
        for r in sorted(refs, key=lambda r: r.sector):
            print(f"  sector=0x{r.sector:08X}  {r.name:24s}  ({r.gcl_path})")
        print(f"\nTotal: {len(refs)} unique .dmo references")
        return 0

    iso_path = Path(args.iso)
    if not iso_path.exists():
        print(f"error: ISO not found at {iso_path}", file=sys.stderr)
        return 1
    iso = IsoImage(iso_path)

    index: list[dict] = []
    for r in refs:
        hdr = read_header_block(iso, args.demo_lba, r.sector)
        if hdr is None:
            print(f"  [skip] {r.name} sector=0x{r.sector:X}: no DMO_DEF found")
            continue
        index.append({
            "name": r.name,
            "sector": r.sector,
            "gcl_path": r.gcl_path,
            "n_frames":  hdr["n_frames"],
            "n_models":  hdr["n_models"],
            "n_maps":    hdr["n_maps"],
        })
        print(f"  [ok]  {r.name:24s} sector=0x{r.sector:08X} "
              f"frames={hdr['n_frames']} models={hdr['n_models']}")

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w") as fp:
        json.dump(sorted(index, key=lambda d: d["sector"]),
                  fp, indent=2)
    print(f"\nWrote {out_path}  ({len(index)} dmos)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
