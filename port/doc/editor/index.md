# MGS Stage Editor

A standalone GL/ImGui front-end built on top of the port's libraries. It
loads any of the disc's 95 stages (or a custom one you've authored), renders
the level geometry unlit, overlays the HZD collision and actor placements
parsed from the GCL, and lets you edit/save actor positions and create new
stages from OBJ + PNG inputs.

The editor lives in [`port/editor/`](../../editor/) as a separate binary
from the main port (`port/mgs`), but reuses almost all of its code: the
SDL2/GL renderer, the libdg/libfs/libgv/libhzd loaders, and the
`port/imgui/` distribution.

This documentation covers the editor as it exists today.

## Contents

| Doc                                                | Topic                                                    |
| -------------------------------------------------- | -------------------------------------------------------- |
| [01-quickstart.md](01-quickstart.md)               | Build, launch, and walk through the UI                   |
| [02-features.md](02-features.md)                   | What every tab and overlay does, with screenshots cues   |
| [03-keybindings.md](03-keybindings.md)             | Full keyboard and mouse reference                        |
| [04-architecture.md](04-architecture.md)           | File layout, libraries linked, runtime data flow         |
| [05-stage-authoring.md](05-stage-authoring.md)     | OBJ + PNG → custom `s99a`-style stage workflow           |
| [06-asset-formats.md](06-asset-formats.md)         | KMD, HZD, PCX, DATACNF byte layouts (writer reference)   |
| [07-data-files.md](07-data-files.md)               | Per-stage actor TSV/JSON, manifest.json, generated files |
| [08-roadmap.md](08-roadmap.md)                     | What's done, what's deliberately out of scope, what next |

## At a glance

- **Read**: cube/model markers for every actor placed by the GCL,
  HZD walls/floors/traps/cameras/zones/routes, free-fly camera with
  click-to-pick, screenshots, world axes, mouse-cursor world readout.
- **Write**: edit actor `pos` and rotation in-place; click *Save* to
  rewrite the per-stage actor TSV.
- **Author**: convert a Blender-exported OBJ + PNG into a complete custom
  stage (`s99a` by default) using `tools/import_stage.py`. The editor
  auto-discovers `port/editor/extra_stages/<name>/` and lists the new
  stage in its picker alongside the disc's 95.
