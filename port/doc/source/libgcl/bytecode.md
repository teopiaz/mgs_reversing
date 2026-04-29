---
file: source/libgcl/{libgcl.h, gcl_init.c, basic.c}
---

# GCL bytecode — format reference

GCL is the **scripting language** that drives every stage's spawn
list, every cinematic's choreography, every codec call's branching
dialogue. The compiler lives in `tools/mgs_tools/gcl/` (Python); the
interpreter is in `source/libgcl/`. The disc carries `.gcx` (bytecode)
and `.dmo` (cutscene) files; both share the same opcode space.

## File types

| Ext | Source | Format | Loaded by |
| --- | ------ | ------ | --------- |
| `.gcx` | per-stage scenario or radio | bytecode | `GCL_LoadScript` |
| `.gcl` | source text — never on disc | — | `tools/gcl2gcx.py` compiles |
| `.dmo` | cinematic scenario | bytecode (subset) | per-stage code |
| `radio.dat` | codec dialogue | bytecode (RDCODE_*) | radio.c |
| `.vox` | voice clips | manifest | sound system |

## File layout (`.gcx`)

```
+---------------------------+
| len_proc_table  (u32)     |
+---------------------------+
| GCL_PROC_TABLE proc_table |  // {proc_id (u16), offset (u16)}[]  zero-terminated
+---------------------------+
| proc_body                 |  // bytecode for each proc
+---------------------------+
| len_script_data (u32)     |
+---------------------------+
| script_body               |  // top-level instructions
+---------------------------+
| string_data               |  // appended C strings, font glyphs
+---------------------------+
```

`script_body` is the boot block — the engine starts there. It calls
into procs (named `&PROC_NAME`) which live in `proc_body`.

## Top-level opcodes

```c
#define GCLCODE_NULL            0
#define GCLCODE_SHORT           1   // 16-bit literal
#define GCLCODE_BYTE            2   // 8-bit literal
#define GCLCODE_CHAR            3   // 8-bit literal
#define GCLCODE_FLAG            4   // 1-bit literal
#define GCLCODE_HASHED_STRING   6   // 16-bit string hash
#define GCLCODE_STRING          7   // length-prefixed string
#define GCLCODE_PROC_CALL       8   // 16-bit hashed proc name
#define GCLCODE_SDCODE          9   // 32-bit constant
#define GCLCODE_TABLE_CODE      10  // table reference (DMO/VOX)
#define GCLCODE_VARIABLE        0x10..0x18  // var read
#define GCLCODE_STACK_VAR       0x20  // stack variable (proc arg)
#define GCLCODE_EXPRESSION      0x30  // bracketed expr
#define GCLCODE_EXPR_OPERATOR   0x31
#define GCLCODE_SCRIPT_DATA     0x40  // raw embedded data
#define GCLCODE_PARAMETER       0x50  // -p, -d, -l etc. CLI options
#define GCLCODE_COMMAND         0x60  // hashed command call
#define GCLCODE_PROC            0x70  // proc definition / call
```

Bit-pattern detection:

```c
#define GCL_IsVariable(c) ((c & 0xF0) == GCLCODE_VARIABLE)   // 0x10..0x1F
#define GCL_IsParam(c)    ((c & 0xFF) == GCLCODE_PARAMETER)
```

## Variable spaces

```
$w:  GCLCODE_SHORT     short[1024]   per-game persistent
$b:  GCLCODE_BYTE      byte[]        per-game persistent
$f:  GCLCODE_FLAG      bit[]         per-game persistent (saved to memcard)
$s:  GCLCODE_HASHED_STRING           — (immediate hashed name, not a var)
```

The `$w` / `$b` / `$f` arrays are saved/restored by `variable.c`. Each
variable encoding packs:

```c
GCL_GetVarTypeCode(v)  =  (v << 1) >> 25 & 0xF        // var-space ID
GCL_GetVarOffset(v)    =  v & 0xFFFF                   // index within space
GCL_IsGameStateVar(v)  =  (v & 0xF00000) == 0x800000   // game-state $vs prefix
GCL_GetFlagBitFlag(v)  =  1 << ((v << 1) >> 17 & 0xF)  // bit within byte for flags
```

