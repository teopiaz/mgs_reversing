# `anime/animconv/` — the particle script interpreter

`animconv/` (one source file, `anime.c`, 1467 lines) is the engine
that runs every effect in `anime/effect/`. It owns:

- the `AnimeWork` actor type (one instance per active effect),
- the `ANIMATION` / `PRESCRIPT` data formats,
- a 15-opcode interpreter that drives per-vertex animation,
- a per-frame UV-strip cycler,
- pos / scale / rotate evolution between keyframes.

If you're authoring a new effect, you write a new
`static const char foo_data[] = { … }` bytecode + a static
`ANIMATION foo = { … }` template and one line `AN_Foo(matrix) →
NewAnime(matrix, GM_CurrentMap, &foo);`. The interpreter does
the rest.

## Public API

From [`animconv/anime.h`](../../../../source/anime/animconv/anime.h):

```c
typedef struct PRESCRIPT {
    SVECTOR pos;          /* keyframe spawn offset (PSX world units) */
    SVECTOR speed;        /* per-tick velocity */
    short   scr_num;      /* index into the bytecode buffer */
    short   s_anim;       /* sub-animation id (UV strip selector) */
} PRESCRIPT;

typedef struct ANIMATION {
    unsigned short field_0_texture_hash;  /* GV_StrCode of the texture */
    short          field_2;
    short          field_4;
    short          n_anims;     /* number of UV strips */
    short          n_vertices;  /* particle count */
    short          field_A;
    short          field_C;
    short          field_E_xw;  /* x packed with width */
    short          field_10_yh; /* y packed with height */
    short          field_12_rgb;
    PRESCRIPT     *pre_script;  /* keyframe table */
    char          *field_18_ptr;
} ANIMATION;

void *NewAnime(MATRIX *world, int map, ANIMATION *animation);
void *NewAnime2(DG_PRIM *prim, int map, ANIMATION *animation);
```

`NewAnime` is the universal spawn — pass a parent matrix (the bone
your effect should follow), the current map, and a pre-authored
`ANIMATION` template. `NewAnime2` is the variant that takes an
already-allocated `DG_PRIM` (used when the caller wants to pre-bind
the primitive for shared lifecycle).

## AnimeWork — the effect actor

(Struct definition is module-internal; the visible shape from
`anime.c`):

```c
typedef struct AnimeWork {
    GV_ACT        actor;          /* engine actor header */
    DG_PRIM      *prim;           /* the rendered primitive */
    MATRIX       *world;          /* parent (bone) matrix to follow */
    int           map;            /* map id */
    int           n_vertices;     /* particle count, copied from ANIMATION */
    AnimeItem    *items;          /* per-particle state (n_vertices entries) */
    SVECTOR      *vertices;       /* per-particle world positions */
    /* …more state for prescript playback, RGB/UV evolution, fadeout… */
} AnimeWork;
```

Each effect has up to N particles ("vertices"). Per particle we
keep an `AnimeItem` containing:

- `op_code` — pointer into the bytecode buffer (this particle's
  current execution position).
- `counter` — ticks until the next opcode dispatch fires.
- `field_14` — variable-purpose pointer set by some opcodes
  (PRESCRIPT row, RGB animation table, …).
- per-particle position / speed / acceleration deltas.

## Spawn flow — `NewAnime`

