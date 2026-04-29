# `bullet/bakudan.c` — planted bomb (C4)

A planted explosive (Bakudan = 爆弾, "bomb"). Snake plants this
when using the C4 equipment; sticks to a wall or floor; detonates
on remote signal (player input) or when destroyed by gunfire.

## Public API

```c
void *NewBakudan(MATRIX *world, SVECTOR *pos, int attached, int unused, void *data);
```

`world` — parent matrix (a wall / floor surface or a static
object). `pos` — local offset from the parent. `attached` — 1 if
the bomb sticks to the parent (most cases), 0 if free-standing.
`data` — opaque blob (likely a `BLAST_DATA *` to apply on
detonation).

## Globals

```c
int bakudan_count_8009F42C = 0;        /* # of active C4s */
int time_last_press_8009F430 = 0;      /* throttle for player retrigger */
```

`bakudan_count_*` tracks how many C4s the player has alive.
There's a hard cap (typically 3 in MGS); attempting to plant a
4th detonates the oldest first.

## Lifecycle

1. **Plant** — `NewBakudan` creates the actor, allocates a small
   KMD (the C4 brick model), registers with the parent matrix.
   Increments `bakudan_count_*`.
2. **Idle** — actor renders + waits. No damage, no animation.
3. **Trigger** — player presses Square (the detonator). Each
   bakudan's Act sees `time_last_press > 0`, calls `NewBlast2`
   with the stored data, self-destroys.
4. **Destroyed** — if a bullet hits the bakudan's TARGET, it
   detonates immediately.

## GetNextC4Data

[`bakudan.c:170`](../../../../source/bullet/bakudan.c#L170):

```c
static int GetNextC4Data(void)
{
    /* Walks an internal C4-data ring buffer; returns the next slot. */
}
```

The C4 weapon writes detonation parameters (damage, blast size)
into a ring buffer per shot. When the bakudan detonates, it pops
the matching entry. This indirection lets the player change C4
inventory between plants without affecting already-planted bombs.

## Detonation

When a bakudan detonates:

1. Spawn `NewBlast2(world, &my_blast_data, 1, ENEMY_SIDE)`.
2. Spawn `AN_Blast_Single(&pos)` for the visual.
3. Decrement `bakudan_count_*`.
4. `GV_DestroyActor`.

## Used by

- `equip/box.c` (the cardboard box) — actually no, that's
  separate.
- `weapon/bomb.c` (C4) — calls `NewBakudan` to plant.
- `equip/c4.c` — C4 equipment selection (no `c4.c` in equip/, the
  inventory item is in `weapon/`).

## Pitfalls

- **Cap exceeded**: planting beyond the cap silently detonates the
  oldest. If gameplay code expects a 4th bakudan to spawn, it
  won't — the count stays at the cap.
- **Free-floating C4**: `attached = 0` produces a C4 that doesn't
  stick to anything and stays at world `pos`. Used for cinematic
  pre-plant (s11g hangar). Not normal player-plantable.
