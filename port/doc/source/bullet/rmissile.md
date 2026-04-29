# `bullet/rmissile.c` — RCM (Snake's remote-controlled missile)

The largest projectile in `bullet/` (1013 lines). Snake's RCM is
the steerable mini-missile he fires from a launcher; the player
takes camera control while it's in flight, steers it with the
d-pad, and detonates it with a button press.

This actor handles:

- Missile flight + visual.
- Player-driven steering input each tick.
- A *new camera channel* — the camera locks behind the missile
  for the player's POV.
- Detonation (manual, on impact, or fuse expiry).

## Public API

```c
void *NewRMissile(MATRIX *world, SVECTOR *initial_velocity, int fuse);
```

(Recovered signature.)

The factory swaps the player into camera-follow mode, suspends
Snake's normal Act (he stays frozen at launch pose), and spawns
this actor at level 5.

## Steering

Each tick `Act` reads `GM_CurrentPadData` and updates velocity:

```c
if (pad->status & PAD_UP)    pitch = -8;
if (pad->status & PAD_DOWN)  pitch = +8;
if (pad->status & PAD_LEFT)  yaw   = -8;
if (pad->status & PAD_RIGHT) yaw   = +8;

work->rot.vx += pitch;
work->rot.vy += yaw;

speed_in_facing_direction(&work->speed, &work->rot, magnitude);
```

The numeric values produce a tight turning radius — too tight is
unfun, too loose makes the steering imprecise. The released MGS
values came after a lot of designer iteration.

## Camera channel

The RCM gets its own `DG_CHANL` (typically `DG_Chanls[1]` or a
dedicated extra channel) that follows the missile. The actor:

```c
DG_LookAt(my_chanl, &work->pos_minus_back, &work->pos, FOV);
```

— eye behind the missile, target = missile position. The player
sees through the missile's "eyes".

Snake's normal channel keeps rendering, but the active viewport
binds to my_chanl.

## Detonation

Three triggers:

1. **Manual** — player presses Square (the detonator). `Act`
   calls `NewBlast2` and self-destroys.
2. **Impact** — wall / floor / TARGET hit. Same blast sequence.
3. **Fuse expiry** — fuse ticks down each frame; at 0, detonate
   (anti-grief — limits how long the player can fly the missile).

## Trail

Spawns `AN_Smoke_800CE164` (the parametric trail variant) every
~2 ticks behind the missile, producing the characteristic visible
flame-trail.

## Why so much code (1013 lines)

- Camera-channel setup + restore (~200 lines).
- Steering + flight + collision (~300 lines).
- Trail effect + smoke + flame variants (~150 lines).
- Detonation + cleanup (~100 lines).
- HUD overlay during flight (crosshair, fuse timer) (~150 lines).
- Per-stage tuning quirks (some stages disable RCM steering for
  scripted moments) (~100 lines).

## Pitfalls

- **Camera flicker on detonation**: the channel restore happens
  in `Die`, so if `Die` is called before the next render frame,
  there's one frame of "no camera". Mitigated by setting a
  fadeout flag.
- **RCM through walls**: the missile uses `step_size > 0` so it
  collides. Sometimes during high-speed flight, integration steps
  through a thin wall. Mitigated by raycasting between previous
  and current pos rather than just testing current.

## Used by

- `weapon/rcm.c` — Snake's RCM weapon equipment.
- (Not used elsewhere — RCM is player-only.)
