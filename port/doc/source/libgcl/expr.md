---
file: source/libgcl/expr.c + variable.c
---

# `libgcl/expr.c` + `variable.c` — expressions + variable spaces

## `GCL_Expr` — postfix evaluator

```c
int GCL_Expr(unsigned char *pScript, int *retValue);
```

Walks the bytecode of a parenthesised expression, evaluating it on
a small per-call value stack. Bytecode is **postfix** (RPN):

```
( $w:5 + 3 )       compiles to:
  VAR $w:5         (push)
  SHORT 3          (push)
  EXPR_OPERATOR ADD  (pop 2, push result)
  NULL
```

Each `EXPR_OPERATOR` byte is followed by the operator code (1..20
from `GCL_OP_*`).

Operations:

| Op | Stack effect |
| -- | ------------ |
| NEGATE / NOT / COMPL | unary: pop 1, push -x / !x / ~x |
| ADD..MOD | binary arithmetic |
| EQUALS..GREATER_EQ | compare → 0/1 |
| BITOR/AND/XOR | bitwise |
| OR/AND | logical |
| ASSIGN | pop 2 (lvalue, rvalue), store rvalue, push rvalue |

`ASSIGN` is the only operator that mutates state (writes back via
`GCL_SetVar`). Everything else just shuffles the stack.

The stack is small (16 entries) — deeply nested expressions overflow
silently. In practice scripts don't approach this; max depth seen
is ~4.

## `variable.c` — variable spaces

The four variable spaces:

| Space | Code | Storage | Lifetime |
| ----- | ---- | ------- | -------- |
| `$w:` | SHORT | `short var[1024]` | per-game (saved) |
| `$b:` | BYTE | shared with `$w` (byte-level access) | per-game |
| `$f:` | FLAG | shared (bit-level access) | per-game |
| `$s:` | (immediate hash) | — | n/a |
| `$vs:` | game-state | linked to global game vars | per-game |

The 1024-short array is shared between `$w`, `$b`, `$f` views — they
all index the same memory in different chunk sizes. So `$w:0` is
the same physical short as `$b:0` (low byte) + `$b:1` (high byte)
+ `$f:0..7` (low byte's bits) + `$f:8..15` (high byte's bits).

### Encoding

```c
GCL_GetVarTypeCode(v)   = ((v << 1) >> 25) & 0xF      // 0..3 = w/b/f/...
GCL_GetVarOffset(v)     = v & 0xFFFF                    // index into space
GCL_IsGameStateVar(v)   = (v & 0xF00000) == 0x800000   // global game var
GCL_GetFlagBitFlag(v)   = 1 << ((v << 1) >> 17 & 0xF)  // bit selector for $f
```

`v` is a 32-bit packed: high nibble = type, low 16 bits = offset,
bit-of-byte for $f. Compiler in `tools/mgs_tools/gcl/` produces
these encodings.

### `$vs:` — linked variables

Special namespace that points into engine-managed globals (alert
state, current weapon, etc.) rather than the GCL var array. Reads
hit a function-pointer table maintained by the game side.

```c
void GCL_SaveLinkVar(short *gameVar);
```

Registers a global short as accessible via `$vs:N`. Called by
`game/` setup code at boot.

## API: read / write

```c
unsigned char *GCL_GetVar(unsigned char *top, int *type_p, intptr_t *value_p);
unsigned char *GCL_SetVar(unsigned char *top, unsigned int value);
```

`GCL_GetVar` is called by `GCL_GetNextValue` when the leading byte
is in 0x10..0x1F. Decodes the variable, reads the underlying
short/byte/bit, returns it as the value.

`GCL_SetVar` is called by `GCL_Expr` for ASSIGN. Decodes the
variable destination, writes the value, advances the pointer.

## Save / load

```c
int  GCL_MakeSaveFile(char *saveBuf);
int  GCL_SetLoadFile(char *saveBuf);
void GCL_SaveVar(void);
void GCL_RestoreVar(void);
unsigned char *GCL_VarSaveBuffer(unsigned char *top);
```

`GCL_MakeSaveFile` snapshots the entire `var[1024]` into a buffer
that goes into the memcard save block. `GCL_SetLoadFile` is the
inverse: take the save buffer and restore variable state.

`GCL_SaveVar` / `GCL_RestoreVar` is a *temporary* in-memory snapshot
(used during a "savepoint" mechanic where the game holds the state
and the player can rewind on death — the snapshot lives in resident
memory).

## Init

```c
void GCL_InitVar(void);          // clear all
void GCL_InitClearVar(void);     // clear non-persistent vars only
```

Persistent vars survive `InitClearVar` (they're flagged by the
script via a separate metadata block). This is how
"chapter completion" flags persist while per-stage flags reset.

## Pitfalls

- **$w / $b / $f overlap.** Writing $b:1 corrupts the high byte of
  $w:0. Scripts must agree on which view owns which offset.
- **No bounds checking.** `var[1024]` index is implicit; an offset
  ≥ 1024 reads/writes garbage.
- **ASSIGN returns the value.** Like C; chained assigns like
  `$w:1 = $w:2 = 5` work — both vars get 5.
- **Comparison results are 0/1, not C's boolean.** `(x == y) + 1`
  gives 1 or 2, never anything else.

## Port notes

The port replaces the memcard save with a flat file
([`port/memcard/`](../../../../port/memcard/) shim), but the GCL
serialisation format is unchanged.

## See also

- [bytecode.md](bytecode.md) — operator and variable opcodes.
- [parse.md](parse.md) — `GCL_GetNextValue` calls into here for
  variable reads.
- [`source/memcard/`](../memcard/README.md) — save/load surface.
