# `source/memcard/` — save-game I/O

PSX memory-card save / load. Two files:

| File | Role |
| ---- | ---- |
| [`memcard.c`](../../../../source/memcard/memcard.c) | Save / load implementation. |
| [`memcard.h`](../../../../source/memcard/memcard.h) | Public API. |

## What gets saved

Per save slot:

- All GCL persistent variables: `$w:` (16-bit) / `$b:` (8-bit) /
  `$f:` (1-bit flag) — collected via `GCL_SaveVar`.
- Snake's stat block: HP / max HP / time-played / current stage.
- Inventory: items + ammo counts.
- Stage progress flags.
- Controller config (camera-Y inversion, vibration on/off).
- A small block of cinematic-state flags (which cinematics
  played).

## API

```c
int  MEMCARD_Init(void);
int  MEMCARD_Format(int slot);
int  MEMCARD_Save(int slot, const char *name, void *data, int size);
int  MEMCARD_Load(int slot, const char *name, void *data, int size);
int  MEMCARD_Delete(int slot, const char *name);
```

## Used by

- `menu/datasave.c` — UI for save/load slot selection.
- `gamed.c::GM_ContinueStart` — load on continue.
- GCL `save` / `load` directives via `script.c`.

---

## Port notes

The port replaces this with a stub
([`port/memcard_stub.c`](../../../../port/memcard_stub.c)) that
either:

- Writes to a host file (when SAVE-PATH env is set).
- Returns "no card" otherwise.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [savefile.md](savefile.md) | Save format, save/load flow, slot management |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`source/libgcl/variable.c`](../../../../source/libgcl/variable.c)
  — `GCL_SaveVar` / `GCL_LoadVar` are what feed the GCL var
  spaces in/out.
- [`source/menu/datasave.c`](../../../../source/menu/datasave.c)
  — the in-game save menu.
