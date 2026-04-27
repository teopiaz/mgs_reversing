# Python tooling

Single home for the project's Python scripts. Library code is the
`mgs_tools` package; user-facing commands are thin wrappers at the
top of this directory.

## Commands

| CLI                  | Purpose |
| -------------------- | ------- |
| `import_stage.py`    | Build a custom MGS stage from `.obj` + `.png` (+ optional collision OBJ + scenerio.gcl) → `port/editor/extra_stages/<name>/datacnf.bin`. |
| `extract_actors.py`  | Extract `chara` placements from one or more `.gcl` files into JSON + TSV the editor reads. `--batch` walks every stage under `port/overlays/` and `port/gcl/decompiled/`. |
| `extract_disc.py`    | Pull bundle-able blobs (box / door KMDs) out of the disc image into `mgs_tools/stage/bundled/`. Re-run only when the bundled set changes. |
| `gcl2gcx.py`         | Compile a `.gcl` text source into a `.gcx` bytecode blob. Used by `port/overlays/compile.sh` to round-trip every disc overlay. |
| `gcx2gcl.py`         | Decompile a `.gcx` bytecode blob back to `.gcl` text. |
| `show_glyphs.py`     | Render a `.gcx`'s stage-font glyph usage (debug aid for Japanese text). |
| `derive_table.py`    | Align `WantedThing.gcl` against `Rex.gcx` to derive Japanese-character → byte mappings. |

Each CLI is a thin wrapper around a `main()` inside `mgs_tools/cli/`;
`tools/import_stage.py` keeps its top-level path because the editor
shells out to it (`port/editor/ed_ui.cpp` Reimport / Play buttons).

## Library layout

```
mgs_tools/
├── common/                # shared utilities (no domain knowledge)
│   ├── strcode.py         # gv_strcode — single source of truth
│   └── iso.py             # IsoImage (ISO-9660 reader for disc images)
├── gcl/                   # GCL bytecode toolkit
│   ├── constants.py       # GclCommand / GclOperator / GclCode / opcodes
│   ├── gcx.py             # GclNode AST + bytecode buffer
│   ├── parser.py          # .gcl text → AST
│   ├── compile.py         # AST → .gcx bytes
│   ├── decompile.py       # .gcx → AST
│   ├── writer.py          # AST → .gcl text (pretty-printer)
│   ├── names.py           # `&NAME` symbolic constants ↔ hash
│   └── glyphs/            # Japanese-font character / glyph helpers
└── stage/                 # OBJ → DATACNF (custom-stage authoring)
    ├── constants.py       # VRAM / CLUT / DG model flags
    ├── obj_reader.py
    ├── kmd_writer.py      # geometry → PSX KMD
    ├── pcx_writer.py      # PNG → PSX PCX (8-bit indexed)
    ├── hzd_writer.py      # collision OBJ → HZD (walls + floors + traps + cameras)
    ├── datacnf_writer.py  # DAR archives + DATACNF wrap
    ├── gcx_writer.py      # writes the per-stage scenerio.gcx (uses gcl/)
    └── bundled/           # box_NN.kmd, door_dd.kmd (extracted from disc)
```

Importing across domains is straight Python, no `sys.path` hacks:

```python
from mgs_tools.common.strcode import gv_strcode
from mgs_tools.gcl.parser     import parse, GclSyntaxError
from mgs_tools.stage.gcx_writer import write_gcx
```
