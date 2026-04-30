# `source/bullet/` — projectiles & explosives

Six projectile actors. Each one is the *thing* a weapon spawned —
the bullet, the grenade in flight, the planted mine, the homing
missile. They live a few seconds, follow a trajectory, deal damage
on hit, and self-destroy.

| File | Lines | Hash | Spawned by | What it is |
| ---- | ----- | ---- | ---------- | ---------- |
| [`blast.c`](../../../../source/bullet/blast.c) | 477 | n/a (helper) | weapon impact code | An explosion — the dispersing damage volume after a bomb / grenade / RCM / missile. Also exports six `AN_Blast_*` factories that spawn the visual effect. |
| [`bakudan.c`](../../../../source/bullet/bakudan.c) | 294 | `0x???` | C4 / claymore equip | A planted explosive (Bakudan = 爆弾, "bomb"). Sticks to a wall or floor; detonates on remote signal or trigger. |
| [`jirai.c`](../../../../source/bullet/jirai.c) | 676 | `0x???` | mine / claymore | A floor mine (Jirai = 地雷, "land mine"). Triggers on proximity. |
| [`tenage.c`](../../../../source/bullet/tenage.c) | 338 | n/a | grenade weapon | A grenade in flight (Tenage = 手投げ, "hand-thrown"). Arcs through the air, bounces on impact, detonates after fuse timer. |
| [`rmissile.c`](../../../../source/bullet/rmissile.c) | 1013 | `0x???` | RCM weapon | Snake's RCM remote-controlled missile. Largest of the bunch — has steering / camera-follow / detonate-on-button logic. |
| [`amissile.c`](../../../../source/bullet/amissile.c) | 479 | `0x???` | enemy missile launcher | An auto-homing missile fired *at* Snake by enemy. |

Total: 3277 lines. Fully decompiled.

## What's *not* here

There's no "bullet.c" — small-arms gunfire (SOCOM, FAMAS, sniper)
doesn't spawn a projectile actor. Instead the weapon does
hitscan: instant ray vs HZD wall + TARGET intersection in the
weapon's `Act()`. The visual effect is just smoke trail + impact
spark. That code lives in `source/weapon/`.

So `bullet/` is **only** for projectiles whose trajectory matters
across multiple frames — slow enough that they need their own
actor and physics step.

## Common pattern — bullet/explosive actor

Every file in this folder follows the same shape (with minor
variations):

```c
typedef struct _Work {
    GV_ACT     actor;                /* engine actor header */
    SVECTOR    pos;                  /* world position */
    SVECTOR    speed;                /* per-tick velocity */
    int        time_left;            /* fuse / lifetime */
    TARGET     target;               /* damage volume */
    DG_OBJS   *objs;                 /* visual KMD (or NULL — invisible) */
    /* …per-projectile state… */
} Work;

static void Act(Work *work)
{
    if (work->time_left-- <= 0) {
        explode_or_die(work);
        GV_DestroyActor(&work->actor);
        return;
    }

    /* Integrate position. */
    work->pos.vx += work->speed.vx;
    work->pos.vy += work->speed.vy;
    work->pos.vz += work->speed.vz;

    /* Apply gravity (for grenades, missiles) or just drift. */
    work->speed.vy += GRAVITY;

    /* Test collisions. */
    if (HitWall_or_Floor(work)) {
        explode_or_bounce(work);
    }
    if (TargetHit(&work->target)) {
        damage_target(...);
        GV_DestroyActor(&work->actor);
        return;
    }

    /* Render. */
    GM_MoveTarget(&work->target, &work->pos);
    if (work->objs) {
        DG_SetPos2(&work->pos, &rot);
        DG_PutObjs(work->objs);
    }

    /* Spawn trail effect. */
    AN_Smoke_*(...);
}

void *New<Name>(MATRIX *world, ...)
{
    Work *work = GV_NewActor(GV_ACTOR_LEVEL5, sizeof(Work));
    if (work) {
        GV_SetNamedActor(&work->actor, Act, Die, "<name>.c");
        GetResources(work, ...);
    }
    return work;
}
```

Variations:

- **blast** — has no `pos.speed` (it's stationary); has a
  *radius* that grows over time as the explosion expands.
- **bakudan / jirai** — stationary; waits for a trigger.
- **tenage** — has gravity arc + bounce.
- **rmissile** — accepts pad input each tick to update `speed`.
- **amissile** — has a homing-target field; updates `speed` to
  steer toward it.

## Components

- [blast.md](blast.md) — explosion + the six `AN_Blast_*` visual
  effects.
- [bakudan.md](bakudan.md) — planted bomb (C4-style).
- [jirai.md](jirai.md) — proximity mine.
- [tenage.md](tenage.md) — thrown grenade.
- [rmissile.md](rmissile.md) — Snake's RCM.
- [amissile.md](amissile.md) — enemy homing missile.
- [_unreversed.md](_unreversed.md) — opaque fields per file.

## See also

- [`source/weapon/`](../../../../source/weapon/) — the weapons
  that *spawn* these projectiles. Each `weapon/<name>.c` calls
  the matching `bullet/<name>.c::New*`.
- [`source/equip/`](../../../../source/equip/) — equipment that
  triggers spawn (claymore, mine selectors).
- [`source/game/target.c`](../../../../source/game/target.c) — the
  TARGET volume each bullet uses for hit detection.
- [`source/game/homing.c`](../../../../source/game/homing.c) — the
  HOMING math used by amissile.
- [anime/effect.md](../anime/effect.md) — the smoke / blast
  particle effects projectiles spawn.
