---
file: source/libgcl/command.c
---

# `libgcl/command.c` — command + proc dispatcher

The script executor. Walks the bytecode block, dispatches commands
and procs, handles control flow.

## Command registration

```c
typedef struct GCL_COMMANDLIST {
    unsigned short  id;             // strcode hash of command name
    GCL_COMMANDFUNC function;        // int(*)(unsigned char *args)
} GCL_COMMANDLIST;

typedef struct _gcl_commanddef {
    struct _gcl_commanddef *next;
    int                     n_commlist;
    GCL_COMMANDLIST        *commlist;
} GCL_COMMANDDEF;

int GCL_AddCommMulti(GCL_COMMANDDEF *def);
```

Multiple subsystems each contribute a `GCL_COMMANDDEF` table. They
all chain into `commdef` (a singly-linked list of tables). Lookup
walks every table.

```
commdef → basic_commands → game/script_commands → demo_commands → NULL
```

`FindCommand(id)` linear-scans through all tables. Slow but only
runs when a command is invoked (rare — once per script statement).

## Dispatcher: `GCL_Command`

```c
int GCL_Command(unsigned char *ptr) {
    cmd = FindCommand(ptr.short[0]);              // hash lookup
    advance(ptr, 2);
    SetCommandLine(ptr + ptr.byte[0]);             // commandline = end of arg block
    advance(ptr, 1);
    SetArgTop(ptr);                                // next_str_ptr = arg start
    ret = cmd->function(ptr);                      // run the command
    UnsetCommandLine();
    return ret;
}
```

The command callback (e.g. `GM_ScriptCharaCommand` for `chara`)
reads its args via `GCL_GetOption` / `GCL_StrToInt` etc.

## Proc table

Procs are *named subroutines*. A `.gcx` file's proc table maps
proc-id (16-bit hash) to byte offset within `proc_body`:

```c
typedef struct {
    unsigned short proc_id;    // strcode hash of proc name
    unsigned short offset;     // offset into proc_body
} GCL_PROC_TABLE;
```

`set_proc_table(table)` byte-swaps the (BE-stored) entries in place
and returns the address just past the table — that's where
`proc_body` starts.

`get_proc_block(proc_id)` linear-scans the table for a match,
returns pointer into `proc_body` at the right offset. Linear because
the table is short (< 100 entries per .gcx).

## Proc invocation

```c
GCL_ForceExecProc(proc_id, args);    // unconditional
GCL_ExecProc(proc_id, args);         // skipped during load / game-over
```

Implementation:

```c
int GCL_ExecProc(int proc_id, GCL_ARGS *args) {
    if (GM_LoadRequest || (GM_PlayerStatus & PLAYER_GAME_OVER))
        return 0;     // suppress during transitions
    return GCL_ExecBlock(get_proc_block(proc_id) + 3, args);
}
```

The `+3` skips the proc's leading marker bytes (length-prefix +
opcode tag). `args` is pushed onto the arg stack for the callee's
`STACK_VAR` reads.

## Inline proc call: `GCL_Proc`

When a script's body contains a `PROC` opcode (0x70), `GCL_Proc`
runs:

```c
GCL_Proc(ptr):
    proc_id = read 16-bit
    while (read next value) → push into argbuf[8]
    GCL_ExecProc(proc_id, &args)
```

So scripts can call procs with arguments inline:

```
&MyProc(42, &SOME_HASH, $w:7)
```

becomes a `PROC` opcode followed by the proc-id and three values.

## The execution loop: `GCL_ExecBlock`

The heart of the interpreter:

```c
int GCL_ExecBlock(unsigned char *top, GCL_ARGS *args) {
    old_stack = SetArgStack(args);
    while (top) {
        switch (*top) {
        case GCLCODE_EXPRESSION:    // 0x30
            GCL_Expr(top+2, &result);
            top += 1 + top[1];      // skip past the expression length
            break;
        case GCLCODE_COMMAND:        // 0x60
            if (GCL_Command(top+3) == 1) return 1;   // 1 = "yield" (e.g. wait)
            top += 1 + ((short)GCL_MakeShort(top[2], top[1]));
            break;
        case GCLCODE_PROC:           // 0x70
            GCL_Proc(top+2);
            top += 1 + top[1];
            break;
        case GCLCODE_NULL:           // end
            UnsetArgStack(old_stack);
            return 0;
        default:
            error;
        }
    }
}
```

The instruction layout has a leading length byte/word so the
interpreter can skip the entire instruction and continue regardless
of nesting.

### Return value semantics

- **0** = block ended normally (NULL hit) — proc returns to caller.
- **1** = command requested *yield* (e.g. `wait` says "resume next
  frame"). Propagates up through any number of nested blocks until
  it reaches the outer scheduler.

This is how `wait 30` works: the `wait` command sets a counter and
returns 1; `GCL_ExecBlock` sees the 1 and returns 1; the daemon
sees the 1 and re-invokes the same proc next frame, picking up
where it left off.

## Top-level boot

```c
void GCL_ExecScript(void) {
    if (*current_script.script_body != 0x40)    // expect SCRIPT_DATA
        printf("NOT SCRIPT DATA !!\n");
    GCL_ExecBlock(script_body + 3, &gcl_null_args);
}
```

Called by `gamed.c` after `GCL_LoadScript` succeeds. The boot script
typically:

1. Sets up persistent variables.
2. Spawns initial actors via `chara &WATCHER`, `chara &SNAKE`, etc.
3. Sets initial camera.
4. Returns.

After return, the actors run on their own (each is an actor in the
GV system). Only `wait`-style commands need to keep GCL ticking.

## Pitfalls

- **Length prefix is 1 byte for COMMAND, 1 word for PROC.** Reading
  the wrong width throws off the cursor by 1 byte and corrupts the
  rest of the script.
- **Don't call `GCL_ExecBlock` recursively from the same arg stack.**
  `SetArgStack` saves the old top; recursion is fine if you nest
  push/pop pairs, but mistaken nesting clobbers args.
- **Proc-not-found prints and returns NULL block.** The interpreter
  doesn't crash on missing proc — it `printf`s and continues —
  but the calling expression evaluates to whatever was before. Watch
  for silent script bugs.

## See also

- [bytecode.md](bytecode.md) — opcode reference.
- [parse.md](parse.md) — `GCL_GetNextValue` is the byte-level reader.
- [expr.md](expr.md) — the expression evaluator.
- [`source/game/script.c`](../../../../source/game/script.c) — the
  game-tier command set built on this library.
