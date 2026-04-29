---
file: source/enemy/ — opaque areas
---

# `enemy/` — what's still by-address / opaque

The functions are 100% decompiled, but several **fields and globals**
still carry their raw addresses because the decompiler hasn't been
able to prove a name. This page lists what's known and what's not.

## `WatcherWork` field placeholders

The struct is partially named. Anything still numbered (`field_0xNN`)
is opaque to varying degrees:

| Field | Hypothesis | Confidence |
| ----- | ---------- | ---------- |
| `field_904` | TARGET — melee attack-out (TARGET_POWER) | high (consistent across watcher / meryl7 / zako11e) |
| `field_94C` | TARGET — touch box (TARGET_TOUCH) | high |
| `punch` | TARGET — pistol-whip swing | high |
| `field_AF0` | NewShadow2 actor pointer | medium |
| `field_AF4[]` | NewShadow2 enable flag (set by act) | medium |
| `field_AF8` | NewGunLight actor pointer | medium |
| `field_AFC[]` | NewGunLight enable flag | medium |
| `field_B00[]` | unused init zero loop in meryl7; hypothesis "joint blend buffer" | low |
| `field_B78` | EnemyCommand.field_0xC8 slot index for this guard | high |
| `field_B7B` | KMD swap mode (-y GCL flag) | high |
| `field_B7C` | HZD address of named spawn point | medium |
| `field_B7F` | mirror of `field_B7C` for restore-on-fadein | medium |
| `field_B81` | gun-glow colour override (-g GCL flag) | medium |
| `field_B94` | unknown small int, init to 0 | low |
| `field_BA0` | -1 sentinel; possibly "last seen player time" | low |
| `field_BA3` | search-flag bits — `0x10` = footstep enable, lower bits used by think | medium |
| `field_BA4` | `COM_NO_POINT` SVECTOR copy — investigation point | medium |
| `field_BB0[]` | "timing" array (-t GCL flag, 4 ints) | medium |
| `field_BD0[]` | "direction" array (-i GCL flag, 4 angles) | medium |
| `field_BF0` | mirror of `start_addr` | low |
| `field_BFC` | per-guard sound-bank index from `s07a_dword_800C35F8[8]` | low |
| `field_C00` | duplicate of `field_B78` | low |
| `field_C08` | mirror of `start_addr` | low |
| `field_C14` | mirror of `start_pos` SVECTOR | low |
| `field_C34/C35[]` | byte flags, unknown semantics | low |
| `field_C3C` | event-hook id (-e GCL flag) | medium |
| `field_C40` | sound-buffer cap (-v GCL flag, ≤2 ints) | medium |

## `EnemyCommand` (`enemy/command.h`)

The shared cross-guard coordination block. ~3 KiB struct, ~95% of
fields are opaque:

- `field_0xC8[N_GUARDS]` — sub-struct array, one per active guard.
  Field `.field_04` is known to mean "alert sub-state" (used by the
  phase-in handler in `meryl7`/`watcher`); other fields not pinned
  down.
- `alert_flag` bits — only `COM_ST_DANBOWL = 0x2000` is named; the
  others (0x0001 .. 0xFFFF) are signaled-and-cleared in `command.c`
  but the symbolic meanings are still TBD.

## Magic constants

| Address | Used in | Description |
| ------- | ------- | ----------- |
| `s00a_dword_800C3328` | `watcher.c::WatcherAct` | Looks like a tuning constant; unknown semantic |
| `s00a_dword_800C3348` | ditto | Same |
| `s07a_dword_800C3618[8]` | `meryl7::field_BB0` defaults | Per-action default timing array |
| `s07a_dword_800C35F8[8]` | `meryl7::field_BFC` lookup | Per-guard sound-bank id |
| `s07a_dword_800E3650/4` | meryl7 dymc_seg flags | Bathroom can / stall enable |
| `s07a_dword_800E3658` | meryl7 `-k` flag global | Per-encounter "key"; unclear use |
| `ENEMY_TARGET_SIZE_800C35A4` … `ENEMY_TOUCH_FORCE_800C35CC` | TARGET init | TARGET hitbox sizes/forces — values known but the choice is hand-tuned magic |

## Action callback table

`merylaction.c::EnemyActionMain_800DB1D0` dispatches 52 action
callbacks via a function-pointer table. The table itself is at a
fixed address (`s07a_aMerylActionTable_*`); the *individual* callback
prototypes are reverse-engineered (`work, time` arguments) but
internal behaviour of several entries (e.g. ACTION27..29 hold-and-
neck-snap chain) is still difficult to follow because they share
state via field_C34 byte flags.

## Think state machine

`merylthink.c::s07a_meryl7_*_think` is the largest single function
(several hundred lines of nested switches). The transition graph
exists in code but no symbolic names — entry / exit conditions are
expressed as bit comparisons against `search_flag` and `alert_level`
without comments.

A future pass should:

1. Rename `field_BA3` → `search_flag`; identify the 8 bits.
2. Walk the `merylthink` switch and label each `case` with the
   corresponding ACTIONxx target.
3. Diff merylthink against `enemy/think.c::WatcherThink_*` to
   isolate Meryl-specific transitions.

## Untyped function pointers

Several callbacks pass through `void *` pointers:

- `work->action_callback` field set in `merylenemy.c` to a function
  resolved by `GV_StrCode` of the action-table-name. The address is
  known but the type signature is not formal.
- `dymc_seg.c::s07a_dymc_seg_800D65C8` accepts a `void **arg6` for
  the enable-pointer; in practice always points to a stage-global
  int but the type isn't fixed.

## Camera enemy state

`camera.c` has its own state machine (~1100 lines). All functions are
decompiled but the per-state field names are unknown:

- `field_xx` (state id) takes values 0,1,2 — known to mean
  idle/sweep, suspicious, alarm — but the transitions are tangled
  with timer fields that haven't been pinned to a single name.
- The cone-of-light DG_PRIM building uses several short[] arrays
  that look like animation curves; structure undocumented.

## See also

- [watcher.md](watcher.md), [meryl7.md](meryl7.md), [supports.md](supports.md)
  — the documented surfaces.
- [`source/enemy/enemy.h`](../../../../source/enemy/enemy.h) — the
  authoritative struct definition (contains the still-numbered
  fields).
