# Expressions

GCL expressions live inside `eval(...)`, inside `if (...)` / `elseif (...)`
conditions, and inside some option values like `ntrap -e (...)`.

Runtime evaluator: [source/libgcl/expr.c](../../../source/libgcl/expr.c).
It's a tiny RPN VM using the PSX scratchpad as its stack.

## Operators and precedence

Lowest precedence (loosest) at the top. Operators at the same level
are left-associative except `=` (right-associative).

| Level | Operators          | Example                |
|-------|--------------------|------------------------|
| 1     | `=`                | `$b:000049 = 7`        |
| 2     | `\|\|`             | `$f:a \|\| $f:b`       |
| 3     | `&&`               | `cond1 && cond2`       |
| 4     | `\|`               | `mask \| 0x80`         |
| 5     | `^`                | `a ^ b`                |
| 6     | `&`                | `flags & 0xff`         |
| 7     | `==` `!=`          | `$w:80007A == 3`       |
| 8     | `<` `<=` `>` `>=`  | `$w:80006E <= 60`      |
| 9     | `+` `-`            | `$b:000000 + 1`        |
| 10    | `*` `/` `%`        | `(n * 3) / 2`          |
| 11 (unary) | `-` `!` `~`   | `!$f:000001`, `-(42)`  |

No dedicated logical-NOT-on-int-truthiness distinct from `!` — `!0` is
`1`, `!<anything else>` is `0`.

Parentheses work the usual way for grouping.

## Assignment

Only valid as the top of an `eval(...)`. Returns the value assigned so
chained forms can work, but they're uncommon.

```gcl
eval($w:800010 = -10000)
eval($f:000001 = false)
eval($b:000000 = $b:000000 + 1 | 128)   # precedence: `+` before `|`, so this is
                                        # ($b:000000 + 1) | 128
```

## Unary negation (gotcha)

Two encodings in the bytecode, both valid:

- **Negative WORD literal**: `01 FF FE` → WORD -2
- **NEGATE op**: `02 00  01 00 02  31 01` → push 0, push 2, apply unary negate

Our text format distinguishes them:

- `-N` (no parens) → negative WORD literal (shorter, used in most places)
- `-(N)` or `-expr` → NEGATE op (required when the runtime evaluator
  must compute the value, e.g. `-$w:800010`)

Why the distinction matters: byte-exact round-trip with the vanilla
scripts needs both forms. The decompiler emits whichever the original
bytecode used; if you hand-write, use `-N` for constants and `-expr`
(parenthesised) for dynamic negations.

```gcl
eval($w:800010 = -10000)        # fine — negative WORD literal
eval($w:800010 = -($w:800012))  # NEGATE op — required, $w:... isn't a constant
```

## Expression examples from the corpus

Comparisons with ranges (`s02a/scenerio.gcl`):

```gcl
if (b:20 < $w:80006E && $w:80006E <= b:60 && !($f:050001)) {
    ...
}
```

Bit flags (`init/scenerio.gcl`):

```gcl
eval($b:000000 = $b:000000 + 1 | 128)   # increment + set bit 7
if ($b:000000 & 128) {
    ...
}
```

Arithmetic on stack args (`s01a/scenerio.gcl`):

```gcl
if (($b:000000 - arg1 + 128 & 127) >= $w:000410) {
    ...
}
```

Chained comparisons using `&&` (`d00a/scenerio.gcl`):

```gcl
if (arg1 == 0 && arg2 != 0 && $w:800002 == 3) {
    call(sub_something)
}
```

## Truthiness

Conditions are "truthy" when non-zero. FLAG variables read back as 0
or 1. WORD comparisons evaluate to 0 or 1. `&&` / `||` short-circuit
(the runtime actually still evaluates both sides — the RPN VM doesn't
short-circuit — but the result matches C's semantics because both
sides are always pure reads).

## What expressions can't do

- No string operators (no `strcmp`, no concatenation — strings are
  opaque `char *` pointers in the VM).
- No calls inside an expression (`call(sub)` is a statement, not an
  operand).
- No pointer arithmetic.
- The stack has a fixed depth (1 KB at `SCRPAD_ADDR + 0x200` on PSX) —
  deeply-nested expressions with many operands would overflow, but
  vanilla scripts stay well under.
