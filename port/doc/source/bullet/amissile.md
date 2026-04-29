# `bullet/amissile.c` — auto-homing enemy missile

Enemy launcher's missile. Spawned at the launcher, locks onto
Snake (or whoever the launcher's TARGET is), steers toward them
with limited turning rate, detonates on hit / fuse expiry.

Fired by:
- M1A1 tank turret (boss fight in s07c).
- Hind (boss fight in s11g).
- Sniper Wolf's anti-personnel mortar (less common).

## Public API

```c
void *NewAMissile(MATRIX *world, TARGET *target, int side, int speed);
```

(Recovered signature.)

`target` — what to home on (typically Snake's `GM_PlayerControl
->event` target). `side` — `PLAYER_SIDE` so it damages Snake.
`speed` — base velocity magnitude; the missile maintains this
while turning.

## Homing

Each tick:

```c
SVECTOR target_pos = work->target->event.pos;
SVECTOR delta;
GV_SubVec3(&target_pos, &work->pos, &delta);

short desired_yaw   = ratan2(delta.vx, delta.vz);
short desired_pitch = -ratan2(delta.vy, GV_Magnitude(&delta));

/* Limit turn rate to avoid impossible-to-dodge missiles. */
work->rot.vy = approach(work->rot.vy, desired_yaw, MAX_TURN_PER_TICK);
work->rot.vx = approach(work->rot.vx, desired_pitch, MAX_TURN_PER_TICK);

/* Speed in facing direction. */
work->speed.vx = (sin(rot.vy) * cos(rot.vx) * work->base_speed) / 4096;
/* …etc. */
```

The `MAX_TURN_PER_TICK` is around 64 PSX-fixed (a bit under 6° per
tick at 30Hz) — enough to track Snake but slow enough that
sprinting at right angles can outrun the missile.

## Detonation

Same as other projectiles — `NewBlast2 + AN_Blast_Single` on
impact. The blast `BLAST_DATA` carries enough damage to kill
Snake outright at full HP (intentional — missiles are scary).

## Visual

- KMD: small missile model, rotating.
- Trail: `AN_Smoke_800CE164` (linear smoke).
- Flame: a small particle at the tail.

## Pitfalls

- **Target dies during flight**: `work->target->event.pos`
  remains the last-known position. The missile homes there +
  detonates. Slight flaw — could be fixed to find a new target,
  but never was.
- **Multiple missiles, no de-dup**: the launcher fires up to 3
  in quick succession; all three home on Snake. Stacking damage
  is what makes the boss fights tense.

## Used by

- `chara/hind2/hd_bul2.c` — Hind helicopter missile launch.
- s07c boss code — M1A1's missile.
- (Not the standard `weapon/` tree — these come from boss
  actors.)
