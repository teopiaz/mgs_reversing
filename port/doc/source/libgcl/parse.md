---
file: source/libgcl/parse.c
---

# `libgcl/parse.c` — value reader + arg/option helpers

The bytecode walker — given a script pointer, decode the next value,
return its type and value. Also implements the per-command argument
stack and the GCL `-x` option lookup.

## Core: `GCL_GetNextValue`

```c
unsigned char *GCL_GetNextValue(unsigned char *top, int *type_p, intptr_t *value_p);
```

Reads one bytecode value from `top`, writes its type into `*type_p`
and decoded value into `*value_p`, returns the advanced pointer.

Switches on the leading byte:

| Code | Behaviour |
| ---- | --------- |
| NULL (0) | returns NULL pointer to signal "end of args" |
| SHORT | read 16-bit BE → value |
| BYTE / CHAR / FLAG | read 8-bit → value |
| HASHED_STRING / PROC_CALL | read 16-bit hash → value |
| SDCODE / TABLE_CODE | read 32-bit BE → value |
| STRING | length byte then bytes; value = pointer to string |
| STACK_VAR | one byte = arg index; value = stack lookup |
| SCRIPT_DATA | length-prefixed payload (used for raw blobs) |
| EXPRESSION | recurse into `GCL_Expr` |
| PARAMETER | the `-x` form: high byte = char, low byte = length |
| 0x10..0x18 | variable ref → defer to `GCL_GetVar` |

After a successful read, `next_str_ptr` is advanced past the value —
so chained reads work without explicit cursor management.

## Argument stack

When a proc is called with arguments, GCL pushes the args onto a
shared stack, which the callee reads via `GCL_GetArgs(idx)`:

```c
int *GCL_SetArgStack(GCL_ARGS *args);     // push args, return old stack ptr
void GCL_UnsetArgStack(void *stack);       // restore old stack ptr
int  GCL_GetArgs(int argno);                // read arg by index (negative-index trick)
```

The negative-index trick: `argstack_p[~argno]` (= `argstack_p[-argno-1]`)
goes back from the *top* of the pushed args. Pushed args are stored
**reversed** (high arg first), so `~0 = -1` is the bottom = arg 0.
Slick and cache-friendly but fragile.

## Command-line stack

Each `command` invocation has its own *parameter line* — the part
after the command name. Parsers like `GCL_GetOption` need to know
which command's argv they're scanning, so there's a stack of these:

```c
extern unsigned char *commandlines[8];
static unsigned char **commandline_p;     // top of stack
```

```c
void GCL_SetCommandLine(unsigned char *argtop);    // push
void GCL_UnsetCommandLine(void);                    // pop
```

A command callback runs with `commandline_p[-1]` pointing to its
own argv. `GCL_GetOption` reads from there.

## `GCL_GetOption(c)` — find `-c <value>`

The classic CLI-style switch lookup:

```c
char *GCL_GetOption(char c) {
    pScript = current commandline;
    do {
        pScript = GCL_GetNextValue(pScript, &code, &value);
        if (code == NULL) return NULL;       // end of args
    } while (!IsParam(code) || (code >> 16 != c));
    next_str_ptr = value;
    return value;
}
```

So `GCL_GetOption('p')` walks the args looking for a `PARAMETER`
record whose char-byte matches `'p'`. Returns the value (= pointer
to the parameter's payload). The caller then typically chains with
`GCL_GetParamResult` to read individual values within a multi-value
parameter (e.g. `-p (1000, 0, -1500)` is three GCL values inside one
parameter slot).

## Helpers built on `GCL_GetNextValue`

```c
int  GCL_StrToInt(unsigned char *p);         // read one value as int
int  GCL_StrToSV(unsigned char *p, SVECTOR *out);   // read 3 values into vec
char *GCL_ReadString(char *p);                // read string, return char *
unsigned char *GCL_GetParamResult(void);      // next value if not param-end
int  GCL_GetNextParamValue(void);              // GCL_StrToInt + advance
void GCL_ReadParamVector(SVECTOR *out);
```

These all mutate `next_str_ptr` so the caller can chain without
tracking offsets.

## Init

```c
void GCL_ParseInit(void) {
    InitArgStack();
    InitCommandLineBuffer();
}
```

Called once at boot.

## Pitfalls

- **`GCL_GetOption` is destructive.** It moves `next_str_ptr` to the
  matched param's payload — so if you call it twice in a row for
  the same option, the second invocation may resume from the
  wrong place.
- **Stack depth limits.** `argbuffer[32]` and `commandlines[8]` are
  hard-coded. Deeply nested proc calls or commands can overflow
  silently.
- **`STACK_VAR` re-types as 1.** Inside `GCL_GetNextValue`, reading
  a `STACK_VAR` overrides `*type_p = 1` (= SHORT). Caller treats it
  as a SHORT literal regardless of actual stack value.
- **Endianness everywhere.** Always go through `GCL_GetShort` /
  `GCL_GetLong`. Do not read u16/u32 directly from the bytecode.

## See also

- [bytecode.md](bytecode.md) — opcode listings.
- [command.md](command.md) — `GCL_Command` calls
  `GCL_SetCommandLine` to pin the argv before dispatching.
- [variable.md](variable.md) — `GCL_GetVar` is invoked from
  `GCL_GetNextValue` for variable codes.
