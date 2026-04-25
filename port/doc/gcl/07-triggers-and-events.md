# Triggers and Events

How `trap`, `ntrap`, and `mesg` plug into the HZD zone system to react
to player actions.

Runtime entry points:
- [source/libhzd/event.c](../../../source/libhzd/event.c) — trigger dispatch
- [source/game/script.c:200 GM_Command_trap](../../../source/game/script.c#L200) — registration
- [source/game/script.c:299 GM_Command_ntrap](../../../source/game/script.c#L299)

---

## The triple: `(zone, subject, event)`

Every `trap` registers a 3-tuple:

```gcl
trap <zone> <subject> <event> { body }
```

| Slot     | What it is                | Examples                           |
|----------|---------------------------|------------------------------------|
| zone     | A hashed HZD zone name    | `$s:8f4c`, `$s:6373` (level-specific) |
| subject  | Who has to be in the zone | `$s:21ca` ("snake")                |
| event    | What action triggers      | `$s:0dd2` (enter), `$s:d5cc` (leave) |

The HZD file (`*.hzd`, loaded by `mapdef -h`) defines zones as 3D
volumes within the map. Each frame the engine asks "is `subject`
inside `zone`?", and "did transition `event` happen?". When yes, the
matching trap fires.

Wildcards: pass `$s:14c9` (`HASH_TRAP_ALL` = `GV_StrCode("？")`) for
any of the three slots to mean "match anything".

---

## Standard event hashes

From [source/include/strcode.h:53](../../../source/include/strcode.h#L53).
These are the literals you put in the `event` slot.

| Sigil          | Hash     | Source           | Meaning                          |
|----------------|----------|------------------|----------------------------------|
| `$s:14c9`      | `0x14c9` | `？`             | wildcard — any event             |
| `$s:0dd2`      | `0x0dd2` | `入る`           | subject ENTERS the zone          |
| `$s:d5cc`      | `0xd5cc` | `出る`           | subject LEAVES the zone          |
| `$s:1a19`      | `0x1a19` | `leave`          | leave (alt spelling)             |
| `$s:3223`      | `0x3223` | `kill`           | subject killed                   |
| `$s:c927`      | `0xc927` | `off`            | toggle off                       |
| `$s:0e4e`      | `0x0e4e` | `on`             | toggle on                        |
| `$s:006b`      | `0x006b` | `ＯＦＦ`         | off (fullwidth)                  |
| `$s:d182`      | `0xd182` | `ＯＮ`           | on (fullwidth)                   |
| `$s:2580`      | `0x2580` | `padon`          | pad input enabled                |
| `$s:af6a`      | `0xaf6a` | `padoff`         | pad input disabled               |
| `$s:3e92`      | `0x3e92` | `slow`           | slow-motion mode                 |
| `$s:2761`      | `0x2761` | `音入れる`       | sound enabled                    |
| `$s:ed7f`      | `0xed7f` | `音切る`         | sound disabled                   |
| `$s:8012`      | `0x8012` | `tabako`         | cigarette equipped               |
| `$s:62b6`      | `0x62b6` | `position`       | position-changed                 |
| `$s:5e8b`      | `0x5e8b` | `stop`           | stop                             |
| `$s:9a1f`      | `0x9a1f` | `start`          | start                            |
| `$s:3238`      | `0x3238` | `stance`         | stance changed                   |
| `$s:70fb`      | `0x70fb` | `run_move`       | running movement                 |
| `$s:937a`      | `0x937a` | `motion`         | motion finished                  |
| `$s:be0a`      | `0xbe0a` | `go_motion`      | start motion                     |
| `$s:4b5d`      | `0x4b5d` | `move`           | move                             |
| `$s:89cb`      | `0x89cb` | `移動`           | move (kanji)                     |
| `$s:385e`      | `0x385e` | `voice`          | voice line                       |
| `$s:e2e9`      | `0xe2e9` | `turn`           | turn                             |
| `$s:491d`      | `0x491d` | `mode`           | mode change                      |
| `$s:ca87`      | `0xca87` | `loop`           | loop                             |

Inside a trap body you can read which event actually fired via the
`arg3` stack arg the runtime pushes:

```gcl
trap $s:6373 $s:50ae $s:14c9 {        # wildcard event
    if (arg3 == $s:0dd2) {             # branch on entry
        mesg $s:18e3 $s:c927           # send "off" to snow
    } elseif (arg3 == $s:d5cc) {
        mesg $s:18e3 $s:0e4e           # send "on" to snow
    }
}
```

This is from [s00a/scenerio.gcl](../../gcl/decompiled/s00a/scenerio.gcl) —
toggling the snowstorm when the player enters/leaves a zone.

---

## Stack arguments inside trap bodies

When a trap fires the runtime pushes three implicit args:

| Arg     | Holds                                       |
|---------|---------------------------------------------|
| `arg1`  | actor handle of the **subject** (so you can `mesg` it) |
| `arg2`  | the **zone** hash that matched              |
| `arg3`  | the **event** hash that fired               |

That lets one trap body handle several wildcarded variants and
branch internally — a common style across the corpus.

---

## `trap` vs `ntrap`

| Aspect            | `trap`              | `ntrap`                            |
|-------------------|---------------------|------------------------------------|
| Lifetime          | persistent until stage end | added/removed dynamically    |
| Storage           | `gBindsArray_800b58e0` | separate ntrap table            |
| Common use        | door triggers, cutscene cues | pad-button reactions, timed binds |
| Options           | `-e <handler>`      | `-e`, `-r` (repeat), `-b <pad mask>`, `-s <stance>`, `-d <dir>` |

`ntrap` examples that don't fit `trap`:

```gcl
# Trigger when user presses Triangle in a zone
ntrap $s:8f4c $s:21ca \
    -e sub_pickup_handler \
    -b 0x10                  # PAD_TRIANGLE bitmask

# Stance-gated: only when crouched
ntrap $s:c48c $s:21ca \
    -e sub_crawl_action \
    -s 2                     # 2 = prone stance
```

Pad bitmasks (PSX controller, `port_pad.h`):

| Hex      | Button    |
|----------|-----------|
| `0x0010` | △ Triangle |
| `0x0020` | ○ Circle   |
| `0x0040` | × Cross    |
| `0x0080` | □ Square   |
| `0x0100` | L2         |
| `0x0200` | R2         |
| `0x0400` | L1         |
| `0x0800` | R1         |
| `0x0008` | Start      |
| `0x0001` | Select     |
| `0x1000..0x8000` | D-pad up/right/down/left |

---

## Common trap idioms

### Toggle gate

```gcl
# A door that opens once when Snake enters its zone
trap $s:c776 $s:21ca $s:0dd2 {
    if (!$f:040002) {              # haven't opened yet
        eval($f:040002 = true)     # mark opened
        mesg $s:dd6d $s:0e4e       # tell door to open
        sound -x sd:01010001       # play door SFX
    }
}
```

### One-shot via flag self-clear

```gcl
trap $s:6373 $s:21ca $s:0dd2 {
    eval($f:06006E = true)
    sound -x sd:FF000007
}

# Elsewhere, on stage exit:
eval($f:06006E = false)             # reset for next visit
```

### Subject-pivot

```gcl
# Different reaction depending on WHO entered (using wildcard subject + arg)
trap $s:8f4c $s:14c9 $s:0dd2 {
    if (arg1 == $s:21ca) {           # was it snake?
        radio -c 14048 t:01010855 0  # codec ring
    } elseif (arg1 == $s:a608) {     # was it a guard? (zako)
        sound -x sd:01010030         # alert beep
    }
}
```

### Dispatcher proc

When several traps share logic, route through a proc:

```gcl
proc sub_door_handler {                 # arg1: door id
    if (arg1 == $s:c776) {
        eval($f:040002 = true)
    } elseif (arg1 == $s:c777) {
        eval($f:040003 = true)
    } elseif (arg1 == $s:c778) {
        eval($f:040004 = true)
    }
    mesg arg1 $s:0e4e
    sound -x sd:01010001
}

# Multiple traps, one handler:
trap $s:c776 $s:21ca $s:0dd2 { call(sub_door_handler, $s:c776) }
trap $s:c777 $s:21ca $s:0dd2 { call(sub_door_handler, $s:c777) }
trap $s:c778 $s:21ca $s:0dd2 { call(sub_door_handler, $s:c778) }
```

This pattern is used heavily in stages with parallel doors / lockers /
elevators.

---

## How a trap fires (runtime path)

1. Each frame, [event.c](../../../source/libhzd/event.c) walks the
   bind list and evaluates each `(zone, subject, event)` against the
   current frame's HZD state.
2. On match, it pushes `(arg1, arg2, arg3)` onto the GCL stack.
3. Calls `GM_DelayedExecCommand` with the bind's `field_14_proc_and_block`
   pointer — that's either an inline block (allocated when the trap
   was registered) or a proc id.
4. The block body executes immediately or next frame depending on
   active vs deferred mode.

Important: the inline-block pointer points **into the loaded `.gcx`
buffer**. If your overlay buffer outlives the cache properly, this
works. The recent overlay bug (sna_init crash spam) was caused by
this exact pointer becoming stale after malloc'ing a buffer outside
the game's pool — see [gcl_overlay.c](../../gcl_overlay.c).
