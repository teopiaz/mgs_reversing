# `source/libgcl/` — bytecode scripting

GCL = "Game Control Language". A small bytecode-driven scripting
engine that runs the per-stage `scenerio.gcx` and `demo.gcx`
scripts. Every `chara` / `mesg` / `delay` / `light` / `map` /
`load` directive in a stage's GCL is dispatched by this code.

## Files

| File | Role |
| ---- | ---- |
| [`gcl_init.c`](../../../../source/libgcl/gcl_init.c) | `GCL_StartDaemon`, `GCL_LoadScript`, `GCL_ExecScript` — loader / boot. |
| [`command.c`](../../../../source/libgcl/command.c) | The `GCL_Command` dispatcher + table — `FindCommand`, `GCL_ExecBlock`. |
| [`basic.c`](../../../../source/libgcl/basic.c) | Built-in commands — `if`, `foreach`, `eval`, `delay`, `proc`, `call`, `return`. |
| [`expr.c`](../../../../source/libgcl/expr.c) | Expression evaluator — operators (`+`, `-`, `&&`, `==`, etc.) for `eval(...)`. |
| [`parse.c`](../../../../source/libgcl/parse.c) | Bytecode walker — `GCL_GetOption`, `GCL_GetParamResult`, `GCL_StrToInt`, `GCL_ReadString`. |
| [`variable.c`](../../../../source/libgcl/variable.c) | Variable spaces — `$w:` (16-bit world), `$b:` (byte), `$f:` (1-bit flag). `GCL_SaveVar` / `GCL_LoadVar` for memcard. |
| [`libgcl.h`](../../../../source/libgcl/libgcl.h) | Public API. |

## API

```c
/* Lifecycle */
void GCL_StartDaemon(void);                 /* register cache loader */
int  GCL_LoadScript(unsigned char *blob);   /* parse one .gcx into current_script */
void GCL_ExecScript(void);                  /* walk the script body, run commands */
int  GCL_ExecProc(int proc_id, void *args); /* call one proc by id */

/* Within a command handler */
int   GCL_GetOption(char tag);              /* find -X option in current directive */
int   GCL_GetParamResult(void);             /* read next operand value */
int   GCL_GetNextParamValue(void);          /* shorthand for next int operand */
char *GCL_ReadString(int marker);           /* string-literal operand */
int   GCL_StrToInt(char *opt);              /* convert "b:N" / "t:N" / etc. */
void  GCL_StrToSV(char *opt, SVECTOR *out); /* parse 3-component vector */

/* Variables */
int  GCL_GetVar(int var_id);                /* read $w:NNN / $b:NN / $f:N */
void GCL_SetVar(int var_id, int value);

/* Custom commands — game-side registration */
void GCL_AddCommand(int hash, GCL_CMD_HANDLER handler);
```

## Bytecode format

GCL scripts compile (offline via `tools/gcl2gcx.py`) from text
to a compact bytecode. The runtime walker (`command.c`)
distinguishes:

- **Command** — opcode + tag bytes for options + operand list.
  Looks up handler via the command table, calls it.
- **Eval** — expression bytecode (`expr.c`).
- **Block / If / Foreach / Proc** — control flow (`basic.c`).

Compile + decompile reference:

- `tools/gcl2gcx.py` — text → bytecode (round-trips disc bytes
  byte-exact).
- `tools/gcx2gcl.py` — bytecode → text.
- `port/gcl/decompiled/` — every disc stage's pre-decompiled
  text source.

## Variable spaces

```c
$w:NNNN    /* 16-bit "world" var — survives stage transitions */
$b:NN      /* 8-bit "byte" var — same */
$f:NNN     /* 1-bit flag — same; used for "this event happened" */
$s:HHHH    /* string code (immediate constant — not a var) */
```

`$w` / `$b` / `$f` are saved to memcard via `GCL_SaveVar`.
`GM_LoadStage` decides whether to save (load-flag bit `0x10`).

## Used by

- Every stage's `scenerio.gcx` and `demo.gcx`.
- `source/game/script.c::GM_InitScript` registers the engine-side
  command set (`mesg` / `chara` / `light` / `map` / `delay` /
  `demo` / `pad` / `sound` / etc.).
- The editor's Demo Player calls `GCL_LoadScript` /
  `GCL_ExecScript` directly.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [bytecode.md](bytecode.md) | Opcode reference, file layout, var spaces, operators, radio.dat opcodes, endianness |
| [parse.md](parse.md) | `GCL_GetNextValue`, arg/cmdline stacks, `GCL_GetOption` |
| [command.md](command.md) | Command + proc dispatcher, `GCL_ExecBlock`, yield semantics |
| [expr.md](expr.md) | Expression evaluator + variable spaces ($w / $b / $f) + save/load |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`tools/mgs_tools/gcl/`](../../../../tools/mgs_tools/gcl/) —
  Python compile / decompile.
- [`source/game/script.c`](../../../../source/game/script.c) —
  the engine-side command registration.
- [`port/gcl/decompiled/`](../../../../port/gcl/decompiled/) —
  every disc script in readable text form.
- [doc/demo/02-data-flow.md](../../demo/02-data-flow.md) — how
  scenerio/demo scripts flow at stage entry.
