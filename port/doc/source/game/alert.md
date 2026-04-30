---
file: source/game/alert.c
---

# `game/alert.c` — alert state machine

The "!", "?" / `eyeflash` reaction system at the global game-level.
250 lines.

## States

```c
ALERT_NONE       0    // peaceful; default radar
ALERT_CAUTION    1    // "?" — guard heard / saw briefly
ALERT_ALERT      2    // "!" — full pursuit; backup called
ALERT_EVASION    3    // post-alert search; "where did he go?"
```

## State transitions

```
NONE → CAUTION:   any guard reports "saw something" once
CAUTION → ALERT:  guard confirms player; calls in
ALERT → EVASION:  player escapes line-of-sight for N seconds
EVASION → NONE:   timer expires (~2-3 minutes)
```

Transitions are driven by message-passing:

- `enemy/command.c::ENE_RaiseAlert(level)` posts to `RADIO`
  hash → alert.c handler.
- alert.c posts back to `WATCHER` hash → all watchers update
  `alert_level`.

## Timers

Each level has its own duration:

- ALERT timer: ~30 seconds — counts down while in ALERT.
- EVASION timer: ~3 minutes — counts down post-ALERT.

`GM_GameStatus` carries the alert state in upper bits; gameplay
code reads `GM_GameStatus & ALERT_MASK` to branch.

## Effects of alert state

Alert state affects:

- **Music**: BGM swaps to the alert / pursuit theme via
  `game/sound.c`.
- **Guard spawning**: ALERT spawns extra reinforcements (guards
  with route id 100+) in some stages.
- **Camera tilt**: subtle red tint applied via
  `palette.c::DG_StorePaletteEffect` during ALERT.
- **Radar**: red blinking border drawn during ALERT.

## API

```c
void GM_InitAlert(void);
void GM_SetAlert(int level);
int  GM_GetAlertLevel(void);
void GM_ActAlert(void);             // tick (called from gamed.c)
```

## State persistence

Alert state is **not** saved to memcard. Each load starts at
`ALERT_NONE`. So a save during alert resumes peaceful — desired
behaviour, since the player just reloaded.

But the *spawning* config is saved (which set of guards). So the
guards from the alert-spawn pass remain on reload until they
naturally despawn.

## Pitfalls

- **State is global per-stage.** Multi-area stages where the
  player crosses a wall don't reset alert (intentional — guards
  still know about you). Some sub-areas have their own scripted
  pseudo-state but the global remains.
- **Music transitions.** Changing alert level changes music; if
  you set alert during a cinematic, the cinematic music breaks.
  Cinematics gate alert with a flag.

## See also

- [`source/menu/radar.c`](../menu/index.md) — radar binding.
- [`source/enemy/command.c`](../../../../source/enemy/command.c) —
  guard-side alert propagation.
- [`source/game/sound.c`](../../../../source/game/sound.c) — BGM
  switching.
