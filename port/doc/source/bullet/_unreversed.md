# `bullet/` — what's not yet reverse-engineered

Fully decompiled. Open items per file:

## blast.c

### `BLAST_DATA` field names

```c
typedef struct BLAST_DATA {
    int field_0;     /* hp / damage — confirmed */
    int field_4;     /* ??? */
    int field_8_z;   /* size dimension Z */
    int field_C;     /* possibly size XY? */
    int weapon;      /* originating WP_* — confirmed */
} BLAST_DATA;
```

`field_4` and `field_C`'s exact roles aren't proven. Guess for
`field_C` is "blast XY-radius", with `field_8_z` being height —
which would make sense (a fireball has a different vertical and
horizontal extent), but the code doesn't make this clear in
isolation. Renaming requires reading every blast spawn site.

### `AN_Blast_*` internals

Each `AN_Blast_*` calls one of the by-address `NewAnime_8005Dxxx`
variants. Picking them apart needs the NewAnime variant table
named first — see [../anime/_unreversed.md](../anime/_unreversed.md).

## bakudan.c

### `dword_8009F434`

Top-of-file global declared `unused variable`. May be vestigial
debug state — kept for matching-build byte-identity.

### `GetNextC4Data`

Walks an internal ring buffer of pre-allocated C4 data structs.
The ring's size + entry layout aren't fully decoded. The
function works correctly (matches disc behaviour); naming the
buffer entries is the open task.

## jirai.c

### Mine-type enum

The `type` field selects between claymore / proxy / sniping pad
variants but the constants aren't named. Likely:

```c
enum mine_type {
    MINE_CLAYMORE = 0,
    MINE_PROXY    = 1,
    MINE_SNIPER   = 2,
    /* …possibly more */
};
```

Confirming requires checking call sites in `weapon/mine.c` and
the s07c / s11e GCL spawns.

### Stage-persistent serialisation

Mines survive stage transitions via a global save buffer. The
buffer's structure isn't documented here; it's somewhere in the
save / GCL-var system. Worth tracing alongside `memcard/` work.

## tenage.c

### Bounce constants

The dampening factor (~0.3) and ground-friction values are
literals in the bounce code. They could be hoisted to named
constants for clarity. Low priority.

## rmissile.c

### Camera channel selection

Which `DG_Chanls[]` slot is used for the RCM POV is determined
by some logic that's not obvious from a single read — possibly
"first unused channel" or "channel reserved for vehicle POVs".
Tracing this clarifies how the editor's DMO inspector might
render an RCM cinematic correctly.

### HUD overlay

The crosshair + fuse timer drawn during flight is rendered via
direct `DG_PRIM` allocation in this file rather than the menu/
system. The drawing code is ~150 lines and uses unnamed
helpers. Untraced.

## amissile.c

### Multi-target acquisition

When the launcher fires, it sometimes locks onto a different
target after spawn (e.g. if Snake hides behind a wall, the
missile redirects to a decoy). The redirection logic isn't
documented here. May involve `homing.c` calls.

### `MAX_TURN_PER_TICK` value

The literal turn-rate constant is in the source but not named.
Worth hoisting if the missile balance is ever tweaked.

## Cross-cutting

### TARGET sizes / forces

Every projectile registers a TARGET with size and force vectors.
The values come from external constants like
`ZAKO_TARGET_SIZE_800C38CC` (or per-bullet equivalents). Some are
named, some aren't — particularly the per-bullet damage curves
(`ENEMY_TARGET_FORCE_*` etc.).

### "side" propagation

The PLAYER_SIDE / ENEMY_SIDE flag flows through every projectile.
A few sites pass `0` instead of an explicit side, which works
out (default to ENEMY?) but is fragile. Worth a sweep.

## See also

- [index.md](index.md) — folder overview.
- Per-file docs (blast, bakudan, jirai, tenage, rmissile,
  amissile).
