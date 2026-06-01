# MGS macOS Port — Documentation

This is the documentation tree for the **Metal Gear Solid: Integral**
PSX-to-macOS port — a native build of the fully-decompiled C
codebase, with PSX hardware replaced by SDL2, an OpenGL renderer,
and a software-SPU audio backend. The disc binary remains
buildable byte-for-byte; this port and the editor link the same
`source/` tree.

## Start here

| Doc | Read when you want to know… |
| --- | --- |
| [00-overview.md](00-overview.md) | What's working today, build instructions, the current control map, env-var reference, and an at-a-glance directory layout. **First stop.** |
| [02-port-architecture.md](02-port-architecture.md) | How `port/` plugs into `source/` — frame loop, pre-game menu, memory pools, SDL backend. |
| [04-rendering.md](04-rendering.md) | PSX GPU pipeline + the port's software + OpenGL renderers + widescreen coverage. |
| [10-changelog.md](10-changelog.md) | Chronological log of fixes and milestones. |
| [12-todo.md](12-todo.md) | What's still open, prioritised. |

## Per-topic deep dives

| Topic | Entry point |
| ----- | ----------- |
| **PSX hardware reference** | [01-psx-architecture.md](01-psx-architecture.md) |
| **64-bit port work** | [03-64bit-porting.md](03-64bit-porting.md) |
| **Sound / SPU emulator** | [05-sound.md](05-sound.md) |
| **Filesystem & STAGE.DIR** | [06-filesystem.md](06-filesystem.md) |
| **Actor system + GCL** | [07-actors-gcl.md](07-actors-gcl.md) |
| **Collision (HZD)** | [08-collision.md](08-collision.md) |
| **Input** | [09-input.md](09-input.md) |
| **Known issues** | [11-known-issues.md](11-known-issues.md) |
| **Shading & lighting** | [13-shading-and-lighting.md](13-shading-and-lighting.md) |

## Sub-trees

| Tree | What's in it |
| ---- | ------------ |
| [demo/](demo/index.md) | Cutscene streaming, the DMO file format, the editor's Demo Player, frame-scrubber instructions. |
| [editor/](editor/index.md) | The standalone stage editor — collision authoring, GCL editing, asset previews. |
| [gcl/](gcl/00-gcl-scripting.md) | GCL bytecode reference — syntax, commands, expressions, the binary format, debugging recipes. |
| [source/](source/index.md) | Per-subsystem reference for the decompiled PSX engine (`source/lib*`, `source/game/`, `source/menu/`, `source/sound/`, etc.). Each subsystem has its own index + per-component pages + `_unreversed.md`. |

## Conventions

Doc pages follow a stable layout:

1. **Frontmatter** — `file: source/<path>.c` on per-component pages.
2. **Sections** — describing the original PSX engine.
3. **Pitfalls** — gotchas the doc calls out.
4. **See also** — cross-references to neighbouring docs.
5. **`---`** — visual separator.
6. **Port notes** — port-specific notes after the rule. A reader who
   only cares about the original engine can stop at the rule.

Folder landing pages are named `index.md`. The HTML build under
`html/` rewrites every internal `.md` link to `.html` and serves
`<dir>/index.html` when the URL points at the folder root.

## Building the HTML

```bash
cd port/doc
make deps      # one-time: pip install markdown
make html      # builds html/ next to the markdown sources
```

The output is a standalone static site. Each page has a
**breadcrumb** at the top (Home › subsystem › page), a fixed
**Home** link via that breadcrumb, and a corner **theme toggle**
(`☼ Light` / `☾ Dark`) that respects your OS preference by default.

The site is fully usable from `file://` URLs — no server required —
so you can browse it directly out of `port/doc/html/` after
`make html`.
