# `anime/` — what's not yet reverse-engineered

The folder is fully decompiled (zero asm) but several layers still
need designer-level identification.

## animconv/

### The 15 opcodes

`anime_fn_table_8009F228[15]` indexes into:

```
anime_fn_1_8005E9C8    — opcode 1: end-of-particle
anime_fn_2_8005EBE8    — opcode 2: load PRESCRIPT entry
anime_fn_3_8005EC8C    — opcode 3: set speed
anime_fn_4_8005ED14    — opcode 4: set counter
anime_fn_5_8005EE2C    — opcode 5: change UV strip
anime_fn_6_8005EF04    — opcode 6: RGB ramp
anime_fn_7_8005EFF8    — opcode 7: gravity / acceleration
anime_fn_8_8005F0F0    — opcode 8: ?
anime_fn_9_8005F180    — opcode 9: ?
anime_fn_10_8005F288   — opcode 10: ?
anime_fn_11_8005F2F4   — opcode 11: ?
anime_fn_12_8005F37C   — opcode 12: ?
anime_fn_13_8005F408   — opcode 13: ?
anime_fn_14_8005F438   — opcode 14: ?
```

Names are inferred from how the effect bytecodes use them — see
[animconv.md](animconv.md). Confidence ranges from "high" (1, 2,
4) to "low" (8-14). Reading each function body (20-50 lines) is
the next step; the difficulty is naming what the *high-level*
behaviour is.

### NewAnime variants

13 wrapper factories named only by address:

```
NewAnime_8005D604(MATRIX *)
NewAnime_8005D6BC(MATRIX *, int)
NewAnime_8005D988(MATRIX *, MATRIX *, int)
NewAnime_8005DDE0(MATRIX *)
NewAnime_8005DE70(MATRIX *)
NewAnime_8005DF50(SVECTOR *, SVECTOR *)
NewAnime_8005E090(SVECTOR *)
NewAnime_8005E1A0(MATRIX *)
NewAnime_8005E258(MATRIX *)
NewAnime_8005E334(MATRIX *)
NewAnime_8005E508(SVECTOR *)
NewAnime_8005E574(MATRIX *)
NewAnime_8005E6A4(SVECTOR *)
NewAnime_8005E774(SVECTOR *)
```

Each is a shortcut for a specific effect category. Their bodies
internally pick a built-in `ANIMATION` template — identifying which
template gives you the rename ("this is the one for…").

### Helper functions named by address

```
anime_act_helper_8005F46C       — per-particle pos += speed
anime_change_polygon_8005E9E0   — UV/RGB updater
anime_act_helper_8005F094       — whole-effect post-processing
anime_loader_helper_8005F644    — script loader, pre-runs prescript
```

These are the engine support routines. Names are descriptive
("act helper" / "polygon change") but the *exact transformations*
they perform aren't documented here.

### `AnimeWork` / `AnimeItem` field names

The struct definitions are module-internal in `anime.c`. From
usage:

```
AnimeWork:
    actor               (named)
    prim                (named)
    world               (named)
    map                 (named)
    n_vertices          (named)
    items[]             (named — n_vertices entries)
    vertices[]          (named — per-particle world pos)
    /* …several unnamed timer / RGB / scale fields… */

AnimeItem:
    counter             (named)
    op_code             (named)
    field_14            (mixed-purpose — PRESCRIPT * sometimes,
                          RGB table * sometimes)
    /* …unnamed pos / speed / acc fields */
```

The unnamed fields hold per-particle pos / speed / acceleration
deltas + RGB / UV state. Identifying them is a straightforward
read of the opcode-handler code.

### `field_18_ptr` in ANIMATION

The 13th field of `ANIMATION` is a `char *field_18_ptr`. Some
effects set it, others don't. Possibly an alternate-script
pointer for certain opcode paths (opcode 11 jump?). Untraced.

## effect/

### Names by address

Most exported `AN_*` factories still carry their address:

```
breath.c:    AN_Unknown_800C3B7C
mark.c:      AN_Unknown_800CA594  (and several internal helpers)
smoke2.c:    AN_Unknown_800CCA40, AN_Unknown_800CCB84
smoke3.c:    AN_Unknown_800DC4B4, _800DC5B4, _800DC6AC,
             _800DC860, _800DC94C, _800DC9C8
smoke_ln.c:  AN_Smoke_800CE08C, _800CE0F8, _800CE164, _800CE240,
             _800CE2C4, _800CE55C, _800CE5C8, _800CE6A4
socom.c:     AN_Unknown_800D6898, _800D6BCC, _800D6EB0,
             _800D6F6C, _800D7028, _800D70E4
```

Renaming is a "play the game and watch which fires when" job. For
each address, find the call sites in `source/` and check what
weapon / chara is invoking it.

### `AN_Piyopiyo`

The name is recovered from a Japanese onomatopoeia (ピヨピヨ —
"piyo piyo", chirping). The team's joke for "small bouncy
particles". Used in s00a opening for the dramatic re-entry. The
function works fine; the name is preserved as-is.

### Effect bytecode authoring

Without identifying every opcode, authoring a new effect by
hand-writing the bytecode is tricky. The current workflow is:

1. Find an existing `effect/<name>.c` that does something close.
2. Copy + tweak the byte values.
3. Iterate by running the game.

A proper authoring tool — readable text → bytecode compiler — would
need the opcode table fully named first. Not yet built.

### Stage-specific helpers

`smoke.c` exports `s00a_command_800CA758` — a stage-private
helper. There are likely others scattered through the effect/
files that aren't visible from a top-level scan. Worth a survey
to identify which effects "leak" into stage-specific call sites.

## See also

- [index.md](index.md), [animconv.md](animconv.md),
  [effect.md](effect.md) — the in-context view of each item
  above.
- [`build/functions.txt`](../../../../build/functions.txt) — the
  authoritative function-address table for renaming.
