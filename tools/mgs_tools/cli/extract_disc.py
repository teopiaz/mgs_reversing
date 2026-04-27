#!/usr/bin/env python3
"""Extract bundle-able assets from the MGS disc image.

Custom stages built by `tools/import_stage.py` ship with their own KMD
+ HZD + PCX, but `chara &ITEM` directives reference KMDs that live in
the disc-resident pool (`box_01.kmd` ... `box_08.kmd`). Without those
the engine logs `KMD_BOX_NN not in cache` and silently drops the item.

This script reads the disc once, locates the resident DARs that contain
the box KMDs, and dumps them as raw blobs under
`tools/mgs_tools/stage/bundled/`. `import_stage.py` then merges them into
every custom stage's resident DAR so items render and pickup works.

Usage:
    python3 tools/extract_disc.py port/ISO/mgs.cue

Re-run only when bundled assets need to be refreshed (rare — the disc
contents don't change).
"""

import argparse
import os
import struct
import sys
from pathlib import Path

# tools/mgs_tools/cli/extract_disc.py → repo root is 4 levels up.
REPO = Path(__file__).resolve().parents[3]

from mgs_tools.common.iso       import IsoImage, ISO_USER_SIZE
from mgs_tools.stage.constants  import SECTOR_SIZE, gv_strcode


# ---------- STAGE.DIR + DATACNF parsing ------------------------------------

def read_stage_dir(stage_buf: bytes) -> list[tuple[str, int]]:
    """Return [(name, sector_offset), ...] from STAGE.DIR's header."""
    table_size = struct.unpack_from("<I", stage_buf, 0)[0]
    n = table_size // 12
    out = []
    for i in range(n):
        rec = stage_buf[4 + i * 12: 4 + (i + 1) * 12]
        name = rec[:8].rstrip(b"\x00").decode("ascii", "replace")
        sector = struct.unpack_from("<i", rec, 8)[0]
        out.append((name, sector))
    return out


def walk_assets(stage_blob: bytes):
    """Yield (id, ext_char, payload, region) for every entry in the stage's
    'r' (resident DAR) and 'c' (cache) regions. Mirrors the walker in
    port/libfs/libfs.c FS_LoadStageRequest."""
    cur = 4  # skip {version u8, pad u8, size u16}
    tags = []
    while cur + 8 <= len(stage_blob):
        tag_id, mode, ext, size = struct.unpack_from("<HBBI", stage_blob, cur)
        cur += 8
        if mode == 0:
            break
        tags.append((tag_id, mode, ext, size))
        if cur > SECTOR_SIZE:
            break

    data_ptr = SECTOR_SIZE
    i = 0
    while i < len(tags):
        tag_id, mode, ext, size = tags[i]
        if mode == ord('r'):
            # DAR archive: { u16 id, u8 ext, u8 pad, u32 size, payload[size] } *
            end = data_ptr + size
            p = data_ptr
            while p + 8 <= end:
                e_id, e_ext, _pad, e_size = struct.unpack_from("<HBBI", stage_blob, p)
                if e_size <= 0 or p + 8 + e_size > end:
                    break
                payload = stage_blob[p + 8:p + 8 + e_size]
                yield (e_id, chr(e_ext), payload, 'r')
                p += 8 + e_size
            data_ptr += (size + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1)
            i += 1
        elif mode == ord('c'):
            # Cache region: a run of c-tags. Each non-terminator c-tag's
            # `size` field is its byte offset into the c-region (from
            # c_base = data_ptr on entry). Entry size = next tag's offset
            # minus this tag's offset. The terminator (ext=0xFF) carries
            # the total c-region size.
            c_base = data_ptr
            j = i
            run = []
            region_size = 0
            while j < len(tags) and tags[j][1] == ord('c'):
                if tags[j][2] == 0xFF:
                    region_size = (tags[j][3] + 3) & ~3
                    j += 1
                    break
                run.append(tags[j])
                j += 1
            for k, (e_id, _mode, e_ext, e_off) in enumerate(run):
                if k + 1 < len(run):
                    next_off = run[k + 1][3]
                else:
                    next_off = region_size
                e_size = next_off - e_off
                if e_id == 0 or e_size <= 0:
                    continue
                start = c_base + ((e_off + 3) & ~3)
                yield (e_id, chr(e_ext), stage_blob[start:start + e_size], 'c')
            data_ptr = c_base + region_size   # NOT sector-aligned (libfs.c)
            i = j
        elif mode == ord('n') or mode == ord('s'):
            data_ptr += (size + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1)
            i += 1
        else:
            i += 1


# ---------- main ------------------------------------------------------------

