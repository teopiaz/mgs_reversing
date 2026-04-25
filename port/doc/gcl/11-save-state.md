# Save State and Persistence

How GCL variables, codec memory, and scripted progress survive across
stage boundaries and into memory-card saves.

Ground truth:
- [source/libgcl/variable.c](../../../source/libgcl/variable.c) — save/load orchestration
- [source/menu/menuman.c](../../../source/menu/menuman.c) — memory-card I/O
- [source/include/linkvarbuf.h](../../../source/include/linkvarbuf.h) — game-state var layout

---

## Two scopes of variables

| Scope        | Buffer                       | Lifetime                              |
|--------------|------------------------------|---------------------------------------|
| **local**    | `gGcl_vars_800B3CC8`         | per-stage; cleared on stage entry     |
| **game-state** | `linkvarbuf` / `GM_*Flag`  | persists across stage transitions     |

Sigil distinguishes them:

```gcl
$f:000001     # local — gone after stage exits
$f:0002CC     # game-state (high address bits set) — persists
```

Game-state is determined by the encoded high bits of the var address,
not by the sigil. See [02-variables.md](02-variables.md#the-encoded-address).

---

## Three layers of persistence

```
                                      memory card
                                    ┌──────────────┐
                                    │  SAVE_FILE   │ ← survives reboot
                                    │   (with CRC) │
                                    └──────┬───────┘
                                           │  GCL_MakeSaveFile / GCL_SetLoadFile
                                    ┌──────▼───────┐
                                    │ sv_linkvarbuf│ ← "checkpoint" snapshot
                                    │ memVars      │   set by GCL_SaveVar / varsave
                                    └──────┬───────┘
                                           │  GCL_RestoreVar
                                    ┌──────▼───────┐
                                    │ linkvarbuf   │ ← live game-state
                                    │ vars         │   read/written every frame
                                    └──────────────┘
```

### Layer 1: live (`linkvarbuf`, `gGcl_vars_800B3CC8`)

Every read/write inside `eval(...)` touches one of these buffers
directly. Modifying `$w:80007A` updates the live difficulty value
immediately.

### Layer 2: snapshot (`sv_linkvarbuf`, `gGcl_memVars_800b4588`)

The "checkpoint" copy. Game-state vars in this buffer are what gets
written to the memory card on save. `linkvarbuf` and
`sv_linkvarbuf` aren't auto-synced — you must explicitly request:

| Command       | Effect                                                     |
|---------------|------------------------------------------------------------|
| `GCL_SaveVar` (C-side) | copy live → snapshot (full)                       |
| `GCL_RestoreVar` (C-side) | copy snapshot → live (full)                    |
| `varsave $w:XXX` (GCL)| copy *one var* live → snapshot                     |

`varsave` is the GCL-side primitive for "promote this variable to the
checkpoint". Without it, your variable lives only in the live buffer
and dies on stage transition.

```gcl
eval($w:0002CA = $w:0002CA + 1)     # increment kill count
varsave $w:0002CA                    # promote to checkpoint
```

### Layer 3: memory card

`GCL_MakeSaveFile` serialises the entire checkpoint into a `SAVE_FILE`
struct (with a CRC32) and the memory-card subsystem writes it to disc.
GCL scripts don't directly drive this — it's invoked by menu actions.

---

## What's in `linkvarbuf` (the well-known fields)

From [linkvarbuf.h](../../../source/include/linkvarbuf.h):

| Offset (`$X:0008XX`)    | Bytes | Variable                            |
|-------------------------|-------|-------------------------------------|
| `$w:800002`             | 2     | Difficulty bias / camera mode       |
| `$w:800010..14`         | 6     | Snake spawn (X, Y, Z)               |
| `$w:800016..18`         | 4     | Snake scale                         |
| `$w:80001C, 1E`         | 4     | Camera angles                       |
| `$w:800022..32`         | …     | Misc per-stage scratch              |
| `$w:80003A`             | 2     | Air supply (mask scenes)            |
| `$w:80003C`             | 2     | Item count modifier                 |
| `$w:800042`             | 2     | Time-attack record                  |
| `$w:800044`             | 2     | Player face direction               |
| `$w:80007A`             | 2     | Active difficulty (1=easy, 2=norm)  |
| `$w:80007E`             | 2     | Bandana / cleared-game flag         |
| `$f:0002CC`             | 1 bit | "previous clear" carry-over flag    |
| `$w:0002CA`             | 2     | Total clears counter                |

These are the most common cross-stage variables; the corpus reads
them in 100+ scenerios.

---

## `varsave` patterns

### Setup-time persist

After difficulty selection, write to the snapshot once:

```gcl
proc sub_set_difficulty {           # arg1 = difficulty (1..3)
    eval($w:80007A = arg1)
    varsave $w:80007A               # so it survives stage exit
}
```

### Counter that survives

```gcl
proc sub_on_clear_stage {
    eval($w:0002CA = $w:0002CA + 1)
    varsave $w:0002CA
}
```

### Bulk persist

`varsave` only takes one var at a time. For several:

```gcl
proc sub_save_progress {
    varsave $w:0002CA
    varsave $w:80007A
    varsave $f:0002CC
    varsave $w:80007E
}
```

---

## What's in the on-disc save (`SAVE_FILE`)

From [variable.c:24](../../../source/libgcl/variable.c#L24):

```c
typedef struct SAVE_DATA
{
    int         version;            // 0x60
    int         version2;           // 0x800
    int         totalFrameTime;     // total play time in frames
    int         padding[3];
    char        stage_name[16];     // e.g. "s01a"
    AreaHistory area_history;       // breadcrumb of areas visited
    short       varbuf[0x60];       // = sv_linkvarbuf snapshot
    GCL_Vars    gcl_vars;           // = gGcl_memVars_800b4588 snapshot
    RadioMemory radio_memory[N];    // codec memory
} SAVE_DATA;
```

What survives a save/load:

- `varbuf` (a copy of game-state `$X:8xxxxx` vars)
- `gcl_vars` (a copy of local vars at save time — *yes*, locals are
  also saved)
- `RadioMemory` — the codec's contact list and which lines you've
  unlocked
- Snake's stage + area + total play time

What does **not** survive:

- Live actor state (HP, position, ammo) — repopulated by re-running
  the destination stage's scenerio
- Sound state, demo flags
- Anything not explicitly persisted by the engine before save

---

## Codec memory (`RadioMemory`)

The radio command interacts with a separate persistent buffer. Each
contact has a `RadioMemory` slot tracking:

- Which dialogue lines have been heard
- Which contacts are unlocked
- Per-contact flags (e.g. "Master called you about saving")

`radio -m` (memo / hangup), `MENU_InitRadioMemory`, and the menu
codec UI all read/write here. From a GCL author's perspective:
calling `radio -c <id> <line>` will mark that line as heard and
update the codec's state — no explicit save needed for that part.

---

## Live save / restore from GCL

The game daemon ([gamed.c](../../../source/game/gamed.c)) calls
`GCL_SaveVar` automatically at certain breakpoints (autosaves, after
boss defeats). You can also force-save in scripts via:

```gcl
# Direct save invocation through system - rarely needed in custom scripts
system -h           # one of the "checkpoint" subops (semantics opaque)
```

Most authors won't touch this — instead, set the right vars and let
the engine save naturally on the next checkpoint trigger.

---

## Pitfall: locals don't auto-clear between visits

If you re-enter the same stage with a soft-restart (`load -r 1`), the
local vars (`$X:0xxxxx`) are NOT cleared — they retain values from
the previous run. To force-clear:

```gcl
script {
    start -c        # clear-flags variant
    ...
}
```

Or in the proc that does the restart, manually:

```gcl
proc sub_full_reset {
    eval($f:000001 = false)
    eval($w:000044 = 0)
    eval($b:000000 = 0)
    load "" -r 1
}
```

---

## Save-data version mismatch

The save's `version`/`version2` fields are checked on load:

```c
if ((save_data->version != SAVE_VERSION)        // 0x60
        || (save_data->version2 != SAVE_VERSION2))  // 0x800
{
    printf("SAVE DATA VERSION ERROR!!\n");
}
```

If you change the layout of `linkvarbuf` (e.g. add a new game-state
var), you must bump `SAVE_VERSION` — or all existing saves become
unreadable. Vanilla didn't change these constants after release.
