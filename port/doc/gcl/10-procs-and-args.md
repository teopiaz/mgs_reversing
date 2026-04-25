# Procedures and Arguments

How procs are defined, called, and how stack arguments flow.

Runtime: [source/libgcl/command.c](../../../source/libgcl/command.c) —
`GCL_ExecProc`, `GCL_ForceExecProc`, `set_proc_table`.

---

## Defining a proc

```gcl
proc sub_8CD4 {
    eval($b:000000 = $b:000000 + 1 | 128)
}
```

The name `sub_<XXXX>` is the original Konami name's `GV_StrCode()`
hash — the source identifier was lost at compile time. You can rename
freely in your overlay; the compiler reads the trailing 4 hex digits
and emits that hash into the proc table.

A proc may contain any statements (commands, `if`, nested `call`,
`eval`). It cannot contain another `proc` definition — procs are
flat at the file level.

---

## Calling a proc

```gcl
call(sub_8CD4)                      # no args
call(sub_handler, 5)                # 1 arg
call(sub_handler, $b:000049, 7)     # 2 args
call(sub_handler, $s:21ca, $s:0e4e, $s:14c9)   # 3 args (mesg-style)
```

Stack args are pushed left-to-right and read inside the callee as
`arg1`, `arg2`, …, `arg8`. Up to 8 args (the runtime emits
`TOO MANY ARGS PROC` if you exceed).

---

## Reading args inside a proc

```gcl
proc sub_door_open {                # arg1 = door id, arg2 = sound code
    mesg arg1 $s:0e4e               # tell the door to open
    sound -x arg2                   # play the SFX
    eval($f:040002 = true)          # mark global "first door opened"
}

call(sub_door_open, $s:c776, sd:01010001)
call(sub_door_open, $s:c777, sd:01010001)
call(sub_door_open, $s:c778, sd:01010002)
```

`argN` works inside any expression — comparisons, assignments, command
args:

```gcl
proc sub_check_dist {               # arg1 = threshold
    if ($b:000000 - arg1 + 128 & 127 >= $w:000410) {
        ...
    }
}
```

---

## Trap stack args

When a `trap` body fires, the runtime pushes a fixed triplet:

```
arg1 = subject hash (the actor that triggered the zone)
arg2 = zone hash    (which zone matched)
arg3 = event hash   (which event fired)
```

That lets one body handle wildcarded variants. From
[s00a/scenerio.gcl](../../gcl/decompiled/s00a/scenerio.gcl):

```gcl
trap $s:6373 $s:50ae $s:14c9 {       # event = wildcard
    if (arg3 == $s:0dd2) {
        mesg $s:18e3 $s:c927          # snow off
    } elseif (arg3 == $s:d5cc) {
        mesg $s:18e3 $s:0e4e          # snow on
    }
}
```

Inside `ntrap`, the same `arg1..arg3` are populated. Inside
`delay -e proc`, only `arg1` is set (to whatever was pushed by the
caller, or zero if none).

Inside `chara -e proc`, args are similarly pushed by the chara
factory's event-fire path.

---

## What a proc returns

GCL has no explicit return value. The `return` command (rare in
vanilla — 0 sites) just stops the current block:

```gcl
proc sub_early_exit {
    if ($f:000099) {
        return        # equivalent to "stop executing this block"
    }
    ...rest...
}
```

This exits the proc's `script` block. The caller continues at the
statement after `call(...)`. It's mostly used for early-exit guard
patterns.

For "passing data back", use a global variable:

```gcl
proc sub_compute {
    eval($w:000200 = $w:000202 * 3 + arg1)
}

call(sub_compute, 10)
if ($w:000200 > 100) { ... }
```

---

## Recursion and re-entry

