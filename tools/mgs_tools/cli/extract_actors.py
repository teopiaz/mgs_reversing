#!/usr/bin/env python3
"""Extract actor placements from a decompiled .gcl scenerio file into JSON.

The editor (port/editor/) loads this JSON to draw a marker for every actor
the GCL script spawns: enemies, cameras, doors, items, etc. We reuse the
existing port/gcl_tools/gcl_parser.py to walk the AST so we don't duplicate
parsing logic.

For each `chara` command we capture:
  - type      : the &NAME (e.g. WATCHER) — looked up via mgs_names hash
  - instance  : the $s:HEX literal (16-bit object instance ID)
  - pos       : [x, y, z] from the -n option (None if absent → non-spatial)
  - rot       : raw -r argument (preserved as a string; usually b:N)
  - flags     : raw -f argument (preserved)
  - options   : every other -X option, stored verbatim
  - proc      : containing `proc sub_XXXX` name
  - line      : source line number for click-through

Usage:
  python3 tools/extract_gcl_actors.py \\
      --input port/overlays/s01a/scenerio.gcl \\
      --output port/editor/data/s01a_actors.json
"""

import argparse
import json
import re
import sys
from pathlib import Path

THIS = Path(__file__).resolve()
# tools/mgs_tools/cli/extract_actors.py → repo root is 4 levels up.
REPO = THIS.parents[3]

from mgs_tools.gcl.parser import parse
from mgs_tools.gcl.names  import HASH_TO_NAME


def first(node, key):
    """Return the value of node[key] if node is a dict-like with that key."""
    if isinstance(node, dict) and key in node:
        return node[key]
    return None


def operand_value(node):
    """Decode a single argument GclNode → a Python primitive (or None)."""
    if not isinstance(node, dict):
        return node
    keys = list(node.keys())
    if not keys:
        return None
    k = keys[0]
    v = node[k]
    if k == "WORD":
        return int(v)
    if k == "BYTE":
        return f"b:{int(v)}"
    if k == "FLAG":
        return bool(v)
    if k == "STR_ID":
        # 16-bit string-code hash: prefer symbolic name if known.
        h = int(v)
        return HASH_TO_NAME.get(h, f"$s:{h:04X}")
    if k == "STR":
        return str(v)
    if k == "CHAR":
        return f"'{v}'"
    if k == "VAR":
        # Nested {SIGIL: hex6}
        if isinstance(v, dict):
            sigil_key, hex6 = next(iter(v.items()))
            return f"${sigil_key.lower()[0] if sigil_key else 'w'}:{hex6}"
        return repr(v)
    if k == "PROC":
        return f"sub_{int(v):04X}"
    if k == "ARG":
        return f"arg{int(v)}"
    if k == "SD_CODE":
        return f"sd:{int(v):X}"
    if k == "TABLE":
        return f"t:{int(v):X}"
    if k == "OP":
        # Negation / arithmetic: best-effort. Just stringify.
        return repr(v)
    return {k: operand_value(v) if isinstance(v, dict) else v}


def parse_chara_args(items):
    """Walk the items list of a `chara` command. Returns:
       (positional [type_name, instance], opts {letter: [values...]})."""
    positional = []
    opts = {}
    for it in items:
        if not isinstance(it, dict):
            continue
        if "OPTION" in it:
            opt = it["OPTION"]
            if not isinstance(opt, dict):
                continue
            # Skip the NULL_SIZE flag if present.
            keys = [k for k in opt.keys() if k != "NULL_SIZE"]
            if not keys:
                continue
            letter = keys[0]
            values = opt[letter]
            if not isinstance(values, list):
                values = [values]
            opts[letter] = [operand_value(v) for v in values]
        else:
            positional.append(operand_value(it))
    return positional, opts


def build_line_index(src_text: str):
    """Return a function that maps a character offset → 1-based line."""
    line_starts = [0]
    for m in re.finditer(r"\n", src_text):
        line_starts.append(m.end())
    def offset_to_line(off):
        # Binary search.
        lo, hi = 0, len(line_starts)
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if line_starts[mid] <= off:
                lo = mid
            else:
                hi = mid
        return lo + 1
    return offset_to_line


