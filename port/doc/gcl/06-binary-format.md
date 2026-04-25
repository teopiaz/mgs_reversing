# `.gcx` Binary Format

For tool writers. The runtime parser lives in
[source/libgcl/command.c:150 GCL_LoadScript](../../../source/libgcl/command.c#L150)
and [source/libgcl/parse.c:20 GCL_GetNextValue](../../../source/libgcl/parse.c#L20).

All multi-byte integers in the file are **big-endian**.

## File layout

```
┌───────────────────────────────────────────────┐ offset 0
│  proc_region_len  (uint32 BE)                 │
├───────────────────────────────────────────────┤ offset 4
│  proc table:                                  │
│    {proc_id: u16, offset: u16}                │
│    {proc_id: u16, offset: u16}                │
│    ...                                        │
│    0x00000000    ← terminator                 │
├───────────────────────────────────────────────┤
│  proc bodies     (packed, each a SCRIPT block)│
├───────────────────────────────────────────────┤ offset 4 + proc_region_len
│  script_region_len  (uint32 BE)               │
├───────────────────────────────────────────────┤
│  main script body  (starts with 0x40 SCRIPT)  │
├───────────────────────────────────────────────┤
│  font_region_len    (uint32 BE)               │
├───────────────────────────────────────────────┤
│  font glyph data   (bank-2 per-stage font)    │
└───────────────────────────────────────────────┘
```

After the font data there may be 0–3 bytes of padding to a 4-byte
boundary.

## Proc table

Each entry is 4 bytes: `proc_id` (hashed name via `GV_StrCode()`,
`u16 BE`) + `offset` (`u16 BE`) relative to the start of the proc-bodies
region.

The terminator is 4 zero bytes (`proc_id == 0 && offset == 0`). Our
decompiler uses this to locate where proc bodies start.

On load, `set_proc_table` byte-swaps both fields in place so later
native reads work correctly.

## Opcode table

All opcodes from [source/libgcl/libgcl.h:76](../../../source/libgcl/libgcl.h#L76):

| Hex  | Name                | Payload                           |
|------|---------------------|-----------------------------------|
| 0x00 | `GCL_NULL`          | (terminator / no payload)         |
| 0x01 | `WORD`              | 2 bytes (signed short, BE)        |
| 0x02 | `BYTE`              | 1 byte                            |
| 0x03 | `CHAR`              | 1 byte                            |
| 0x04 | `FLAG`              | 1 byte (0 / 1)                    |
| 0x06 | `HASHED_STRING`     | 2 bytes (string hash, BE)         |
| 0x07 | `STRING`            | 1 byte size + payload             |
| 0x08 | `PROC_CALL`         | 2 bytes (proc hash, BE)           |
| 0x09 | `SDCODE`            | 4 bytes                           |
| 0x0A | `TABLE_CODE`        | 4 bytes (vox/dmo/radio ref)       |
| 0x11…0x18 | `VARIABLE`     | 3 bytes (packed: type, scope, offset) |
| 0x20 | `STACK_VAR`         | 1 byte (arg index)                |
| 0x30 | `EXPRESSION`        | 1 byte size + RPN payload         |
| 0x31 | `EXPR_OPERATOR`     | 1 byte (operator enum)            |
| 0x40 | `SCRIPT_DATA`       | 2 bytes size BE + block body      |
| 0x50 | `PARAMETER`         | 1 byte letter + 1 byte size + payload |
| 0x60 | `COMMAND`           | 2 bytes total size BE + hash + args |
| 0x70 | `PROC` (call)       | 1 byte size + proc hash + args    |

Size-field semantics vary per opcode (STRING counts payload only,
PARAMETER includes itself + 1, SCRIPT_DATA counts size+body, …). See
the [decompiler](../../gcl_tools/gcl_decompile.py) for the exact match
to runtime behaviour.

## Expression operators

From [libgcl.h:95](../../../source/libgcl/libgcl.h#L95), paired with
`EXPR_OPERATOR` (opcode `0x31`):

| Code | Operator        | Arity |
|------|-----------------|-------|
| 0    | `OP_NULL`       | (terminator) |
| 1    | `NEGATE`        | unary |
| 2    | `ISFALSE` (!)   | unary |
| 3    | `COMPLEMENT` (~)| unary |
| 4    | `ADD`           | binary |
| 5    | `SUBTRACT`      | binary |
| 6    | `MULTIPLY`      | binary |
| 7    | `DIVIDE`        | binary |
| 8    | `MODULUS`       | binary |
| 9    | `EQUALS`        | binary |
| 10   | `NOTEQUALS`     | binary |
| 11   | `LESSTHAN`      | binary |
| 12   | `LESSTHANOREQUAL` | binary |
| 13   | `GREATERTHAN`   | binary |
| 14   | `GREATERTHANOREQUAL` | binary |
| 15   | `BITWISEOR`     | binary |
| 16   | `BITWISEAND`    | binary |
| 17   | `BITWISEXOR`    | binary |
| 18   | `OR`            | binary |
| 19   | `AND`           | binary |
| 20   | `ASSIGN`        | binary |

The RPN evaluator in [expr.c](../../../source/libgcl/expr.c) always
pops 2 operands (even for unary ops — hence the "dummy" first operand
the decompiler discards when rendering). Post-fix order:

```
push VAR ptr
push 0          ← dummy for NEGATE
push WORD N
OP NEGATE       ← result: -N
OP ASSIGN       ← store into VAR
OP_NULL         ← end
```

## Variable encoding (3 bytes)

```
bit  23 .. 20  |  19 .. 16  |  15 .. 0
    type code  |  flag bit  |  scope + offset
```

Decoded by [variable.c:199](../../../source/libgcl/variable.c#L199):
- **type code** (bits 23..20): `GCL_GetVarTypeCode()` = `(var << 1) >> 25) & 0xF` — picks which bucket (WORD/BYTE/FLAG/...).
- **flag bit** (bits 19..16): for FLAG type, which bit inside the byte.
- **offset** (bits 15..0): byte offset; high bit (`0x800000`) signals game-state scope.

Our text form shows these as 6-hex after the sigil (see
[02-variables.md](02-variables.md#the-encoded-address)).

## Tooling

Everything above is implemented in the Python tooling:

- [gcl_decompile.py](../../gcl_tools/gcl_decompile.py) — bytecode → AST
- [gcl_writer.py](../../gcl_tools/gcl_writer.py) — AST → `.gcl` text
- [gcl_parser.py](../../gcl_tools/gcl_parser.py) — `.gcl` text → AST
- [gcl_compile.py](../../gcl_tools/gcl_compile.py) — AST → bytecode

All 135 vanilla files round-trip byte-exact through the full pipeline.

## Trailing font data

Positions `[script_end + 4, EOF)` of the `.gcx` hold 12×12 glyph
bitmaps (2 bits/pixel, 36 bytes each) for any codes ≥ 0x9A00 used in
this stage's `m"..."` strings. See
[glyph_extractor.py](../../gcl_tools/glyph_extractor.py) and
[source/font/font.c](../../../source/font/font.c) for the rendering
side; also `show_glyphs.py --sheet` to dump them for visual
inspection.

When patching a script, keep the trailing blob if you use any
bank-2 characters — our [compile.sh](../../overlays/compile.sh)
automatically pairs the sibling `.tail` file.