# Hashes the importer wants to bundle (box_01..box_08 — referenced as
# KMD_BOX_01+type by source/game/item.c GetResources).
#
# Plus a generic sliding door (door_dd, hash 0x6E28 — pulled from d11c's
# resident DAR). Custom stages that drop a `chara &DOOR ... -m $s:6E28`
# need this in the cache or the door actor spawns with no model and the
# engine spins re-running the script. See port/doc/editor/05-stage-
# authoring.md "Doors" for the wider naming convention.
WANTED_KMDS = {gv_strcode(f"box_{i:02d}"): f"box_{i:02d}" for i in range(1, 9)}
WANTED_KMDS[gv_strcode("door_dd")] = "door_dd"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc", help="path to .cue or .bin/.iso of MGS disc")
    ap.add_argument("--out", default=str(REPO / "tools" / "mgs_tools" / "stage" / "bundled"),
                    help="output directory for extracted blobs")
    ap.add_argument("--prefer-stage", default=None,
                    help="preferred source stage (e.g. 'init', 's00a'). "
                         "If absent, the first stage that contains the hash wins.")
    args = ap.parse_args()

    # Resolve .cue → .bin.
    disc_path = Path(args.disc)
    if disc_path.suffix.lower() == ".cue":
        cue_text = disc_path.read_text()
        for line in cue_text.splitlines():
            line = line.strip()
            if line.upper().startswith("FILE"):
                # FILE "name.bin" BINARY
                start = line.find('"')
                end = line.find('"', start + 1)
                if start < 0 or end < 0:
                    raise SystemExit(f"can't parse FILE line: {line!r}")
                disc_path = disc_path.parent / line[start + 1:end]
                break
        else:
            raise SystemExit(f"no FILE line in {args.disc}")

    print(f"[disc] opening {disc_path}")
    img = IsoImage(disc_path)
    print(f"[disc] sector_size={img.sector_size} data_offset={img.data_offset}")

    sd_lba, sd_size = img.find_file("MGS/STAGE.DIR")
    print(f"[disc] STAGE.DIR @ lba={sd_lba} size={sd_size}")
    stage_dir = img.read_extent(sd_lba, sd_size)
    stages = read_stage_dir(stage_dir)
    print(f"[disc] {len(stages)} stage entries")

    # Reorder so --prefer-stage is tried first; otherwise leave order as-is.
    if args.prefer_stage:
        stages.sort(key=lambda s: 0 if s[0] == args.prefer_stage else 1)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    found: dict[int, tuple[str, str, bytes]] = {}   # hash → (stage, region, payload)
    for name, sector in stages:
        if not name or sector < 0:
            continue
        # Read the stage's first sector to get the DATACNF size.
        first = stage_dir[sector * SECTOR_SIZE:(sector + 1) * SECTOR_SIZE]
        if len(first) < 4:
            continue
        version, pad, n_sectors = struct.unpack_from("<BBH", first, 0)
        if n_sectors <= 0 or n_sectors > 4096:
            continue
        blob = stage_dir[sector * SECTOR_SIZE:(sector + n_sectors) * SECTOR_SIZE]
        if len(blob) < n_sectors * SECTOR_SIZE:
            # STAGE.DIR was under-reported. Stop on this stage; we still got prior ones.
            continue
        try:
            for e_id, e_ext, payload, region in walk_assets(blob):
                if e_ext == 'k' and e_id in WANTED_KMDS and e_id not in found:
                    found[e_id] = (name, region, payload)
                    print(f"[disc]   stage '{name}' [{region}]: "
                          f"{WANTED_KMDS[e_id]}.kmd "
                          f"(hash 0x{e_id:04X}, {len(payload)} bytes)")
                    if len(found) == len(WANTED_KMDS):
                        break
        except Exception as exc:                       # noqa: BLE001
            print(f"[disc]   stage '{name}': walk failed ({exc})")
        if len(found) == len(WANTED_KMDS):
            break

    if not found:
        raise SystemExit("no box KMDs found in any stage — disc layout unexpected")

    for h, (stage, region, payload) in sorted(found.items()):
        out_path = out_dir / f"{WANTED_KMDS[h]}.kmd"
        out_path.write_bytes(payload)
        print(f"[disc] wrote {out_path.relative_to(REPO)} "
              f"({len(payload)} B, from stage '{stage}' region '{region}')")

    missing = sorted(WANTED_KMDS.keys() - found.keys())
    if missing:
        print(f"[disc] WARNING: {len(missing)} KMDs missing: "
              f"{', '.join(WANTED_KMDS[h] for h in missing)}")
    else:
        print(f"[disc] all {len(WANTED_KMDS)} box KMDs extracted")


if __name__ == "__main__":
    main()
