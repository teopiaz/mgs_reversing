---
file: source/libgcl/ — opaque areas
---

# `libgcl/` — what's still by-address / opaque

`libgcl/` is fully decompiled and matches PSX byte-for-byte. The
bytecode format is now formally documented in
[`tools/mgs_tools/gcl/`](../../../../tools/) (the Python compiler).
Remaining ambiguities:

## SDCODE vs TABLE_CODE

```c
GCLCODE_SDCODE       9    // 32-bit constant
GCLCODE_TABLE_CODE   10   // table reference
```

Both decode the same way (read 32-bit BE). The semantic difference —
`SDCODE` is a generic 32-bit literal, `TABLE_CODE` is specifically a
pointer into VOX/DMO/RADIO table — is observable at compile time but
the runtime treats them identically. Could be merged.

## STACK_VAR retype

`GCL_GetNextValue` returns `*type_p = 1` for `STACK_VAR` (overriding
the actual `GCLCODE_STACK_VAR = 0x20`). This forces callers to treat
it as a SHORT. Why: stack vars are always int-sized, so the type
"hint" was simplified. The original intent might have been to allow
typed stack vars later.

## Per-script string-data layout

`script_body + GCL_GetLong(tmp) + sizeof(int)` is passed to
`font_set_font_addr(2, ...)`. Implies that .gcx files include their
own font glyph table in the trailing string-data region — but the
exact glyph encoding is not formally documented.

## Implicit yield semantics

`GCL_Command` returning 1 means "yield, re-enter this proc next
frame". Mechanism is clear but the *which command should yield*
specification is per-command (each command's body returns the right
value). No central registry — auditing all commands for correct
return value is a manual exercise.

## Var encoding bit layout

`GCL_GetFlagBitFlag(v) = 1 << ((v << 1) >> 17 & 0xF)`. The 4-bit
bit-selector field is at bits 17..20 of the encoded `v`. The
compiler in `tools/mgs_tools/gcl/` knows this; the runtime decodes
it. There's no header documenting the full bit layout — the inline
macros in `libgcl.h` are the only spec.

## Game-state ($vs) semantics

`GCL_IsGameStateVar(v)` checks `(v & 0xF00000) == 0x800000`. The
`$vs:` namespace is implemented via a separate function-pointer
table populated at runtime. The full set of registered $vs slots
isn't documented in one place — discovered by grepping for
`GCL_SaveLinkVar`.

## Argbuffer / commandlines depth

`argbuffer[32]` and `commandlines[8]` are static. Hitting overflow
is silent. No assertion.

## The $sd: / $sn: prefixes (if any)

GCL source can use `$sn:`, `$sa:` and similar prefixes that compile
into different opcodes. Coverage of these in the port's tooling is
partial; some are present but not formally documented.

## See also

- [index.md](index.md), [bytecode.md](bytecode.md), [parse.md](parse.md),
  [command.md](command.md), [expr.md](expr.md) — documented surfaces.
- `tools/mgs_tools/gcl/constants.py` — the canonical opcode list.
