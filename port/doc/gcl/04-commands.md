# Command Reference

The 28 GCL commands. Each runs a C handler registered in
[source/game/script.c:1141](../../../source/game/script.c#L1141) or
[source/libgcl/basic.c](../../../source/libgcl/basic.c). Every command
is dispatched by its 16-bit `GV_StrCode()` hash — the names in this doc
are what the code uses, not what you see in bytecode (where you see
hashes like `0x9906` for `chara`).

Conventions in this doc:
- Positional args first, then options (alphabetical).
- Examples are **real lines** from [port/gcl/decompiled/](../../gcl/decompiled/).

---

## Stage & world

### `load` — transition to another stage

```gcl
load "s01a" -m $s:7df9 -s 1
load "title" -m $s:7df9 -s 1
load "s00a" -r 1              # restart (soft)
```

Options: `-m <map-id>` (which map to load — `$s:7df9` = "main"),
`-s <slot>` (camera slot), `-r <mode>` (0 = hard restart, 1 = soft),
`-d`, `-e`, `-l`, `-n`, `-p`, `-v`, `-x` (various init hooks).

Empty string `load ""` triggers a plain `GM_LoadRequest = 1` — the
game re-enters the same stage. Handler:
[script.c:539 GM_Command_load](../../../source/game/script.c#L539).

### `mapdef` — declare a map and its assets

```gcl
mapdef $s:7df9 \
    -k $s:c681 $s:6da4     # KMD files (model)
    -l $s:c681             # light data
    -h $s:c681 0           # HZD file (collision/zone map) + area index
```

Maps bundle collision (`-h`), lighting (`-l`), and renderable geometry
(`-k`). The positional is the map's ID (hashed name, e.g. `$s:7df9` =
"main"). Handler: [script.c GM_Command_mapdef](../../../source/game/script.c).

### `map` — build map areas at runtime

```gcl
map -d $s:7df9 $s:eee9                      # define area
map -a $s:7df9                              # activate area
map -s $s:80000E                            # set current map from variable
map -l                                      # load queued areas
```

`mapdef` declares data; `map` instantiates active areas. Almost every
scenario ends with a sequence of `map -a` / `map -s` that commits the
currently selected area.

### `camera` — camera control

```gcl
camera \
    -p 0 -3000 -1000       # position
    -t 0 0 0               # target
    -m 2                   # mode
```

Options like `-a/-b/-c/-d/-e/-f/-k/-l/-m/-n/-p/-r/-s/-t/-v/-x` tune
position, target, mode, field of view, cuts. Look at any cutscene
`demo.gcl` for dense examples (e.g. `d00a/demo.gcl`).

### `light` — directional / ambient lighting

```gcl
light -d 0 -1 0           # directional (XYZ direction)
light -c 35 35 35         # color (R, G, B)
light -a 51 51 51         # ambient
```

Three `light` invocations (directional, color, ambient) almost always
appear as a triplet in stage init. Handler:
[script.c GM_Command_light](../../../source/game/script.c).

---

## Actors & objects

### `chara` — spawn a character (actor)

```gcl
chara $s:21ca $s:21ca                                    # snake (no options)
chara $s:1465 $s:1465 -m -1                              # shadow
chara $s:81c7 $s:81c7 -e sub_1D3C "..."                  # with event handler
chara $s:a12e $s:9119 -m 0 -s 1                          # fade with params
```

Positional args: `<type-hash> <instance-name-hash>`. The type-hash
looks up a `NEWCHARA` factory registered via the `MainCharacterEntries[]`
table in [port/extern_stubs.c]. The instance-hash distinguishes multiple
spawns of the same type (e.g. `$s:1465/1466/1467` = three kage/shadow
instances).

Common options:
- `-e <proc-or-block>` — handler proc / inline block executed as events fire
- `-m <int>` — mode / map binding
- `-s <int>` — variant / slot
- `-p X Y Z` — spawn position

Handler: [script.c:476 GM_Command_chara](../../../source/game/script.c#L476).
Note: the handler's signature is a known mess — `argc`/`argv` are trash
for this one command, factories ignore them.

### `trap` — persistent event binding

```gcl
trap $s:8f4c $s:21ca $s:14c9 {
    mesg arg1 $s:0e4e
}

trap $s:c776 $s:21ca $s:0dd2 {
    call(sub_3D10, 0)
}
```

Positional: `<zone-hash> <subject-hash> <event-hash>`. When `<subject>`
enters the trap's zone with `<event>` firing, the inline block or
`-e` proc runs. Used heavily for doors, cutscene triggers, elevator
stops.

Common event hashes (from [strcode.h](../../../source/include/strcode.h)):
- `$s:0dd2` (`入る`) — enter
- `$s:d5cc` (`出る`) — leave
- `$s:14c9` (`？`) — wildcard
- `$s:0e4e` (`on`) — activate

### `ntrap` — non-persistent (one-shot) trap

```gcl
ntrap $s:8f4c $s:21ca \
    -e sub_handler
    -r              # repeating
    -b 0x10         # button mask
```

Like `trap` but registered into a different table that can be
cleared/replaced more dynamically. Used for pad-button triggers,
stance-based activations, timed events.
Handler: [script.c:299 GM_Command_ntrap](../../../source/game/script.c#L299).

### `delay` — schedule delayed execution

```gcl
delay -t 22 -e sub_later        # run sub_later after 22 frames
delay -t 60 -p 42               # run proc 42 after 60 frames
delay -t -30 -e sub_alert       # negative = "active mode" delay
```

Options: `-t <frames>` (wait time), `-p <proc-id>` or `-e <proc|block>`,
`-g` (negate time, alternate mode). The delay actor destroys itself
once the target fires.
Handler: [script.c:411 GM_Command_delay](../../../source/game/script.c#L411),
implementation [source/game/delay.c](../../../source/game/delay.c).

### `mesg` — send a message to an actor

```gcl
mesg arg1 $s:0e4e                # send "on" to arg1
mesg $s:18e3 $s:c927             # send "off" to snow actor
mesg arg6 $s:0e4e $s:4878        # with extra payload
```

Actor `address` receives a `GV_MSG` with the remaining args as the
payload. Common uses: toggle visibility, trigger alerts, wake/sleep
enemies.

### `pad` — player input gating

```gcl
pad -r        # release (disable input)
pad -s        # set (enable input)
pad -m 1      # set mode
```

Used around cutscenes (`pad -r` at entry, `pad -s` at exit) to freeze
Snake's input while demos play.

---

## Flow control & state

### `if` / `elseif` / `else`

```gcl
if ($w:800002 < 0) {
    eval($w:000410 = 3)
} elseif ($w:800002 == 0) {
    eval($w:000410 = 3)
} elseif ($w:800002 == 1) {
    eval($w:000410 = 5)
} else {
    eval($w:000410 = 7)
}
```

Classic C-style. The runtime dispatches via
[source/libgcl/basic.c:5 GCL_Command_if](../../../source/libgcl/basic.c#L5).

### `eval` — evaluate / assign

```gcl
eval($f:000001 = false)
eval($b:000000 = $b:000000 + 1 | 128)
eval($s:80000E = $s:7df9)
```

The expression forms are covered in [03-expressions.md](03-expressions.md).

### `return` — stop current proc/block

Rare in vanilla scripts (0 uses in the vanilla corpus), but valid. Ends
the enclosing `GCL_ExecBlock` / `GCL_ExecProc`.

### `foreach` — iterate over a value list

```gcl
foreach 18000 10000 2000 -6000 \
    -do sub_handler            # call sub_handler(arg) for each value
```

Rarely used (only 12 sites across 135 files). The values are pushed
one at a time as `arg1` to the `-do` proc.

### `start` — stage-start hooks

```gcl
start -m          # initialize menu system
start -f          # load fonts
start -v          # clear vars
start -c          # clear flags
start -s          # reset read-error counter
```

Called at the top of many scenerios to prepare game state. Options are
independent flags; combine as needed. Handler:
[script.c:498 GM_Command_start](../../../source/game/script.c#L498).

### `restart`

```gcl
restart           # no args — full restart of current stage
```

Triggers `GM_LoadRequest` for the current area with the soft-restart
flag. Used in game-over handlers.

### `varsave` — persist a variable to save buffer

```gcl
varsave $w:0002CA
```

Copies the live value into `sv_linkvarbuf` so it survives
`GCL_MakeSaveFile` / memory-card write. See
[02-variables.md](02-variables.md#persistence-into-save-files).

### `system` — runtime engine flags

```gcl
system -g        # set a game-system flag
system -s val    # set stage-status
```

Opaque grab-bag of engine flags. Usage is sparse (81 sites).
Handler: [script.c GM_Command_system](../../../source/game/script.c).

---

## Audio & media

### `sound` — play SFX / BGM

```gcl
sound -x sd:01FFFF0A                   # play by SD code
sound -g 0x01 0x00 0x01 0x75 0x47      # play with explicit params
```

Triggers a PSX SPU or the port's SDL audio backend. `-x sd:XXXXXXXX` is
the most common pattern — it's an opaque 32-bit code that lookups a
wave/MIDI combo. Handler:
[script.c GM_Command_sound](../../../source/game/script.c).

### `radio` — start a codec conversation

```gcl
radio -c 14048 t:01010855 0      # call Deepthroat (14048 = hash) with line t:...
radio -c 14085 t:01010366 0      # Campbell
radio -m                         # mute / hangup
```

`-c <contact-id> <dialog> <mode>` — the first arg is a character ID
like `14085` (Campbell) / `14048` (Deepthroat), the second is a
TABLE-typed reference into `RADIO.DAT`.
Handler: [script.c GM_Command_radio](../../../source/game/script.c).

### `demo` — trigger a cutscene

```gcl
demo t:00000015         # play demo block from DEMO.DAT
demo -e sub_after_demo  # with callback when finished
```

Options select demo variants, placement, callbacks. Used for all the
pre-rendered cinematic segments. Handler:
[script.c GM_Command_demo](../../../source/game/script.c).

### `menu` — menu engine control

```gcl
menu -r 0                   # reset
menu -t "stage-name"        # top-level menu with title
menu -i option1 option2     # item list
```

Drives the main menu, pause screen, item select. Dense options set.

### `jimaku` — subtitle text

```gcl
jimaku m"s12a_ウルフ２始インクリメント"
jimaku m"始到テロ{C223}プ「メタルギアソリ{C223}ド」ロゴ" \
    -t 90                   # display for 90 frames
```

Pushes a subtitle string to the `jimctrl` actor. Uses the `m"..."`
MGS-encoded string form (see [01-syntax.md](01-syntax.md#strings)).

---

## Utility

### `rand` — sample an integer range

```gcl
rand 9           # 0..8 inclusive
rand 3
rand 12
```

The result goes to an implicit register the runtime reads next. Often
used right before an `if ($w:...) {...}` that checks the roll.

### `func` — misc engine queries

```gcl
func -s $w:800002      # sync/store something
func -i 0x20           # inspect
```

Opaque runtime access. 283 sites, mostly game-specific plumbing.

### `print` — debug output

```gcl
print "BGM change"
print "to radio"
print "s04b"
```

Writes to stdout / debugger log. No effect on gameplay. Use freely
while debugging custom scripts. Handler:
[script.c:1109 GM_Command_print](../../../source/game/script.c#L1109).

### `demodebug` — demo debug hooks

Unused in vanilla corpus. Probably a leftover Konami toolchain.

---

## The full command hash table

| Name        | Hash     | Handler                                  |
|-------------|----------|------------------------------------------|
| `if`        | `0x0d86` | `GCL_Command_if`                         |
| `eval`      | `0x64c0` | `GCL_Command_eval`                       |
| `return`    | `0xcd3a` | `GCL_Command_return`                     |
| `foreach`   | `0x7636` | `GCL_Command_foreach`                    |
| `mesg`      | `0x22ff` | `GM_Command_mesg`                        |
| `trap`      | `0xd4cb` | `GM_Command_trap`                        |
| `chara`     | `0x9906` | `GM_Command_chara`                       |
| `map`       | `0xc091` | `GM_Command_map`                         |
| `mapdef`    | `0x7d50` | `GM_Command_mapdef`                      |
| `camera`    | `0xeee9` | `GM_Command_camera`                      |
| `light`     | `0x306a` | `GM_Command_light`                       |
| `start`     | `0x9a1f` | `GM_Command_start`                       |
| `load`      | `0xc8bb` | `GM_Command_load`                        |
| `radio`     | `0x24e1` | `GM_Command_radio`                       |
| `restart`   | `0xe43c` | `GM_Command_restart`                     |
| `demo`      | `0xa242` | `GM_Command_demo`                        |
| `ntrap`     | `0xdbab` | `GM_Command_ntrap`                       |
| `delay`     | `0x430d` | `GM_Command_delay`                       |
| `pad`       | `0xcc85` | `GM_Command_pad`                         |
| `varsave`   | `0x5c9e` | `GM_Command_varsave`                     |
| `system`    | `0x4ad9` | `GM_Command_system`                      |
| `sound`     | `0x698d` | `GM_Command_sound`                       |
| `menu`      | `0x226d` | `GM_Command_menu`                        |
| `rand`      | `0x925e` | `GM_Command_rand`                        |
| `func`      | `0xe257` | `GM_Command_func`                        |
| `demodebug` | `0xa2bf` | `GM_Command_demodebug`                   |
| `print`     | `0xb96e` | `GM_Command_print`                       |
| `jimaku`    | `0xec9d` | `GM_Command_jimaku`                      |

Full list in [source/include/strcode.h:13](../../../source/include/strcode.h#L13).