[`anime.c:1432`](../../../../source/anime/animconv/anime.c#L1432):

```c
void *NewAnime(MATRIX *world, int map, ANIMATION *animation)
{
    AnimeWork *work = GV_NewActor(GV_ACTOR_LEVEL5, sizeof(AnimeWork));
    if (work) {
        GV_SetNamedActor(&work->actor, Act, Die, "anime.c");
        work->world = world;
        work->map   = map;
        if (GetResources(work, map, animation) < 0) {
            GV_DestroyActor(&work->actor);
            return NULL;
        }
    }
    return work;
}
```

`GetResources` ([`anime.c:1363`](../../../../source/anime/animconv/anime.c#L1363))
allocates the per-particle arrays and the `DG_PRIM`, copies the
texture handle from the animation's `field_0_texture_hash`, sets
each particle's `op_code` to the start of the script, runs the
prescript (which seeds initial positions for every particle).

## Per-tick `Act` — the interpreter

[`anime.c:1191`](../../../../source/anime/animconv/anime.c#L1191):

```c
static void Act(AnimeWork *work)
{
    DG_VisiblePrim(work->prim);

    for (i = 0; i < work->n_vertices; i++) {
        AnimeItem *item = &work->items[i];

        if (item->counter <= 0) {
            /* Counter expired — execute the next opcode(s) until
             * one returns "I'm done, pause and let the renderer see
             * me" (returns non-zero). */
            while (1) {
                op = *item->op_code & 0x7F;       /* low 7 bits = opcode */
                if (op == 0 || op > 15) {
                    /* port: bail with diagnostic */
                    GV_DestroyActor(&work->actor);
                    return;
                }
                if (anime_fn_table[op - 1](work, i) != 0) break;
            }
        }

        anime_act_helper_8005F46C(vertices, item);   /* step pos by speed */
        anime_change_polygon_8005E9E0(work, i);      /* update UV / RGB */
        item->counter--;
    }

    anime_act_helper_8005F094(work);    /* whole-effect post-process */
    GM_CurrentMap = work->map;
    if (work->world) {
        DG_SetPos(work->world);
        DG_PutPrim(&work->prim->world);
    }
}
```

For each particle:

1. If its per-particle `counter` reached 0, dispatch opcodes until
   one says "wait" (returns 0).
2. Always: integrate position by speed, advance UV strip / RGB,
   decrement counter.

The script byte's high bit (`0x80`) is reserved — every dispatch
masks `& 0x7F`. Some opcodes set the high bit on next iteration to
mean "skip". Details vary per opcode.

## The 15 opcodes

[`anime.c:1175-1189`](../../../../source/anime/animconv/anime.c#L1175):

```c
static int (*anime_fn_table_8009F228[15])(AnimeWork *, int) = {
    anime_fn_1_8005E9C8,
    anime_fn_2_8005EBE8,
    anime_fn_3_8005EC8C,
    anime_fn_4_8005ED14,
    anime_fn_5_8005EE2C,
    anime_fn_6_8005EF04,
    anime_fn_7_8005EFF8,
    anime_fn_8_8005F0F0,
    anime_fn_9_8005F180,
    anime_fn_10_8005F288,
    anime_fn_11_8005F2F4,
    anime_fn_12_8005F37C,
    anime_fn_13_8005F408,
    anime_fn_14_8005F438,
    anime_fn_15_*    /* (one extra slot — see _unreversed.md) */
};
```

The functions are named by address — their semantics are inferred
from how the effect bytecodes use them. The decoded guesses:

| Opcode | Likely role | Confidence |
| ------ | ----------- | ---------- |
| 1 | end-of-particle (mark dead, skip next ticks) | high |
| 2 | set pos from PRESCRIPT entry | high |
| 3 | set speed from operand bytes | medium |
| 4 | set counter (tick delay before next opcode) | high |
| 5 | UV-strip change (s_anim) | medium |
| 6 | RGB ramp setup | medium |
| 7 | acceleration / gravity | medium |
| 8 | pos jitter / random offset | low |
| 9 | scale ramp | low |
| 10 | swap to next sub-animation | low |
| 11 | jump in script | medium |
| 12 | conditional / counter-based branch | low |
| 13 | spawn-child-particle | low |
| 14 | (rare) | low |
| 15 | (rarely used; possibly a debug stub) | low |

Recoverable by reading each `anime_fn_*` body — they're 20-50
lines each. See [_unreversed.md](_unreversed.md) for the rename
opportunity.

## ANIMATION bytecode layout

A typical effect's bytecode is a small per-particle script. From
`breath.c`:

```c
static const char anm_breath_data[] = {
    0x05, 0x00, 0x00, 0x00,    /* opcode 5 with 3-byte operand */
    0x04, 0x14, 0x00, 0x00,    /* opcode 4 (counter=20 ticks) */
    0x02, 0x00, 0x00, 0x00,    /* opcode 2 (load PRESCRIPT[0]) */
    0x07, 0x00, 0xFF, 0x00,    /* opcode 7 (apply gravity Y=-256) */
    0x04, 0x3C, 0x00, 0x00,    /* opcode 4 (counter=60 ticks) */
    0x06, 0x80, 0x80, 0x80,    /* opcode 6 (RGB fadeout target) */
    0x01, 0x00, 0x00, 0x00,    /* opcode 1 (end-of-particle) */
};
```

Each row is the opcode byte plus three operand bytes — the
interpreter reads in 4-byte units. `op_code` advances by 4 when the
opcode returns "done with this step".

## PRESCRIPT — the keyframe table

`pre_script` is an array of `PRESCRIPT` entries — initial states
for each particle:

```c
typedef struct PRESCRIPT {
    SVECTOR pos;       /* offset from the parent matrix */
    SVECTOR speed;     /* initial velocity */
    short   scr_num;   /* which script-byte to start at (offset / 4) */
    short   s_anim;    /* which UV strip to play */
} PRESCRIPT;
```

A breath effect with 1 particle has 1 PRESCRIPT row. A smoke
column with 16 particles has 16 PRESCRIPT rows so each particle
can launch from a slightly different offset with slightly
different velocity, producing a natural-looking spread.

## Rendering — DG_PRIM

The prim binding lives in `work->prim` — a `DG_PRIM` allocated by
`GetResources` via `GM_AllocPrim`. The interpreter writes UV +
RGB + pos into the prim's per-vertex arrays each tick;
`DG_PutPrim` then registers it for OT submission.

The prim is parented to `work->world` via `DG_SetPos(work->world);
DG_PutPrim(&work->prim->world);` — so when the parent matrix
moves, the effect follows. This is how head-attached breath puffs
ride with the head bone naturally.

## NewAnime variants — `NewAnime_8005Dxxx`

The interpreter exports 13 alternate spawn entry points:

```c
void *NewAnime_8005D604(MATRIX *pMtx);
void  NewAnime_8005D6BC(MATRIX *arg0, int arg1);
void  NewAnime_8005D988(MATRIX *m1, MATRIX *m2, int mode);
void  NewAnime_8005DDE0(MATRIX *pMtx);
void  NewAnime_8005DE70(MATRIX *rotation);
void  NewAnime_8005DF50(SVECTOR *v1, SVECTOR *v2);
void  NewAnime_8005E090(SVECTOR *pPos);
void  NewAnime_8005E1A0(MATRIX *arg0);
void  NewAnime_8005E258(MATRIX *pMatrix);
void  NewAnime_8005E334(MATRIX *rotation);
void  NewAnime_8005E508(SVECTOR *pos);
void  NewAnime_8005E574(MATRIX *pMtx);
void  NewAnime_8005E6A4(SVECTOR *pos);
void  NewAnime_8005E774(SVECTOR *pos);
```

Each is an authored shortcut for a *specific* effect kind:
e.g. `NewAnime_8005DF50(SVECTOR *v1, SVECTOR *v2)` takes a
two-point line (start + end) and is used by trail effects;
`NewAnime_8005E090(SVECTOR *pos)` takes a single world point and
is used by impact-style effects.

The first arg type tells you most:

| Signature | Likely use |
| --------- | ---------- |
| `(MATRIX *)` | bone-attached effect (breath, gunlight) |
| `(SVECTOR *)` | world-position effect (impact, pickup) |
| `(SVECTOR *, SVECTOR *)` | line / trail (bullet trail) |
| `(MATRIX *, MATRIX *, int)` | two-bone span (some chain effects) |

Each variant references a specific built-in `ANIMATION` template
internally — they're convenience factories. Untraced; see
[_unreversed.md](_unreversed.md).

## Frame budget

Each effect runs at GV_ACTOR_LEVEL5 (one level below most
gameplay actors). Cost is roughly:

- `n_vertices × ~15 instructions` per opcode dispatch (rare).
- `n_vertices × ~5 instructions` per tick for the position/UV
  step.
- 1 prim submission per effect.

For a frame with 50 simultaneous effects (typical combat scene),
that's ~50 prims and a few thousand instructions — well within
PSX budget.

## Common pitfalls

### Effect doesn't appear

- **Texture not in cache**: `field_0_texture_hash` must resolve in
  `DG_GetTexture`. If the stage didn't ship the texture in its
  PCX data, the prim is allocated but renders solid white.
- **`world` matrix is freed**: if the parent bone is destroyed
  but the effect is still alive, `work->world` dereferences freed
  memory. Effects should outlive their parent or the parent
  should kill the effect first via `GV_DestroyActor`.
- **`map` mismatch**: `GM_CurrentMap = work->map` is set every
  tick. If the effect's map differs from the active map, it
  renders but may be culled by the map's clip check.

### Effect lingers forever

Each particle's bytecode must terminate with opcode 1
("end-of-particle"). If the script omits it, the particle never
dies and `work->n_vertices` stays > 0 forever. The actor doesn't
self-destroy until *all* particles complete.

### Opcode 0 crash

The port (anime.c:1216) has an explicit `op == 0 || op > 15`
guard that kills the actor cleanly + logs. On disc this would
underflow the function table. If you see
`[anime] bad op_code: …` in stderr, the bytecode for that effect
has malformed bytes — check the static blob's terminator.

### Effect plays once but never again

`AnimeWork` is single-use. Each `AN_<Name>` call spawns a fresh
actor. If you want a continuous stream, the *caller* needs to
re-spawn — see `pato_lmp.c` for an example of "lamp re-spawns
beam every N ticks".

## See also

- [effect.md](effect.md) — every authored effect (breath / smoke
  / fog / mark / sleep / RCM / SOCOM / smoke_ln).
- [_unreversed.md](_unreversed.md) — the 15 opcodes and 13
  NewAnime_* variants.
- [`source/thing/emitter.c`](../../../../source/thing/emitter.c) —
  the GCL `chara &EMITTER` actor that batch-spawns these
  effects.
- [`source/libdg/`](../../../../source/libdg/) — `DG_PRIM`,
  `DG_PutPrim`, `DG_VisiblePrim` semantics.
