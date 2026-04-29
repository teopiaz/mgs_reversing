# `bullet/blast.c` — explosions

The damage volume + visual effect for any explosion in the game.
Spawned by every weapon/projectile that detonates: grenades,
mines, RCMs, claymores, enemy missiles, even the door C4 in s11g.

## Public API

```c
typedef struct BLAST_DATA {
    int field_0;       /* hp / damage value */
    int field_4;       /* (purpose unclear) */
    int field_8_z;     /* size dimension Z */
    int field_C;       /* size dimension X/Y? */
    int weapon;        /* originating weapon id (WP_*) */
} BLAST_DATA;

void *NewBlast(MATRIX *world, BLAST_DATA *data);
void *NewBlast2(MATRIX *world, BLAST_DATA *data, int doSound, int side);

void AN_Blast_Single(SVECTOR *pos);     /* visual-only */
void AN_Blast_Mini(SVECTOR *pos);
void AN_Blast_Minimini(SVECTOR *pos);
void AN_Blast_Rand(SVECTOR *pos);
void AN_Blast_high(SVECTOR *pos);
void AN_Blast_high2(SVECTOR *pos, SVECTOR *offset);
```

`NewBlast` spawns a damage-only blast (caller draws their own
visuals). `NewBlast2` adds sound + side selection (PLAYER_SIDE vs
ENEMY_SIDE). The `AN_Blast_*` factories are visuals-only — they
don't do damage, just particle effects via `NewAnime`.

## Lifecycle

The blast actor lives ~30-60 ticks. Each tick:

1. Expands its hit-volume radius (small → large → small fadeout).
2. Pushes a TARGET at every alive entity in range — anyone overlap
   takes `BLAST_DATA.field_0` damage.
3. Spawns secondary visual effects (smoke, sparks).
4. After fadeout, self-destroys.

## TARGET registration

```c
static void InitBlastTarget(BLAST_DATA *blast_data, Work *work, int side)
{
    GM_SetTarget(&work->target,
                 TARGET_POWER | TARGET_PUSH,
                 side,                       /* PLAYER_SIDE or ENEMY_SIDE */
                 &target_size_from_blast_data);
    GM_Target_8002DCCC(&work->target, 1, -1,
                       blast_data->field_0,  /* hp == damage */
                       0,
                       &target_force_from_blast_data);
}
```

The `side` field controls who the blast hurts. A grenade Snake
threw at a guard is `ENEMY_SIDE` (hurts enemies). An enemy missile
that hits Snake is `PLAYER_SIDE` (hurts player). Friendly fire is
prevented by side mismatch.

## Visual variants — `AN_Blast_*`

Six pre-baked visuals — caller picks based on context:

| Factory | Visual |
| ------- | ------ |
| `AN_Blast_Single` | One large fireball — used for big explosions (RCM hit, C4 detonate) |
| `AN_Blast_Mini` | Smaller fireball — grenade airburst |
| `AN_Blast_Minimini` | Tiny puff — small impact |
| `AN_Blast_Rand` | Random scatter of mini blasts — multi-impact |
| `AN_Blast_high` | High-altitude variant (less ground debris) |
| `AN_Blast_high2` | Same with positional offset (chained explosions) |

Each is a single-line wrapper:

```c
void AN_Blast_Single(SVECTOR *pos) {
    NewAnime_8005E090(pos);   /* via the position-only NewAnime variant */
    /* + sometimes additional smoke/spark spawns */
}
```

The exact NewAnime variant chosen per `AN_Blast_*` differs — see
[../anime/_unreversed.md](../anime/_unreversed.md) for the
NewAnime variant table.

## Sound

`NewBlast2` plays an explosion sound through `GM_AlertSound` —
which:
- Plays the sample.
- Adds a noise event at `pos` so guards within hearing range
  react.

`NewBlast` (without 2) is silent — used for chained / scripted
blasts that the caller wants to play their own sound for.

## Common pitfalls

- **Damage missing**: `BLAST_DATA.field_0` must be > 0. A blast
  with damage 0 still pushes the target volume but does nothing.
- **Wrong side**: a Snake-thrown grenade with `side =
  PLAYER_SIDE` would hurt Snake instead of guards. The weapon
  spawning the blast must pass the right side.
- **No visible effect**: `NewBlast` spawns no visuals — the
  caller must `AN_Blast_*` separately.

## Used by

- `weapon/grenade.c` — grenade detonation calls `NewBlast2 +
  AN_Blast_Mini`.
- `weapon/bomb.c` — C4 detonate.
- `bullet/rmissile.c` — RCM warhead detonate.
- `bullet/jirai.c` — mine trigger.
- `okajima/mg_room.c` — sprinkler-system explosions.
- `enemy/missile launchers` — amissile.c on impact.
