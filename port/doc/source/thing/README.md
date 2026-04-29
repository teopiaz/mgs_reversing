# `source/thing/` — non-character actors

A small folder of "things" that are spatial actors but aren't
characters. Everything here is spawned by GCL `chara &TYPE`
directives but represents a *prop* or *effect* rather than an
NPC.

## Files

| File | Role |
| ---- | ---- |
| [`door.c`](../../../../source/thing/door.c) | The `DOOR` chara — opens / closes via mesg, animation, sound. |
| [`emitter.c`](../../../../source/thing/emitter.c) | The `EMITTER` chara — particle / sprite spawner. Drives `anime/effect/*` factories at scripted intervals. |
| [`sgtrect3.c`](../../../../source/thing/sgtrect3.c) | "Sprite-rect 3D" — billboard sprite with 3D positioning. |
| [`sight.c`](../../../../source/thing/sight.c) | The `SIGHT` chara — vision-cone visualisation (debug overlay). |
| [`snow.c`](../../../../source/thing/snow.c) | Snow particle field — the falling snowflakes in cold stages. |
| [`sphere.c`](../../../../source/thing/sphere.c) | Sphere primitive (debug / placeholder geometry). |

## EMITTER — the GCL effect spawner

The most-used chara in this folder. GCL syntax:

```gcl
chara &EMITTER $s:NAME \
    -t $s:texture_hash \    # which effect to spawn
    -p P0 P1 P2 \            # 3 spawn anchor points
    -s SX SY SZ \            # particle scale envelope
    -l LIFETIME \            # frames to keep emitting
    -r RATE \                # spawn rate
    ...
```

Inside `New<Emitter>` the actor reads these options and, every
N ticks, calls one of the `AN_*` factories from
`anime/effect/*.c`:

```c
static void Act(EmitterWork *work) {
    if (work->time % work->rate == 0) {
        AN_Smoke_*(work->pos);   /* or whatever effect */
    }
    work->time++;
    if (work->time >= work->lifetime) GV_DestroyActor(...);
}
```

Used pervasively in cinematic stages — d00a's helipad spawns
~10 EMITTERs for the snow drifts, gun smoke, light shafts.

## SNOW — falling snowflakes

`snow.c` is a **larger** version of EMITTER specifically for the
snowy outdoor stages. Spawns a *cloud* of falling snowflake
sprites that rendered in front of the camera, gives the
illusion of weather.

The implementation:

- Allocates a fixed pool of ~64 flakes.
- Each flake has its own pos / velocity (gentle gravity +
  rightward drift).
- When a flake falls below the screen, it respawns at the top
  with a new random X.
- Lifetime is "stage-long" — the actor never self-destroys
  while the player is in a snow stage.

## DOOR — interactive openable

`door.c` is the canonical interactive prop. Listens for `mesg
HASH_OPEN` / `HASH_CLOSE`, plays an animation, blocks player
movement when closed, allows when open. The HZD wall in front of
the door is enabled / disabled via `HZD_EnableSeg` /
`HZD_DisableSeg` per state.

## SIGHT — vision-cone visualiser

When debug mode is on (or in some stages always), guards spawn
a SIGHT actor that renders a translucent cone showing the
guard's vision range. Used for both gameplay (radar visualisation
of guard sight) and debugging.

## Used by

- Stage GCL scripts spawn DOORs and EMITTERs liberally.
- Cinematic stages spawn SNOW for atmospheric effect.
- Debug builds spawn SIGHT around every guard.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [things.md](things.md) | door / emitter / sight / sgtrect3 / snow / sphere |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`source/anime/effect/`](../anime/effect.md) — what EMITTER
  ultimately spawns.
- [`source/libhzd/dynamic.c`](../../../../source/libhzd/dynamic.c)
  — DOOR uses HZD_EnableSeg to toggle wall collision.
