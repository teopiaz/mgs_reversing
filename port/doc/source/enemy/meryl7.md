---
file: source/enemy/meryl7.c + merylaction.c + merylcheck.c + merylenemy.c + merylthink.c
---

# `meryl7` — Meryl as enemy (s07a / s09)

The `MERYL7` actor is the *enemy* version of Meryl Silverburgh, used in
the bathroom-encounter sequence of stage **s07a** (and reused in s09a
where she's still acting hostile). It pre-dates the player's first
"reveal" of Meryl as ally; from the AI's perspective she is a watcher
with extra animations and a custom message handler.

| File | Lines | Role |
| ---- | ----- | ---- |
| [meryl7.c](../../../../source/enemy/meryl7.c) | 784 | Factory + GCL options + per-tick Act + Die |
| [merylaction.c](../../../../source/enemy/merylaction.c) | 1992 | Action-state callbacks (one fn per ACTIONxx) |
| [merylcheck.c](../../../../source/enemy/merylcheck.c) | 379 | Per-frame condition tests |
| [merylenemy.c](../../../../source/enemy/merylenemy.c) | 433 | Cross-cutting helpers (TARGET init, comm-state init) |
| [merylthink.c](../../../../source/enemy/merylthink.c) | 2927 | High-level decision making |
| [meryl.h](../../../../source/enemy/meryl.h) | — | ACTIONxx enum, COM_ST_DANBOWL flag |

> Total: **5266 lines**, all decompiled. Most functions begin with the
> `s07a_meryl7_800Dxxxx` raw-address prefix — they originated by
> verbatim copy from `enemy/watcher.c` and were diffed by the
> decompiler tooling.

## Why a copy of `WATCHER`

The codebase has *two* live copies of nearly every guard function: the
generic one in `enemy/` and a per-encounter mutant. `meryl7` is one of
the largest such mutants because she has the richest single-encounter
animation set (52 actions vs. ~20 for a regular watcher), and several
state-machine special cases (the bathroom-stall hide, the can-kick
trigger, the surprise wake-up).

The Japanese comment at the top of `meryl7.c` reads:

> ルート変更フラグチェック / 指定フラグが立てば次のルートへ変更 /
> ここではコマンダーに変更ルートをセットする

— "When the flag is raised, change route. Here we set the
COMMANDER's change-route. After change, each enemy soldier
will swap routes at the next point-action."

In other words: the standard route-change message handler was edited
in place to add the bathroom dynamic-segment handoff, then everything
downstream of it had to be copied because the message dispatcher is
an inline switch statement.

## Action ID enum (`meryl.h`)

`meryl.h` enumerates 52 action states. Highlights:

| ID | Name | Animation |
| -- | ---- | --------- |
| 0 | STANDSTILL | Idle pose |
| 1–6 | ACTION1..ACTION6 | Walk / run / aim / fire variations |
| 7 | GRENADE | Throw grenade |
| 8 | ACTION8 | Pistol-whip |
| 9–14 | ACTION9..ACTION14 | Looking around (right / left / down / crouch / up) |
| 15–16 | ACTION15..ACTION16 | Hit-from-front / hit-from-back |
| 17 | DANBOWLKERI | Kick the bottle on the floor (the bathroom can) |
| 18 | DANBOWLPOSE | Pose looking at the bottle |
| 19–24 | ACTION19..ACTION24 | Stretching / sleeping / waking / radio / neck / sneezing |
| 27–34 | Held / neck-snap variations |
| 35–47 | Knockdown / get-up sequences |

The ACTIONxx labels are decompiler-imposed; the names beside them
come from frame-by-frame inspection of the KMD animations. Where two
IDs share a comment ("SAME ID AS 40"), the second is a reused
animation slot the action callback selects between by parameter.

`COM_ST_DANBOWL = 0x2000` is the ENEMY_COMMAND.alert_flag bit set
when *any* guard sees Snake kick the bathroom can. Bit propagates to
all guards in the COMMANDER's `field_0xC8[]` table, so all of them
swivel and aim at the can simultaneously.

`SP_DANBOWLKERI = 0x400000` is the *search-flag* bit on a guard's
WatcherWork that means "this guard is the can-kicker". Set during
`merylthink.c` when path planning routes the guard near the can.

## `EnemyMerylAct_800D5638` per-tick flow

A modified version of `WatcherAct_800C430C` — five lines were added,
the GM_TouchTarget handling re-ordered:

```c
EnemyMerylAct(work):
    if (CheckMessage(HASH_KILL)) → DestroyActor
    s07a_meryl7_800D52FC(work)        // route-change + STATE_ENEMY_OFF
    s07a_meryl7_800D55A8(work)        // dymc_seg flag update (bathroom stall)
    s07a_meryl7_800D5614(work)        // player-in-zone test
    if (!faseout) {
        EnemyPushMove_800DB23C(work)  // collision push
        GM_ActControl(ctrl)            // CONTROL pass
        GM_ActObject2(body)
        GM_ActObject2(weapon)
        DG_GetLightMatrix2(...)
        EnemyActionMain_800DB1D0(work) // DISPATCH to ACTIONxx callback
        TARGET update
        if (target.class & TARGET_TOUCH)
            field_94C TOUCH handling   // ← the can-kick check
        ScaleMatrix(body)
    }
    s07a_meryl7_800D53A4(work)         // visibility update
```

The bathroom can is implemented as a **dynamic HZD segment**
(`dymc_seg.c`) — see `s07a_meryl7_800D5E34`. Two segments are
allocated at GCL-init time, one for the can-zone (flag 0xFE), one for
the toilet-stall door (flag 0xF7). Each segment's enabled state is
toggled by writing to a global pointer (`s07a_dword_800E3650` /
`s07a_dword_800E3654`); the watcher writes to those globals every tick
based on its own position and the player's, so HZD collision raycasts
see/don't see the segment as Meryl moves through.

## Mesg handlers

`s07a_meryl7_800D50F8` — modified `RootFlagCheck_800C3EE8`. The last
two switch cases (alarm-propagate and chaff-radio) were removed
because Meryl-the-enemy doesn't participate in alarms. Three remain:

| `message[0]` | Handler |
| ------------ | ------- |
| `0x430F` | Change-route — overwrite `param_c_root` with new route id, recompute `field_B7C` (HZD address) |
| `0xF1BD` | Phase-out — clear hom flag, alert_level = 0, target.class = TARGET_AVAIL, set faseout = 1, act_status = EN_FASEOUT |
| `0x1DC4` | Phase-in — restore visible if EnemyCommand.field_0xC8[N].field_04 == 2, restore TRAP attribute, clear faseout |

## GCL options (`EnemyMerylGetResources_800D5F24`)

Same option layout as `WATCHER` plus three additions:

| Flag | Meaning | Notes |
| ---- | ------- | ----- |
| `-p` | KMD chara position | inherited |
| `-d` | KMD chara direction | inherited |
| `-r` | route id | default 0 |
| `-l` | param_life | default 192 |
| `-f` | param_faint | default 10 |
| `-b` | param_blood | default 65 (= 'A'); 'Z' suppresses muzzle-flash |
| `-g` | field_B81 (gun-glow override) | default 0xFF |
| `-e` | field_C3C (event hook) | default -1 |
| `-k` | s07a_dword_800E3658 (global key flag) | default -1 |
| `-v` | sound-buffer params (≤2 ints) | calls 800D5DD4 |
| `-n` | named-position spawn point | resolves via `HZD_GetAddress` |
| `-a` | param_area | default 'A'; 'S' = "snowy" → enables PUTBREATH white-breath fx |
| `-s` | scale offset | default 4096 |
| `-y` | field_B7B — *KMD swap mode* | 1 = swap to LOPRYHEI when behind cam |
| `-c` | enable footstep markers | sets field_BA3 \|= 0x10 |
| `-t` | timing array | up to 4 ints into field_BB0[1..4] |
| `-i` | direction array | up to 4 angles into field_BD0 |

KMD swap (`-y 1`) is unusual — when the camera is behind the player
or in first-person, Meryl's body objs are re-bound to a low-poly
KMD (`HASH_LOPRYHEI`) to save GTE budget. `s07a_meryl7_800D5328` does
the actual `obj.def` rebinding.

## Action dispatch — `EnemyActionMain_800DB1D0`

Lives in `merylaction.c`. A 52-entry function-pointer table indexed by
`work->action`. Each entry runs for `work->time` ticks then advances
to the next action ID (or branches based on conditions).

The `merylthink.c` block decides what to enqueue:

```
think.c::DecideAction():
    if alert_level >= 3 → ACTION3 (aim) → ACTION4 (fire) → ACTION6 (crouch fire)
    else if can-kick triggered → DANBOWLKERI → DANBOWLPOSE
    else if hot → ACTION23 (rub neck)
    else if bored → ACTION19/20 (stretch / sleep)
    else → patrol path
```

Several states *gate* on TARGET hit results:

- ACTION15/16 are entered when `target.damaged & TARGET_FLAG` (took
  bullet) and `field_904.class & TARGET_POWER` (took melee).
- ACTION27..ACTION34 are the held / neck-snap chain — set when
  Snake's CHARA grabs Meryl. Snake's `chara/snake/` calls
  `GM_SendMessage` with a hash that triggers ACTION27.

## Combat TARGETs

Same 4-TARGET layout as WATCHER:

```
work->target           — main HP/faint target (TARGET_FLAG bullet test)
work->field_904        — melee attack-out target (TARGET_POWER)
work->field_94C        — touch box (TARGET_TOUCH)
work->punch            — pistol-whip target (set in ACTION8)
```

Initialised in `s07a_meryl7_800D5780` — identical to
`InitTarget_800C444C` from `watcher.c`.

## Cross-references

- [watcher.md](watcher.md) — the parent function set; meryl7 is
  diff-from-watcher.
- [animal/meryl72.md](../animal/meryl72.md) — Meryl-as-ally (post
  s09 reveal), used in s11 / s11d. Different chara, shared
  CONTROL/MOTION primitives, but not an `enemy/` actor.
- [`source/enemy/dymc_seg.c`](../../../../source/enemy/dymc_seg.c) —
  dynamic HZD segments used by the bathroom can / stall.
- [_unreversed.md](_unreversed.md) — opaque areas (action callback
  pointer table layout, merylthink internal state).
