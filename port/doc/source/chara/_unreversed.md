# `chara/` — what's not yet reverse-engineered

## snake/

### `SnaInitWork` — many `field_XXX`

The struct is largely named, but ~20 trailing `field_XXX`
placeholders remain. Notable:

```c
int  field_89C_pTarget    /* TARGET ptr — confirmed */
Target_Data field_8F4     /* "Target_Data" struct of unknown purpose */
Target_Data field_8FC
int  field_950
int  field_A22_snake_current_health  /* confirmed */
/* …many more */
```

`Target_Data` itself has:

```c
typedef struct Target_Data {
    SVECTOR      field_0;
    SVECTOR      field_8_size;
    int          field_10;
    int          field_14;
    int          field_18;
    unsigned int field_1C;
} Target_Data;
```

Roles unclear — possibly aim-target prediction state.

### `SnaFlag1` / `SnaFlag2` — most bits unknown

```c
typedef enum {
    SNA_FLAG1_UNK1 = 0x1, // related to knockdown — confirmed
    SNA_FLAG1_UNK2 = 0x2,
    SNA_FLAG1_UNK3 = 0x4,
    /* …32 bits, only 1 confirmed */
} SnaFlag1;
```

`flags1` and `flags2` together cover ~60 bits of Snake state.
Only a handful are named. Tracing each requires watching
gameplay with a debugger.

### Action callbacks named by address

`sna_init.c` has dozens of `sna_action_<addr>` and
`sub_<addr>` action callbacks. The structural ones (Stand, Walk,
Aim, Damage) are probably named in the disc's debug-build symbol
table; in the released stripped build they're addresses.

The forward-decls + extern uses provide enough hints to rename a
batch (`sna_act_8005AD10` is "the Act dispatcher" for instance).

### `ACTSTILL.tap_wall_r` / `tap_wall_l`

The "tap-wall" animation ids. They map to specific anim segments
in the OAR, but which-segment-is-which animation needs in-game
verification.

### Many `sna_*_<addr>` helpers

Functions like `sna_8004E260`, `sub_8004E458` — tiny utilities
called from action callbacks. Non-trivial to name without
context but each is short.

## hind2/

### State enum

The Hind state machine values aren't named:

```
HIND_PATROL    (probably 0 or 1)
HIND_AIM       (?)
HIND_FIRE      (?)
HIND_RETREAT   (?)
HIND_DYING     (?)
```

Reading `do_*` fn bodies should reveal the constants.

### Rotor speed constants

`ROTOR_SPEED` / `ROTOR_TAIL_SPEED` are inline literals — could
be hoisted.

## torture/

### unknown5.c / unknown7.c

Two files literally named "unknown". Whatever they are, no other
code identifies the role. These are where some torture-room
scripted-prop actors live (perhaps the camera trigger, perhaps a
sound-effect emitter); naming them needs play-through.

### Per-character action sets

Every torture character has its own action enum (similar to
`Meryl72`'s 58 ACTION_*). None of them are named here. Renaming
is character-by-character.

### `boxall.c`

The "all boxes" container is a small registry but its API isn't
documented anywhere.

## others/

### `intr_cam.c` is fully reversed

This one's complete — small, tight, no opaque parts.

### `belong.c` — table layout

The "belong" entries' exact format isn't documented. It may be:

- Per-character item list (each character has 0..N items)
- Per-item character (each item has one carrier)

The function signatures should reveal the model.

### `motse.c` — name origin

"MOT-SE" probably stands for Motion-SE (sound-effect). The shared
helper functions are named like
`sna_act_helper2_helper2_80033054` — the nested "helper2_helper2"
suggests several layers of refactoring. Renaming to
`Motse_PlayBoneSound` or similar would be clearer.

## See also

- [README.md](README.md) — folder overview.
- Per-component docs.
