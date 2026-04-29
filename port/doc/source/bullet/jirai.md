# `bullet/jirai.c` — proximity mine

A floor-laid mine (Jirai = 地雷, "land mine"). Triggers on
proximity — anyone walks within range, it explodes. The largest
file in `bullet/` (676 lines) because mines have richer state
than other projectiles:

- Multiple mine types (Snake's claymore, Wolf's sniping pad,
  pre-placed enemy mines).
- Detection / disarm logic.
- Visual cue when armed (LED pulse).
- Stage-saved state (mines persist across stage transitions if
  the player exits + re-enters).

## Public API

```c
void *NewJirai(MATRIX *world, SVECTOR *pos, int type, int radius,
               int armed_after_ticks, void *data);
```

(Exact signature is module-internal; this is the recovered shape.)

`type` selects the mine variant — claymore (directional), proxy
mine (omnidirectional), Sniper Wolf's "anti-personnel" mines.
`radius` is the trigger distance. `armed_after_ticks` delays
arming so Snake doesn't immediately blow himself up planting one.

## Mine types (recovered)

| Type | Trigger | Use |
| ---- | ------- | --- |
| Claymore | front 90° cone, ~3000 units | player's claymore weapon |
| Proxy | omni, ~2500 units | enemy pre-placed mines |
| Sniping pad | omni, smaller | Wolf-arena mines that Wolf herself avoids |

The `type` field selects which TARGET volume + which sprite to
render.

## Detection

Snake can find mines via:

- **MD (mine detector)**: ranges over the field, beeps proportional
  to distance.
- **Thermal vision**: shows mine sprites with a heat tint.
- **Walking too close while armed**: triggers, takes damage.

The mine's `Act` checks `GM_PlayerStatus & PLAYER_THERMAL` and
makes itself rendered visible (otherwise hidden under the floor).
That's why mines pop into view when you switch to thermal.

## Disarm

Player walking *very* slowly (with the cardboard box equipped or
crouching) can step on a mine without triggering — the mine
detects vertical pressure < threshold and stays armed. If Snake
collects a mine via the equipment menu, the actor's `Die` path
runs and the mine's data goes into Snake's inventory.

## Stage-persistent state

Unique among bullet/ — mines survive stage transitions. When the
player leaves a stage, mines are serialised to a global table and
re-spawned on re-entry. The serialisation format isn't documented
(probably in `memcard/` save code); see `_unreversed.md`.

## Pitfalls

- **Type mismatch**: passing `type = 0` produces a default
  claymore which may have wrong sprite for an enemy mine.
- **Radius vs visibility**: mines render with a fixed sprite size
  regardless of trigger radius, so a small-radius mine can
  visually look "in range" but not trigger.

## Used by

- `weapon/mine.c` — Snake's claymore weapon spawns these.
- `enemy/check.c` — guards' "spotted-by-mine" reaction.
- s07c / s11e stages pre-spawn enemy mines via GCL.
