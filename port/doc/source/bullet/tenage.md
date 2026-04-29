# `bullet/tenage.c` — thrown grenade

A grenade in flight (Tenage = 手投げ, "hand-thrown"). Arcs
through the air, bounces on impact, fuses to detonation. Spawned
by Snake's grenade weapon and by enemy grenade-throw actions
(`enemy/grnad_e.c`).

## Public API

```c
void *NewTenage(MATRIX *world, SVECTOR *velocity, int side, int fuse_ticks);
```

(Recovered signature.)

`world` — spawn matrix (the thrower's hand bone). `velocity` —
initial velocity vector. `side` — PLAYER_SIDE or ENEMY_SIDE
(passed to NewBlast on detonate). `fuse_ticks` — frames until
detonation regardless of bounces.

## Trajectory

Each tick:

1. `pos += velocity`.
2. `velocity.vy += GRAVITY`. (PSX gravity ≈ 80 PSX-units/tick²)
3. Test floor collision via `HZD_GetFloor`.
4. If hitting floor: invert + dampen `velocity.vy`. Bounces ~3
   times before settling.
5. Test wall collision: bounce off (reflect velocity along wall
   normal).
6. Decrement fuse; if ≤ 0, detonate.

The bounce dampening is around 0.3× — each bounce loses 70% of
vertical energy, settling within a second. Horizontal velocity
drops too (friction).

## Detonation

When fuse expires:

1. Spawn `NewBlast2(&world_at_pos, &grenade_blast_data, 1, side)`.
2. `AN_Blast_Mini(&pos)` for visual.
3. `GV_DestroyActor`.

## Visual

The grenade renders a small KMD (the grenade model) rotating as
it tumbles through the air. Rotation rate is roughly proportional
to velocity magnitude.

A trail effect (`AN_Smoke_*`) is spawned every few ticks to leave
a fading smoke trail.

## Side & damage

Same side-mismatch logic as blast.c — grenade thrown by Snake at
guards = `ENEMY_SIDE`; grenade thrown by guard at Snake =
`PLAYER_SIDE`. Catching your own grenade early (mid-bounce) does
nothing; only the blast at fuse expiry damages.

## Pitfalls

- **Out of stage**: a grenade thrown off a ledge falls forever,
  but `time_left` (fuse) ensures it eventually detonates. The
  blast at extreme Y values does nothing visible.
- **Stuck in a wall**: rare — if the grenade enters a corner
  during integration, the wall-bounce reflection may not push it
  out. The fuse still expires normally.

## Used by

- `weapon/grenade.c` — Snake's grenade weapon.
- `enemy/grnad_e.c` — enemy grenade-throw action.
- `animal/meryl72/action.c` — Meryl's `ActGrenade_800C9790` —
  she throws grenades alongside Snake.
