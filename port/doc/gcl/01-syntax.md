# GCL Text Syntax

This is the syntax our decompiler emits and our compiler accepts (see
[port/gcl_tools/](../../../port/gcl_tools/)). It is **not** the syntax
Konami used in 1998 — that tool is lost. But it maps 1:1 with the
runtime's bytecode so anything you write here compiles to an exact
`.gcx` the stock runtime can execute.

## File layout

```gcl
# `#` starts a line comment. Comments are ignored by the parser.

proc sub_XXXX {          # zero or more proc definitions
    ...statements...
}

proc sub_YYYY {
    ...
}

script {                 # exactly one `script` block (the main body)
    ...statements...
}
```

Procs are named `sub_XXXX` where `XXXX` is the 4-hex `GV_StrCode()` hash
of the original name. The actual source name was lost at compile time;
the decompiler can only recover the hash.

## Statements

Every statement is either:

| Form               | What it is                             |
|--------------------|----------------------------------------|
| `eval(expr)`       | Evaluate an expression (usually assignment) |
| `call(sub_XXXX, ...)` | Invoke a proc, optional args         |
| `if (expr) { ... } elseif (expr) { ... } else { ... }` | Control flow |
| `cmd args... -x val -y val ...` | Invoke any of the 28 commands |

Commands may span multiple lines via trailing `\\`:

```gcl
load "s00a" \
    -m $s:7df9
    -s 1
```

The backslash isn't required — subsequent lines that start with `-X`
(an option) are treated as continuations of the current command.

## Literals

| Example       | Type        | Encoded as   |
|---------------|-------------|--------------|
| `42`          | WORD        | signed 16-bit (`0x01 ??`) |
| `-1`          | WORD        | signed 16-bit (decompiler emits `-N` for negative WORD literals) |
| `0xff00`      | WORD        | hex decimal              |
| `b:7`         | BYTE        | 1 byte (`0x02 ??`)       |
| `true` / `false` | FLAG     | 1 byte boolean (`0x04 01`/`0x04 00`) |
| `'x'`         | CHAR        | 1 byte ASCII (`0x03 ??`) |
| `"ascii"`     | STRING      | length-prefixed ASCII (`0x07 LEN ...`) |
| `m"..."`      | STRING      | MGS-encoded (ASCII chars + `{XXXX}` for 2-byte codes + mapped chars from [mgs_chars.py](../../gcl_tools/mgs_chars.py)) |
| `sd:01010855` | SD_CODE     | 4-byte opaque (`0x09 XX XX XX XX`) |
| `t:00005678`  | TABLE       | 4-byte reference to VOX/DMO/RADIO.DAT (`0x0A XX XX XX XX`) |
| `$s:abcd`     | STR_ID      | 2-byte hashed name (`0x06 AB CD`) — see [02-variables.md](02-variables.md#str_id-literals) |

Negative values in expression context use `-(N)` parens to force the
runtime's `NEGATE` operator form (different bytecode than a direct
negative WORD — both valid, not interchangeable). See
[03-expressions.md](03-expressions.md#unary-negation).

## Strings

Two flavours:

- `"..."` — plain ASCII. Embedded `\xNN` hex escapes allowed for non-
  printable bytes. Use for stage names, file names, debug prints.
- `m"..."` — MGS-encoded text. Mixes ASCII (1-byte), mapped Unicode
  chars from the table, and `{XXXX}` markers for unresolved 2-byte
  codes. Use for dialogue, menu labels, subtitle strings.

Both round-trip through the binary format exactly — `\xNN` escapes are
emitted whenever a byte can't safely sit in a quoted literal.

## Variables

Sigil-prefixed hex addresses. Size is indicated by the sigil letter:

```
$w:800010         # WORD (2 bytes) at offset 0x800010
$b:000049         # BYTE (1 byte)
$f:000001         # FLAG (bit)
$s:80000E         # STR_ID variable (hashed-string cell, 2 bytes)
```

The 6-hex address encodes both the scope (game-state vs local) and the
offset. Full details in [02-variables.md](02-variables.md).

## Proc references and call

```gcl
call(sub_DF2A)                    # no args
call(sub_1D3C, 5, $b:000049)      # two args
```

A bare `sub_XXXX` in option position (e.g. `trap -e sub_XXXX`) is also
a proc reference — used by `chara`, `trap`, `ntrap`, `delay` to
register a handler that runs later.

## Options ("dash flags")

Commands take positional args followed by zero or more options. Each
option is `-<letter>` plus its own positional values:

```gcl
trap $s:8f4c $s:21ca $s:14c9 \
    -m $s:14c9        # mask option (maps to "mask" in the source)
    -c                # valueless flag (some options are just -X alone)
```

One special form: `-!X` (note the `!`) marks a "NULL_SIZE" option
where the encoded size byte is zero — a format detail the decompiler
surfaces verbatim so round-trip stays byte-exact. You almost never
need to write this by hand.

## A minimal but complete file

```gcl
proc sub_abcd {
    eval($f:000001 = false)
    load "title" -m $s:7df9 -s 1
}

script {
    pad -s
    eval($f:000001 = true)
    call(sub_abcd)
}
```

Recompile with [gcl2gcx.py](../../gcl_tools/gcl2gcx.py):

```
python3 gcl2gcx.py mini.gcl -o mini.gcx --no-align4
```

Drop it into `port/overlays/<stage>/scenerio.gcx` and the stage's
scenerio script will execute your code instead of the disc version.