A proc may call itself or call back into a proc that's already on
the stack. The runtime maintains an args stack
(`argstack_p` in [parse.c](../../../source/libgcl/parse.c#L7)) that
saves/restores around each `GCL_ExecProc`.

But: depth is limited by the scratchpad/stack — there's an
`argbuffer[32]` shared store, so deep recursion can corrupt other
state. Vanilla scripts don't recurse.

Wrapping mutual recursion through `delay -t 1 -e ...` works around
this — each delay slot is a fresh actor with its own stack frame.

---

## Proc table layout in bytecode

Each `.gcx` starts with a `(proc_id, offset)` table:

```
proc_table:
    0xDF2A → offset 0           sub_DF2A starts at proc-region byte 0
    0x8CD4 → offset 0x33         sub_8CD4 starts at proc-region byte 0x33
    0x1D3C → offset 0x4E         sub_1D3C starts at proc-region byte 0x4E
    0x0000 → 0x0000              ← terminator
    [proc bodies packed...]
```

- The order of entries reflects the order of `proc` blocks in the
  source file.
- IDs must be unique within a file.
- Lookup is linear — `get_proc_block()` in
  [command.c:86](../../../source/libgcl/command.c#L86) just walks the
  table.

---

## Proc visibility / scope

All procs in a `.gcx` are visible to each other. There's no module
system, no imports, no public/private distinction.

You can't call a proc defined in another `.gcx` directly — proc lookup
only consults the **current_script** state (set by the last
`GCL_LoadScript`).

To "share" code between scenerio.gcx and demo.gcx of the same stage,
you must duplicate the proc into both. The corpus does exactly this —
e.g. `sub_8CD4` (counter increment) appears verbatim in nearly every
file.

---

## Proc patterns from the corpus

### Setup proc + script entry

```gcl
proc sub_DF2A {                     # "go to title"
    eval($f:000001 = false)
    load "title" -m $s:7df9 -s 1
}

proc sub_8CD4 {                     # "stage counter ++"
    eval($b:000000 = $b:000000 + 1 | 128)
}

proc sub_init_lights {              # standard 3-light setup
    light -d 0 -1 0
    light -c 35 35 35
    light -a 51 51 51
}

script {
    call(sub_init_lights)
    ...
}
```

### Trap dispatcher

```gcl
proc sub_door_dispatch {            # arg1 = door hash
    eval($f:040002 = true)
    mesg arg1 $s:0e4e
    sound -x sd:01010001
}

trap $s:c776 $s:21ca $s:0dd2 { call(sub_door_dispatch, $s:c776) }
trap $s:c777 $s:21ca $s:0dd2 { call(sub_door_dispatch, $s:c777) }
trap $s:c778 $s:21ca $s:0dd2 { call(sub_door_dispatch, $s:c778) }
```

### Multi-stage handler

```gcl
proc sub_difficulty_init {
    if ($w:80007A == 1) {                # easy
        eval($w:000410 = 5)               # more health, etc.
        eval($f:dynamic_check = false)
    } elseif ($w:80007A == 2) {          # normal
        eval($w:000410 = 7)
        eval($f:dynamic_check = true)
    } else {                              # hard / extreme
        eval($w:000410 = 10)
        eval($f:dynamic_check = true)
    }
}
```

### Per-frame poll via `delay`

You can simulate "tick every 30 frames" with self-rescheduling:

```gcl
proc sub_tick {
    eval($w:tick_counter = $w:tick_counter + 1)
    if ($f:running) {
        delay -t 30 -e sub_tick     # reschedule
    }
}

script {
    eval($f:running = true)
    delay -t 30 -e sub_tick
}
```

The cost is one delay actor per cycle, but it's how the engine
handles all timed events anyway.

---

## Argv passing across complex commands

`mesg` is the canonical "send args to actor" pipe:

```gcl
mesg arg1 $s:0e4e              # send single-payload "on" to subject
mesg arg1 $s:14c9 arg3         # forward our event hash via mesg payload
mesg $s:21ca $s:9d00 1024 ...  # snake gets: id 0x9d00, then 1024
```

The destination actor's message handler reads the payload from a
fixed array. Receivers are C functions, not GCL procs, so the GCL
side just packs the args and the C side knows the layout per
message-type.

For details, see [12-mesg-protocol.md](12-mesg-protocol.md).

---

## Pitfall: trash `argc/argv` for `chara`

`GM_Command_chara` has the same C signature as other handlers but is
called through a cast that misroutes the first 12 bytes after the
command hash as `argc` and `argv`. The decompiled comment in
[script.c:1144](../../../source/game/script.c#L1144) is candid:

> ```c
> // TODO: Why does this one have a different signature?
> // Putting a breakpoint GM_Command_chara shows it receives
> // trash argc and argv.
> ```

That trash trickles into the `NEWCHARA` factories' fourth/fifth args:

```c
void *NewSnake(int name, int where, int argc, char **argv);
```

`argc/argv` here are garbage. Most factories ignore them. If you're
writing a custom factory in C, do **not** read `argc/argv` — instead
use `GCL_GetOption('x')` etc. inside the chara command's option
parsing.
