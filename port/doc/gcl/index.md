# GCL — Game Command Language

MGS Integral ships its per-stage logic as bytecode written in
**GCL** (Game Command Language) — a small, untyped, stack-machine
language that drives stage init, actor spawns, alert state, cutscene
triggers, and the message bus between game systems. Every stage's
`.gcx` file in `STAGE.DIR` is a GCL program; the engine parses,
links and runs it on stage entry.

The pages in this folder are the complete reference — start with
[00-gcl-scripting.md](00-gcl-scripting.md) for the introduction,
then read in numerical order. Most readers can stop after
*04-commands.md* and refer to the later pages only when needed.

## Reading order

| Doc | What it covers |
| --- | -------------- |
| [00-gcl-scripting.md](00-gcl-scripting.md) | Overview — what GCL is, where it runs, why bytecode. **Start here.** |
| [01-syntax.md](01-syntax.md) | Source-level syntax: directives, blocks, comments. |
| [02-variables.md](02-variables.md) | The variable system — locals, the linkvar pool, stage-private vs global. |
| [03-expressions.md](03-expressions.md) | Operators, precedence, evaluation order, type rules. |
| [04-commands.md](04-commands.md) | Command reference — `chara`, `delay`, `mesg`, `kill`, `if`, `goto`, etc. The day-to-day surface. |
| [05-cookbook.md](05-cookbook.md) | Common patterns: timed spawn, conditional cutscene, alert state machine. |
| [06-binary-format.md](06-binary-format.md) | The `.gcx` on-disk format — opcode table, jump tables, string pool. |
| [07-triggers-and-events.md](07-triggers-and-events.md) | How GCL hooks into the engine's event bus (HZD events, area zones). |
| [08-known-ids.md](08-known-ids.md) | Hash-ID reference — `CHARA_*`, `STR_*`, `HZD_*`. The names you'll see in scripts. |
| [09-stages-and-loading.md](09-stages-and-loading.md) | Stage transitions — `load`, `change`, `route`. |
| [10-procs-and-args.md](10-procs-and-args.md) | Procedure calls — `proc`, `call`, argument passing, return. |
| [11-save-state.md](11-save-state.md) | What GCL state is preserved across saves vs reset on load. |
| [12-mesg-protocol.md](12-mesg-protocol.md) | The `mesg` system — actor-to-actor message protocol, hash conventions. |
| [13-debugging.md](13-debugging.md) | How to debug a misbehaving script — `print`, breakpoints, the ImGui GCL pane. |
| [14-spawning-actors.md](14-spawning-actors.md) | The `chara` directive in detail — factory lookup, parameter passing, lifetime. |

## How GCL fits into the engine

```
Stage load
   │
   ▼
DATACNF parser ─── reads stage.dnc ─── finds scenerio.gcx
   │
   ▼
GCL parser (source/libgcl/) ─── byte-code → instruction stream
   │
   ▼
GCL VM ─── runs in the GAME task slot, one tick per frame
   │
   ├──► chara directive    ─── GV_NewActor / factory lookup
   ├──► mesg directive     ─── GV_SendMessage to actor
   ├──► delay directive    ─── yields N ticks
   ├──► linkvar read/write ─── reads/writes linkvarbuf[]
   └──► proc call          ─── pushes return addr, jumps
```

The runtime is in [`source/libgcl/`](../source/libgcl/index.md);
the command dispatcher (per-opcode handlers) is in
[`source/game/script.c`](../source/game/index.md) — see the
[bytecode](../source/libgcl/bytecode.md) /
[command](../source/libgcl/command.md) doc pages for the
implementation side.

## Related

- [`source/libgcl/`](../source/libgcl/index.md) — the parser + VM.
- [`source/game/script.c`](../source/game/script.md) — per-command
  dispatcher (the big switch).
- [demo/](../demo/index.md) — cutscenes are also GCL programs
  (`demo.gcx` vs `scenerio.gcx`); the demo docs are the
  script-author view.
- [editor/](../editor/index.md) — the stage editor has a GCL
  syntax-highlighted editor pane.