## Operators (expressions)

```c
1   GCL_OP_NEGATE
2   GCL_OP_NOT
3   GCL_OP_COMPL          (bitwise complement)
4   GCL_OP_ADD
5   GCL_OP_SUB
6   GCL_OP_MUL
7   GCL_OP_DIV
8   GCL_OP_MOD
9   GCL_OP_EQUALS
10  GCL_OP_NOT_EQ
11  GCL_OP_LESS
12  GCL_OP_LESS_EQ
13  GCL_OP_GREATER
14  GCL_OP_GREATER_EQ
15  GCL_OP_BITOR
16  GCL_OP_BITAND
17  GCL_OP_BITXOR
18  GCL_OP_OR             (logical)
19  GCL_OP_AND
20  GCL_OP_ASSIGN
```

Reverse-Polish stack-based; `GCL_Expr` evaluates a postfix expression
using a small per-call stack.

## Radio dialogue opcodes (`radio.dat`)

```c
0     RDCODE_NULL
1     RDCODE_TALK            // text line
2     RDCODE_VOICE           // VOX clip ref
3     RDCODE_ANIM            // face animation
4     RDCODE_ADD_CONTACT     // unlock new frequency
5     RDCODE_MEMSAVE         // "remember this freq"
6     RDCODE_SOUND
7     RDCODE_PROMPT          // wait for player button
8     RDCODE_VARSAVE         // store choice → var
0x10  RDCODE_IF
0x11  RDCODE_ELSE
0x12  RDCODE_ELSEIF
0x20  RDCODE_SWITCH
0x21  RDCODE_SWITCH_CASE
0x22  RDCODE_SWITCH_DEFAULT
0x30  RDCODE_RANDSWITCH
0x31  RDCODE_RANDSWITCH_CASE
0x40  RDCODE_EVAL
0x80  RDCODE_SCRIPT          // recursive: a GCL block inline
0xFF  RDCODE_ENDLINE
```

`radio.dat` is essentially a separate dialect using a different
opcode space, but it can host inline GCL via `RDCODE_SCRIPT` (0x80).

## Endianness

GCL is **big-endian** (PSX is little-endian!). All multi-byte values
in `.gcx` files are written MSB-first. The `GCL_GetShort` /
`GCL_GetLong` helpers swap on read:

```c
static inline long GCL_GetLong(char *ptr) {
    return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}
```

This is a deliberate choice — GCL files were designed to be portable
across the development tools (PC-side) and the runtime (PSX), so
they used network byte order.

## Initialisation — `gcl_init.c`

```c
void GCL_StartDaemon(void);   // register the GCL "tick" actor
void GCL_ResetSystem(void);   // wipe vars between scenarios
void GCL_ChangeSenerioCode(int demo_flag);   // toggle DEMO mode
```

The daemon is a level-0 actor that processes incoming proc-call
requests (`GCL_ExecProc`) once per frame.

## Basic command set — `basic.c`

Registers a small core of commands that are always available:

| Command | Hash | Effect |
| ------- | ---- | ------ |
| `print` | strcoded | debug print to console |
| `wait` | | sleep N frames |
| `mesg` | | post a message via `GV_SendMessage` |
| `if/else/endif` | | structural |
| `proc/endproc` | | proc def boundaries |
| `chara` | | spawn an actor by name (e.g. `chara &WATCHER -p (...)`) |

Other commands are added by `game/script.c::GM_InitScript` and other
subsystems via `GCL_AddCommMulti`.

## See also

- [parse.md](parse.md) — value/parameter parsing.
- [command.md](command.md) — command + proc execution.
- [expr.md](expr.md) — expression evaluator.
- [variable.md](variable.md) — variable spaces and save/load.
- [`tools/mgs_tools/gcl/`](../../../../tools/) — the compiler.