def color_for(type_name: str) -> str:
    """Stable ARGB-like hex string per type, derived from name hash."""
    h = 0
    for ch in (type_name or "?").encode("utf-8"):
        h = (h * 131 + ch) & 0xFFFFFF
    # Avoid too-dark colors: bias each channel up.
    r = 90 + ((h >> 16) & 0xFF) % 166
    g = 90 + ((h >> 8)  & 0xFF) % 166
    b = 90 + ( h        & 0xFF) % 166
    return f"#{r:02X}{g:02X}{b:02X}"


def find_chara_in_proc(proc_node, proc_name, src_text, offset_to_line, out):
    """Walk a proc body, emitting JSON entries for every chara command found."""
    body = proc_node.get("PROC_DATA")
    if not body:
        return
    # body is a SCRIPT node {SCRIPT: [stmts]}
    script_stmts = body.get("SCRIPT") if isinstance(body, dict) else None
    if not script_stmts:
        return

    def walk(stmts):
        if isinstance(stmts, list):
            for s in stmts:
                walk(s)
            return
        if not isinstance(stmts, dict):
            return
        if "CMD" in stmts:
            cmd = stmts["CMD"]
            if isinstance(cmd, dict):
                cname = next(iter(cmd))
                if cname == "CHARA":
                    items = cmd[cname]
                    pos_args, opts = parse_chara_args(items if isinstance(items, list) else [])
                    type_name = pos_args[0] if pos_args else None
                    instance  = pos_args[1] if len(pos_args) > 1 else None

                    # Position option varies by chara type. WATCHER (humanoid
                    # enemies, COMMANDER children) uses -n; static props
                    # (SEARCHLIGHT, CAMERA, DOOR, ITEM, CAMERA_SHAKE, ...)
                    # use -p. Take whichever holds three plain integers.
                    pos_xyz = None
                    pos_letter = None
                    for letter in ("n", "p"):
                        cand = opts.get(letter)
                        if (cand and len(cand) >= 3 and
                                all(isinstance(v, int) for v in cand[:3])):
                            pos_xyz = list(cand[:3])
                            pos_letter = letter
                            break
                    has_pos = pos_xyz is not None

                    def to_str(x):
                        if isinstance(x, list) and len(x) == 1:
                            return to_str(x[0])
                        return x

                    out.append({
                        "type": str(type_name) if type_name is not None else None,
                        "instance": str(instance) if instance is not None else None,
                        "pos": pos_xyz,
                        "pos_option": pos_letter,
                        "non_spatial": not has_pos,
                        "rot": to_str(opts.get("r") or opts.get("d")),
                        "flags": to_str(opts.get("f")),
                        "options": opts,
                        "proc": proc_name,
                        "color": color_for(str(type_name) if type_name else "?"),
                    })
                # IF / FOREACH bodies contain nested SCRIPTs — recurse into all values.
                for v in cmd.values() if isinstance(cmd, dict) else []:
                    walk(v)
        else:
            for v in stmts.values():
                walk(v)

    walk(script_stmts)


def parse_one(src_path: Path, source_tag: str, out_list):
    src_text = src_path.read_text()
    ast = parse(src_text)
    offset_to_line = build_line_index(src_text)
    (void := offset_to_line)  # placeholder — keep for future click-through
    top_level = ast if isinstance(ast, list) else [ast]
    before = len(out_list)
    for node in top_level:
        if isinstance(node, dict) and "PROC_ID" in node:
            pid = node.get("PROC_ID")
            proc_name = f"sub_{pid:04X}" if isinstance(pid, int) else "<script>"
            find_chara_in_proc(node, proc_name, src_text, offset_to_line, out_list)
    # Tag the new entries with their source.
    for a in out_list[before:]:
        a["source"] = source_tag


