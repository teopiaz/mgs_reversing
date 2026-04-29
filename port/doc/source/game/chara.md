---
file: source/game/chara.c
---

# `game/chara.c` — chara factory dispatch

Resolves a GCL `chara &NAME` directive to a constructor function.
Tiny file (~75 lines) but absolutely central — every actor in the
game is spawned via this dispatcher.

## The two tables

```c
extern CHARA MainCharacterEntries[];    // built into main exe (SLPM_*)
extern CHARA _StageCharacterEntries[];  // appended by stage overlay
```

`CHARA` is a struct of `{ class_id, NEWCHARA func, ... }` where
`class_id` is the strcoded chara name and `func` is the
constructor.

`MainCharacterEntries[]` lives in `main.c` (`source/main.c`) and
contains the always-loaded charas: `SNAKE`, `WATCHER`, `DOOR`,
`EMITTER`, etc.

`StageCharacterEntries[]` is dynamic — comes from whatever stage
overlay is currently loaded. Each stage's overlay binary ends with
its private chara table.

## The lookup

```c
NEWCHARA GM_GetCharaID(int chara_id) {
    for (i = 0; i < 2; i++) {
        table = (i == 0) ? MainCharacterEntries : StageCharacterEntries;
        for (; table->func != NULL; table++) {
            if (table->class_id == chara_id)
                return table->func;
        }
    }
    return NULL;
}
```

Two-pass search: try main table first (built-in charas); if not
found, fall through to stage table. So a stage *can* override a
built-in chara by registering one with the same id, since main is
checked first — wait, **main wins**. The fallback gives stages a
private extension namespace, not override capability.

## `StageCharacterEntries` discovery

The stage table's location varies by build:

```c
#if DEV_EXE
    StageCharacterEntries = &_StageCharacterEntries[0];   // direct symbol
#elif PORT_BUILD or RELEASE
    StageCharacterEntries = mts_get_bss_tail();           // computed at boot
#endif
```

In RELEASE / PORT_BUILD, the stage table isn't a known symbol —
it lives at the tail of BSS, populated by stage init. `mts_get_bss_tail()`
walks the linker layout to find it.

## Port: 32-bit address rejection

The port adds a guard:

```c
if ((uintptr_t)chara_table->func < 0x100000000ULL)
    return NULL;
```

Some stage tables still have raw PSX addresses (`0x80XXXXXX`) for
charas not yet ported. The port treats these as "missing" rather
than calling into garbage.

## `GM_GetChara(script)` — GCL entry point

```c
NEWCHARA GM_GetChara(unsigned char *script);
```

Reads the next GCL value (a hashed string), then dispatches via
`GM_GetCharaID`. This is what `chara &WATCHER ...` resolves to:
`GM_GetCharaID(strcode("WATCHER")) → NewWatcher`.

## `GM_InitChara` / `GM_ResetChara`

`GM_InitChara` — called once at game boot; resolves
`StageCharacterEntries` based on build mode.

`GM_ResetChara` — called between stages; clears the current stage
table's first entry to "nothing" so a stage that doesn't replace
the table won't expose stale entries.

## Pitfalls

- **`MainCharacterEntries` order matters.** Charas later in the
  table are reached only if earlier ones don't match. No collision
  detection at boot.
- **Hash collisions are possible.** Two charas hashing to the same
  16-bit id would conflict. The known shipped set is collision-
  free.
- **Stage table can shadow main table only by appearing later in
  search**, but main is searched first — so stages can't override.

## See also

- [02-game-loop.md](../02-game-loop.md) — `gamed.c` calls
  `GM_InitChara` at boot.
- [`source/main.c`](../../../../source/main.c) — `MainCharacterEntries`
  definition.
- [`source/include/charadef.h`](../../../../source/include/charadef.h) —
  `CHARA` struct.
