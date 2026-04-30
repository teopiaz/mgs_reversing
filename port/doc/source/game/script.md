---
file: source/game/script.c
---

# `game/script.c` — GCL command set

The bridge between scripts and engine policy. Registers all the
commands a `.gcx` file can call. 1185 lines.

## Initialisation — `GM_InitScript`

Called by `gamed.c` at boot. Walks a list of `GCL_COMMANDLIST`
tables and registers them via `GCL_AddCommMulti`.

The result: the GCL script can now use commands like `chara`,
`mesg`, `delay`, `cam`, `pad`, `fade`, `vox`, `light`, etc.

## Command catalogue

Roughly:

### Spawning + naming

| Command | Effect |
| ------- | ------ |
| `chara &NAME -p ... -d ...` | Spawn a chara via `GM_GetChara` + constructor |
| `cancel &NAME` | `GM_CancelChara(hash)` — destroy all charas of given name |
| `proc &NAME(args)` | Call a GCL proc (built into libgcl, registered here) |

### Communication

| Command | Effect |
| ------- | ------ |
| `mesg &TARGET HASH ...` | `GV_SendMessage` — send mesg to target |
| `wait FRAMES` | Yield N frames |
| `event &EVENT_NAME ...` | Bind / fire an event handler |

### Camera

| Command | Effect |
| ------- | ------ |
| `cam -p eye -d center -r interp` | Set camera; see [camera.md](camera.md) |
| `view -m mode` | Switch view mode (1st-person etc.) |

### Display

| Command | Effect |
| ------- | ------ |
| `fade type duration` | Trigger screen fade (white/black/etc.) |
| `light -d direction -c color` | Set scene light |
| `clip dist` | Far-clipping plane |

### Sound

| Command | Effect |
| ------- | ------ |
| `vox file` | Play voice sample |
| `bgm song [tempo]` | Background music |
| `se sound_id` | Play sound effect |

### Demo / cinematics

| Command | Effect |
| ------- | ------ |
| `demo file` | Load and play DMO file |
| `pad u/d/l/r/...` | Inject demo pad inputs |
| `chrtbl &name` | Switch chara table for cinematic |

### Game-state

| Command | Effect |
| ------- | ------ |
| `alert level` | Force alert state (caution/alert/evasion) |
| `radar on/off` | Toggle radar visibility |
| `pause flag` | Set / clear pause level |
| `save -s slot` | Save game (memcard) |
| `gameover` | Trigger game-over sequence |

### Item / weapon

| Command | Effect |
| ------- | ------ |
| `item -g ITEM` | Give item to player |
| `weapon -e WEAPON` | Equip weapon |

### Stage management

| Command | Effect |
| ------- | ------ |
| `load STAGE` | Trigger stage transition |
| `map AREA` | Switch sub-area within stage |

The full list is in `script.c` — these are the categories.

## Command implementation pattern

Each command is `int command_fn(unsigned char *args)`:

1. `GCL_GetOption('p')` etc. to read CLI-style options.
2. Translate options to engine calls: `GM_NewActor`, `GV_SendMessage`,
   `DG_SetLight`, etc.
3. Return 0 (continue) or 1 (yield).

Example (`mesg`):

```c
int script_mesg(unsigned char *args) {
    int target = strcode(GCL_StrToInt(args));
    GV_MSG msg = { .address = target,
                   .message = { GCL_GetNextParamValue(), ... } };
    GV_SendMessage(&msg);
    return 0;
}
```

## `wait` — the yield primitive

```c
static int wait_count = 0;
int script_wait(unsigned char *args) {
    if (wait_count == 0) wait_count = GCL_StrToInt(args);
    if (--wait_count > 0) return 1;     // yield: re-enter next frame
    return 0;                            // done
}
```

This is how `wait 30` blocks the current proc for 30 frames. The
`return 1` propagates up through `GCL_ExecBlock` so the daemon
re-enters the proc next frame — picking up at the same instruction
because `GCL_ExecBlock`'s cursor is *not* advanced past a yielded
command.

## Per-frame execution: the GCL daemon

`gcl_init.c` registers a level-0 daemon actor:

```c
gcl_daemon_act():
    GCL_ExecBlock(current_script.script_body + 3, &gcl_null_args)
    if returned 1 (yielded): re-enter next frame, same script position
```

The daemon doesn't step its cursor — `GCL_ExecBlock` itself owns the
script cursor and resumes from where it yielded.

## Pitfalls

- **`wait` state is per-script, not per-proc.** A nested proc that
  calls `wait` reuses the outer wait counter — leading to subtle
  delay-mismatch bugs. Stages tend to avoid wait inside procs for
  this reason.
- **Returning 1 from a non-yielding command stalls the script.**
  Common bug: forgetting that 1 means yield.
- **The command-line stack is bounded at 8.** Commands that nest
  too deeply (nested `if` / `for` etc.) may exhaust it.

---

## Port notes

`script.c` runs unmodified. The port adds debug hooks to log every
GCL command invocation (visible via `[gcl-cmd]` prefix in stderr).

## See also

- [`source/libgcl/`](../libgcl/index.md) — the bytecode
  interpreter this builds on.
- [`source/libgcl/command.md`](../libgcl/command.md) — `GCL_Command`
  is what dispatches into here.
- [chara.md](chara.md) — the `chara` command's lookup helper.
- [camera.md](camera.md) — what the `cam` command writes to.
