---
file: source/game/target.c
---

# `game/target.c` — TARGET hit system

The damage / hitbox / collision-resolution layer. Every entity that
can be shot, punched, or interacted-with owns one or more `TARGET`s.
726 lines.

## TARGET — the unit

A TARGET is a hitbox + metadata. `TARGET` types are small (~64
bytes) and pooled.

```c
typedef struct TARGET {
    int      class;       // FLAG | POWER | TOUCH | AVAIL | side bits
    int      damaged;     // result bits set by the system after a hit
    short    side;        // PLAYER_SIDE / ENEMY_SIDE
    short    pad;
    SVECTOR  size;        // hitbox half-extents
    SVECTOR  force;       // damage / knockback amount
    short    life;        // HP for damage targets
    short    faint;       // stamina (non-lethal damage)
    MATRIX  *body;        // attached matrix (follows the entity)
    // ... more
} TARGET;
```

### Class flags

```c
TARGET_FLAG    = bullet/damage receiver       (e.g. enemy body)
TARGET_POWER   = melee attack-out             (e.g. enemy fist)
TARGET_TOUCH   = touch / collide              (e.g. trip-wire)
TARGET_AVAIL   = "free", not in use
| ENEMY_SIDE / PLAYER_SIDE                    (which "team")
```

A target with `class & TARGET_FLAG` will have damage written to it
when a TARGET_POWER source touches it. A target with `TARGET_TOUCH`
fires a hit *event* without taking damage.

## API

```c
TARGET *GM_AllocTarget(void);                       // pool alloc
void    GM_FreeTarget(TARGET *t);                    // pool free
void    GM_SetTarget(TARGET *t, int class, int side, SVECTOR *size);
void    GM_TargetBody(TARGET *t, MATRIX *body);      // attach to entity matrix
void    GM_MoveTarget(TARGET *t, SVECTOR *new_pos);  // update position
void    GM_PushTarget(TARGET *t);                    // queue for collision pass
void    GM_TouchTarget(TARGET *t);                   // queue for touch pass
void    GM_Target_8002DCCC(TARGET *t, ...);          // configure damage params
void    GM_Target_8002DCB4(TARGET *t, ...);          // configure stamina params
```

## The collision pass

Each frame, after all actors update:

1. Every active actor calls `GM_PushTarget` with its TARGETs.
2. The dispatcher walks the pushed list pairwise (N²) — but with
   side filtering, so PLAYER_SIDE never collides with PLAYER_SIDE.
3. For each pair where AABBs overlap (using `size`):
   - If src is `TARGET_POWER` and dst is `TARGET_FLAG`: subtract
     `force` from dst's `life`; set `dst.damaged |= TARGET_FLAG`.
   - If src is `TARGET_TOUCH` and dst is `TARGET_TOUCH`: set
     both `damaged |= TARGET_TOUCH` (mutual touch event).

After the pass, every actor reads its own `target->damaged` to
detect what happened this frame, then clears it.

The N² check is fast in practice because: very few TARGETs at once
(< 50 typical), AABB test is cheap, and a side-filter cuts the
work in half.

## Faint vs life

Two HP bars per target:

- `life` — lethal damage (bullets, explosions). Reach 0 → death.
- `faint` — stun damage (punches, stun-grenades). Reach 0 → unconscious.

Used by `enemy/check.c` to decide between `ACTION15` (bullet hit)
and `ACTION16` (knockout) and `ACTION30` (death).

## TARGET attachment

`GM_TargetBody(target, &body.objs->objs[N].world)` links the target
to a body-bone matrix. Each frame the system reads the matrix's
translation as the target's *current* position, so the hitbox
follows the bone. No code per-frame needs to update positions
manually — just `GM_PushTarget` and the system picks up the matrix.

## `GM_AllocHomingTarget`

A specialised TARGET variant — HOMING_TARGET — used by the laser
sight / eyeflash to "lock onto" a chara's body. Stored separately
from the regular pool.

```c
HOMING_TARGET *GM_AllocHomingTarget(MATRIX *body, CONTROL *ctrl);
void           GM_FreeHomingTarget(HOMING_TARGET *h);
```

The laser-sight beam queries `GM_FindHomingTarget(player_pos, range)`
to find the nearest enemy, draws the beam to that target's body.

## `class` flags as state machine

Setting `class = TARGET_AVAIL` returns the TARGET to the pool
without actually freeing it — the pool reuses the slot. So enemy
death does:

```c
work->target->class = TARGET_AVAIL;
```

instead of `GM_FreeTarget`. The next allocator call will reuse the
slot.

## Pitfalls

- **`damaged` accumulates until cleared.** If you don't read +
  clear `damaged` every frame, multiple hits stack and confuse
  the action callback.
- **TARGET sides.** Forgetting to set `side` makes the target
  collide with itself or its team — usually a quick visible bug
  (enemy taking damage from itself).
- **Pool exhaustion is silent.** `GM_AllocTarget` returns NULL on
  exhaustion; calling code that doesn't check NULLs out targets
  silently. (See watchers / merylenemy.c — they all guard with
  `if (target = GM_AllocTarget()) ...`.)
- **`GM_MoveTarget` is redundant if `body` is set.** `body` matrix
  drives position automatically. `MoveTarget` is for unattached
  targets (most are attached).

## See also

- [`source/enemy/enemy.c`](../../../../source/enemy/enemy.c) —
  consumers (every guard's TARGETs).
- [`source/chara/snake/`](../chara/snake.md) — player's TARGETs
  for pickup detection + attack-out.
- [`source/bullet/`](../bullet/index.md) — bullets are TARGETs
  that fire on collision.
