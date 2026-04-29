---
file: source/memcard/ + port/memcard/
---

# `source/memcard/` — save / load

Memcard-driven save / load. PSX has 15 slots × 8 KiB per memcard
slot, total 120 KiB. MGS uses one slot per save.

## Public API

```c
int  MC_Init(void);
int  MC_Format(int port);
int  MC_Save(int port, int slot, const char *name, void *data, int size);
int  MC_Load(int port, int slot, void *data, int size);
int  MC_GetSlots(int port, MC_INFO *out);
int  MC_DeleteSlot(int port, int slot);
```

The `port` arg is 0 or 1 (two physical memcard ports on PSX); MGS
defaults to port 0.

## Save format

The MGS save consists of:

```
HEADER       — magic + checksum + thumbnail
GCL_VARS     — $w / $b / $f arrays from libgcl
INVENTORY    — every ITEM_ENTRY from game/item.c
WEAPON_AMMO  — per-weapon ammo state
PLAYER_STATE — life, position, current stage
RANK_FLAGS   — completion timer, codename
```

Layout is fixed-size — corrupt save = checksum failure.

## Save flow

```
GCL `save` command (or radio call)
  ↓
GM_SaveData():
  GCL_MakeSaveFile(buf)        // dump GCL vars
  serialise inventory + weapons + state
  compute checksum
  ↓
MC_Save(port, slot, "MGS_DATA", buf, size)
```

The port replaces `MC_Save` with a stdio write to a host file
(`port/memcard/`).

## Load flow

```
Player picks save slot in datasave.c
  ↓
GM_LoadData(slot):
  MC_Load(0, slot, buf, size)
  verify checksum
  ↓
  GCL_SetLoadFile(buf)         // restore GCL vars
  restore inventory + weapons + state
  trigger stage transition to saved stage
```

## Slot management UI

`menu/datasave.c` is the slot picker — see
[menu/menuman.md](../menu/menuman.md). It calls `MC_GetSlots` to
enumerate, `MC_Save` / `MC_Load` for the actual operations.

## Pitfalls

- **Save during cinematic = bad state.** Should be gated, but
  some scripts allow it. Reload may resume mid-cinematic.
- **Per-stage state isn't always saved.** Stage GCL can mark
  certain `$w` vars as transient; they reset on load.
- **Checksum failure aborts load silently.** Player sees "data
  corrupted" without an error code.

## Port notes

The port replaces the memcard with a flat file at
`port/memcard/0.bin` etc. The save format is byte-identical so
saves are portable from PSX to port (after extracting from PSX
memcard).

## See also

- [`source/menu/menuman.md`](../menu/menuman.md) — datasave UI.
- [`source/libgcl/expr.md`](../libgcl/expr.md) — GCL var
  serialisation.
- [`source/game/item.md`](../game/item.md) — inventory data.
- [`port/memcard/`](../../../../port/memcard/) — port replacement.
