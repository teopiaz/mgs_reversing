# `chara/hind2/` — the Hind helicopter (Liquid's chopper)

The Hind D attack helicopter Liquid pilots in the s11g boss fight.
Two files, ~1126 lines:

| File | Lines | Role |
| ---- | ----- | ---- |
| [`hind2.c`](../../../../source/chara/hind2/hind2.c) | 742 | The Hind itself — body, rotor, missile launcher, AI. |
| [`hd_bul2.c`](../../../../source/chara/hind2/hd_bul2.c) | 384 | Hind's bullet/missile spawner — chains to `bullet/amissile.c`. |

The "2" suffix is because there's an earlier `hind.c` somewhere
that was the *first* iteration; this is the released version.

## Hind2Work — state struct

(Recovered shape; full struct is in the c file as not-extern'd.)

```c
typedef struct Hind2Work {
    GV_ACT       actor;
    CONTROL      control;       /* helicopter pos / rot */
    OBJECT       body;          /* "hind" KMD — main fuselage */
    OBJECT       rotor_main;    /* spinning top rotor */
    OBJECT       rotor_tail;    /* tail rotor */
    OBJECT       weapon;        /* missile launcher / gun pod */

    int          state;         /* HIND_PATROL / HIND_AIM / HIND_FIRE / … */
    int          life;
    int          max_life;

    SVECTOR      target_pos;    /* where to fly to */
    int          target_yaw;    /* desired facing */

    GV_ACT      *spotlight;     /* searchlight beam actor */
    int         *spotlight_enable;

    int          time_to_next_volley;
    int          missiles_left;

    /* …more state for cinematic-camera moments… */
} Hind2Work;
```

## Per-tick `Act`

```c
static void Act(Hind2Work *work)
{
    if (HASH_KILL received) destroy + return;

    /* 1. Fly toward target. */
    sub_hover_to_target(work);

    /* 2. Spin rotors. */
    work->rotor_main_yaw += ROTOR_SPEED;
    work->rotor_tail_yaw += ROTOR_TAIL_SPEED;

    /* 3. Run AI state machine. */
    switch (work->state) {
        case HIND_PATROL:    do_patrol(work);    break;
        case HIND_AIM:       do_aim(work);       break;
        case HIND_FIRE:      do_fire(work);      break;
        case HIND_RETREAT:   do_retreat(work);   break;
        case HIND_DYING:     do_dying(work);     break;
    }

    /* 4. Render. */
    DG_SetPos2(&work->control.mov, &work->control.rot);
    GM_ActObject2(&work->body);
    DG_SetPos2(&rotor_pos, &rotor_rot);
    GM_ActObject2(&work->rotor_main);
    /* …same for rotor_tail and weapon */
}
```

The hover-to-target behaviour smooths position via PD-controller-
style integration (delta = target - pos, velocity += delta * k).

## hd_bul2.c — Hind's missile launcher

When `do_fire` is called, it goes through `hd_bul2.c` to spawn the
actual projectiles:

```c
void NewHindBullet2(MATRIX *world, int target_id);
```

Which internally calls:

- `NewAMissile` from `bullet/amissile.c` for the homing missile,
  *or*
- `NewBullet` (hitscan-style) for the gun pod.

Hind's missiles are `PLAYER_SIDE` for damage purposes (they hit
Snake, not other guards).

## Visual

- `hind` KMD — the helicopter fuselage.
- `rotor_main` — top rotor, spinning constantly.
- `rotor_tail` — tail rotor.
- `searchlight` — a separate light-cone actor projecting from
  the nose.
- Smoke trails from the engine intakes when damaged.

The rotor spinning is the same skeletal-bone mechanism as any
other character — assign `rots[N]` for the rotor bone with
`rot.vy += ROTOR_SPEED` per tick.

## Death

When `life <= 0`:

1. Switch to `HIND_DYING`.
2. Spin descend toward ground over ~5 seconds.
3. Spawn smoke + sparks.
4. Crash blast (`NewBlast2 + AN_Blast_high`).
5. `GV_DestroyActor`.

Triggers a stage transition cue — the cinematic that follows
plays once the HIND_DYING actor self-destroys.

## Cinematic role

Hind2 also drives a few cinematic moments — when Liquid taunts
Snake from inside the chopper, the camera locks behind/around
the Hind. Those moments interact with `gUnkCameraStruct2`
similar to `democame.c` (see
[doc/demo/04-camera-pipeline.md](../../demo/04-camera-pipeline.md)).

## See also

- [bullet/amissile.md](../bullet/amissile.md) — Hind's homing
  missile.
- s11g overlay (`source/overlays/s11g/kojo/hind.c`) — the *first*
  iteration of Hind logic, kept around for the s11g boss-arena
  rendering.