def extract(inputs, output_json):
    """Library entry point: walk every GCL in `inputs` (Path or list of
    Paths) and write the JSON + companion TSV the editor reads.

    Returns (n_entries, n_spatial). Used by tools/import_stage.py to
    populate `port/editor/data/<stage>_actors.{json,tsv}` for custom
    stages."""
    if isinstance(inputs, (str, Path)):
        inputs = [inputs]
    out = []
    for inp in inputs:
        p = Path(inp)
        if not p.is_file():
            continue
        tag = "demo" if p.name == "demo.gcl" else "scenerio"
        parse_one(p, tag, out)

    out_path = Path(output_json)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(out, indent=2))

    # Companion TSV the C editor reads. One row per spatial actor.
    # Columns: type, instance, x, y, z, color, proc, rot, from_demo
    tsv_path = out_path.with_suffix(".tsv")
    rows = []
    for a in out:
        if a["non_spatial"]:
            continue
        x, y, z = a["pos"]
        rot = a.get("rot")
        rot_s = "" if rot is None else (rot if isinstance(rot, str) else json.dumps(rot))
        from_demo = "1" if a.get("source") == "demo" else "0"
        rows.append("\t".join([
            a["type"] or "?",
            a["instance"] or "?",
            str(int(x)), str(int(y)), str(int(z)),
            a["color"] or "#FFFFFF",
            a["proc"] or "?",
            rot_s,
            from_demo,
        ]))
    tsv_path.write_text("\n".join(rows) + ("\n" if rows else ""))
    n_spatial = len(rows)
    return len(out), n_spatial


def discover_stages():
    """Discover every stage with a decompiled scenerio.gcl. Returns
    `{stage_name: [Path, ...]}` — overlays/ wins over gcl/decompiled/
    when both have a stage (it's the one the engine actually loads).
    Used by the --batch mode (replaces the old build_editor_data.py)."""
    stages: dict[str, list[Path]] = {}
    for base in (REPO / "port" / "gcl" / "decompiled",
                 REPO / "port" / "overlays"):
        if not base.is_dir():
            continue
        for child in base.iterdir():
            paths = []
            for fname in ("scenerio.gcl", "demo.gcl"):
                p = child / fname
                if p.is_file():
                    paths.append(p)
            if paths:
                stages[child.name] = paths
    return stages


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input",  action="append",
                    help="path to scenerio.gcl or demo.gcl (may be repeated; "
                         "ignored when --batch is set)")
    ap.add_argument("--output",
                    help="path to write JSON (omitted in --batch mode — "
                         "files land at port/editor/data/<stage>_actors.json)")
    ap.add_argument("--batch", action="store_true",
                    help="run over every stage that has a decompiled "
                         "scenerio.gcl (under port/overlays/ or "
                         "port/gcl/decompiled/) and emit per-stage JSON+TSV. "
                         "Replaces the old build_editor_data.py.")
    args = ap.parse_args()

    if args.batch:
        out_dir = REPO / "port" / "editor" / "data"
        out_dir.mkdir(parents=True, exist_ok=True)
        stages = discover_stages()
        if not stages:
            print("no stages with decompiled scenerio.gcl found", file=sys.stderr)
            return 1
        ok = fail = 0
        for name in sorted(stages):
            out = out_dir / f"{name}_actors.json"
            try:
                extract([str(p) for p in stages[name]], str(out))
                ok += 1
            except Exception as e:                       # noqa: BLE001
                print(f"  {name}: FAILED — {e}", file=sys.stderr)
                fail += 1
        print(f"generated {ok} stages, {fail} failed -> {out_dir}")
        return 0 if fail == 0 else 2

    if not args.input or not args.output:
        ap.error("--input and --output are required (or pass --batch)")

    n_total, n_spatial = extract(args.input, args.output)
    out_path = Path(args.output)
    print(f"extracted {n_total} chara entries ({n_spatial} spatial) -> {out_path}")
    print(f"editor TSV ({n_spatial} rows) -> {out_path.with_suffix('.tsv')}")


if __name__ == "__main__":
    main()
