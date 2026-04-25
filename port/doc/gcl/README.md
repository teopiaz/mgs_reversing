# GCL — Game Command Language

MGS1's per-stage scripting language. Every stage `.gcx` file is a GCL
program that the runtime loads, parses and runs on stage entry.

## What's in here

| File                                | Contents                                              |
|-------------------------------------|-------------------------------------------------------|
| [01-syntax.md](01-syntax.md)        | Text syntax (our `.gcl` dialect): procs, blocks, options, literals |
| [02-variables.md](02-variables.md)  | Variable storage classes, save-persistence, game-state flags       |
| [03-expressions.md](03-expressions.md) | Operators, precedence, unary/binary forms                       |
| [04-commands.md](04-commands.md)    | All 28 commands, each with signature + real examples              |
| [05-cookbook.md](05-cookbook.md)    | Annotated walk-throughs of real scenarios                         |
| [06-binary-format.md](06-binary-format.md) | `.gcx` bytecode layout (for tool writers)                  |

## Quick mental model

```
                 .gcl text              .gcx bytecode
               (authored / tooled)       (what the runtime loads)
 ┌──────────┐   ┌────────────┐           ┌───────────────┐   ┌─────────┐
 │ ideas    │──▶│ gcl2gcx.py │──────────▶│ cache buffer  │──▶│ runtime │
 └──────────┘   └────────────┘           └───────────────┘   └─────────┘
                      ▲                        │
                      │   gcx2gcl.py           │   GCL_ExecScript()
                      └────────────────────────┘
```

- The full runtime lives in [source/libgcl/](../../../source/libgcl/) and
  the stage-specific commands in
  [source/game/script.c:1141](../../../source/game/script.c#L1141).
- The text tooling (decompile / compile / inspect) lives in
  [port/gcl_tools/](../../../port/gcl_tools/).
- Vanilla decompiled corpus: 135 `.gcx` files → [port/gcl/decompiled/](../../gcl/decompiled/),
  plus per-stage font blobs next to each.

## A 15-second tour

```gcl
# Every file has: zero or more procs, then one main `script` block.

proc sub_8CD4 {                             # proc name is hex of GV_StrCode()
    eval($b:000000 = $b:000000 + 1 | 128)   # variable assignment
}

script {
    call(sub_8CD4)                          # run a proc
    mapdef $s:7df9 -k $s:c681 $s:6da4 ...   # declare a map
    chara $s:21ca $s:21ca                   # spawn "snake"
    trap $s:8f4c $s:21ca $s:14c9 {          # event handler
        mesg arg1 $s:0e4e
    }
    pad -s                                  # hand input to player
}
```

Everything else is elaboration on this: more sigils for different variable
types, more options per command, more kinds of literals, and how the
runtime dispatches the hashed 2-byte commands back to C handlers.
