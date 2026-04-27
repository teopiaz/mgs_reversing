# GCL Tools — moved

The GCL bytecode toolkit lives at
[`tools/mgs_tools/gcl/`](../../tools/mgs_tools/gcl/) now, and the CLI
entry points are at the repo's [`tools/`](../../tools/):

| Was                          | Is                              |
| ---------------------------- | ------------------------------- |
| `port/gcl_tools/gcl2gcx.py`  | `tools/gcl2gcx.py`              |
| `port/gcl_tools/gcx2gcl.py`  | `tools/gcx2gcl.py`              |
| `port/gcl_tools/show_glyphs.py` | `tools/show_glyphs.py`       |
| `port/gcl_tools/derive_table.py`| `tools/derive_table.py`      |
| `port/gcl_tools/*.py` (libs) | `tools/mgs_tools/gcl/*.py`      |

See [tools/mgs_tools/gcl/README.md](../../tools/mgs_tools/gcl/README.md)
for the full toolkit doc, and [tools/README.md](../../tools/README.md)
for the cross-domain library layout.

`vscode-gcl/` (the VSCode language extension) stays here — it has no
import relationship to the Python tooling.
