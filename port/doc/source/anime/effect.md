# `anime/effect/` — the effect catalogue

11 source files, each holding one or more `AN_*` factories that
spawn a specific particle effect via `NewAnime()`. Files group
*conceptually-related* effects authored as a unit.

| File | Lines | Effects exported | Used for |
| ---- | ----- | ---------------- | -------- |
| [`breath.c`](../../../../source/anime/effect/breath.c) | 135 | `AN_Breath`, `AN_Breath_2`, `AN_Unknown_800C3B7C` | white breath puffs (cold stages — Snake / guards exhale) |
| [`fog.c`](../../../../source/anime/effect/fog.c) | 47 | `AN_Fog` | drifting low-altitude fog wisps |
| [`mark.c`](../../../../source/anime/effect/mark.c) | 386 | `AN_Unknown_800CA594` and several others | impact / footstep / pickup marker sprites |
| [`rcm.c`](../../../../source/anime/effect/rcm.c) | 265 | RCM (remote control missile) trails | Snake's RCM weapon path |
| [`rcm2.c`](../../../../source/anime/effect/rcm2.c) | 135 | RCM tail flame variant | RCM steered turn marker |
| [`sleep.c`](../../../../source/anime/effect/sleep.c) | 68 | `AN_Sleep` | "Z" sleep markers above stunned guards |
| [`smoke.c`](../../../../source/anime/effect/smoke.c) | 209 | `AN_Piyopiyo`, `s00a_command_*` | basic smoke puff (dramatic re-entry s00a) |
| [`smoke2.c`](../../../../source/anime/effect/smoke2.c) | 123 | `AN_Unknown_800CCA40`, `AN_Unknown_800CCB84` | secondary smoke variant |
| [`smoke3.c`](../../../../source/anime/effect/smoke3.c) | 302 | 6 `AN_Unknown_800DCxxx` variants | colored smoke + parametric position/speed |
| [`smoke_ln.c`](../../../../source/anime/effect/smoke_ln.c) | 618 | 8 `AN_Smoke_800CExxx` variants | smoke trails / linear smoke (bullets, missiles) |
| [`socom.c`](../../../../source/anime/effect/socom.c) | 483 | 6 `AN_Unknown_800Dxxxx` | SOCOM pistol muzzle flash + shell ejection + smoke |

Total: 2771 lines, ~30+ exported `AN_*` factories. Most names are
still by-address (`AN_Unknown_800CXXXX`) — the role is clear
(they're effects), the *name* needs a designer pass to identify
which-effect-is-which.

## File shape — every effect is built the same way

A typical `effect/<name>.c`:

```c
#include "anime/animconv/anime.h"

/* 1. The bytecode buffer (per-particle script). */
static const char anm_breath_data[] = {
    0x05, 0x00, 0x00, 0x00,
    0x04, 0x14, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00,
    0x07, 0x00, 0xFF, 0x00,
    0x04, 0x3C, 0x00, 0x00,
    0x06, 0x80, 0x80, 0x80,
    0x01, 0x00, 0x00, 0x00,
};

/* 2. Maybe a PRESCRIPT table (per-particle initial state). */
static PRESCRIPT anm_breath_prescript[1] = {
    { {0,0,0,0}, {0,-32,0,0}, 0, 0 },   /* one particle at parent
                                            origin, drifting up */
};

/* 3. The ANIMATION template tying them together. */
static ANIMATION anm_breath_form = {
    GV_StrCode("breath"),   /* texture hash */
    0, 0,                   /* field_2, field_4 */
    1,                      /* n_anims = 1 UV strip */
    1,                      /* n_vertices = 1 particle */
    /* …packed xw / yh / rgb / pre_script ptr… */
    anm_breath_prescript,
    (char *)anm_breath_data,
};

/* 4. The exported factory. */
void AN_Breath(MATRIX *matrix)
{
    NewAnime(matrix, GM_CurrentMap, &anm_breath_form);
}
```

Effects with multiple modes (e.g. light vs heavy smoke) keep
multiple `ANIMATION` templates in the same file and export
multiple `AN_*` factories pointing at different ones.

## breath.c — character breath

Three exports:

```c
void AN_Breath(MATRIX *matrix);              /* primary — used by snake/guards */
void AN_Breath_2(MATRIX *matrix);            /* variant — slightly larger */
void AN_Unknown_800C3B7C(MATRIX *matrix);    /* unidentified — possibly cigarette smoke */
```

The matrix arg is a bone matrix — typically the head bone (bone 6
in most humanoid KMDs). The effect rides the bone, rendering a
small white puff that drifts up and dissolves over ~2 seconds.

Used in cold stages (s11g hind hangar, s17a snowfield, s19b jeep).
Gameplay code calls `AN_Breath` every ~30 ticks per character.

## fog.c — atmospheric fog

One export:

```c
void AN_Fog(SVECTOR *pos);
```

Spawns a drifting low-poly fog wisp at the world position. Several
of these scattered across a stage make a fog bank. Used in s17a
and s19b.

## mark.c — impact / footstep markers

One named export plus several internal helpers:

```c
void AN_Unknown_800CA594(SVECTOR *pos);
```

Spawns a small dust puff or footprint mark at a world pos. The
exact mark differs per `AN_Unknown_*` variant — there are 5+ in
this file but only one with an exported decl.

Used by:
- Bullet impact code (when a bullet hits a wall, leaves a mark).
- Footstep system (chara/snake/afterse.c emits these on each
  footfall).
- Item pickup (sparkle effect).

## rcm.c / rcm2.c — RCM (remote-controlled missile)

The RCM weapon is one of Snake's gadgets — a tiny remote-controlled
missile he steers manually with the d-pad. It needs:

- A continuous flame trail behind the missile.
- A puff at the launch tube.
- Smoke when it hits / explodes.

`rcm.c` (265 lines) covers the trail + tail flame. `rcm2.c` covers
the variant trail used during steering (when the player is actively
turning).

## sleep.c — "Z" sleep marker

One export:

```c
void AN_Sleep(SVECTOR *pos);
```

Spawns a small floating "Z" character above a stunned guard. Used
when a guard is knocked unconscious (stun grenade, choke hold).

## smoke.c — generic smoke

`AN_Piyopiyo` (named after a Japanese onomatopoeia for chirping —
the particle pattern apparently looked like little birds) is the
main entry. Plus `s00a_command_800CA758` — a stage-specific call
used by the s00a opening cinematic.

The `anm_piyopiyo_*` data + `anm_piyopiyo_form` template define a
multi-particle (8-16 particles) smoke puff that expands and fades
over ~3 seconds.

## smoke2.c — secondary smoke variant

Two `AN_Unknown_*` exports. Probably "thicker" or "smaller" smoke
than `smoke.c`. The bytecode differs (slightly different RGB
fadeout curve).

## smoke3.c — parametric smoke

The richest smoke library — six exports each taking different
parameter combinations:

```c
void AN_Unknown_800DC4B4(SVECTOR *pos, int ang);
void AN_Unknown_800DC5B4(SVECTOR *pos, SVECTOR *speed, int script);
void AN_Unknown_800DC6AC(SVECTOR *pos, SVECTOR *speed, char r, char g, char b);
void AN_Unknown_800DC860(SVECTOR *pos, SVECTOR *speed);
void AN_Unknown_800DC94C(SVECTOR *pos);
void AN_Unknown_800DC9C8(SVECTOR *pos);
```

The variant taking `(pos, speed, r, g, b)` is the most flexible —
use it for arbitrary colored smoke (e.g. red smoke for fire, blue
for chaff). The simpler ones `(pos)` use default white smoke.

Used heavily by:
- `bullet/blast.c` — explosion smoke.
- `weapon/grenade.c` — grenade smoke trail.
- `okajima/uji.c` — fire-effect smoke.

## smoke_ln.c — linear smoke trails

The biggest effect file (618 lines). 8 exports for trail-style
smoke — particles that span between two world points:

```c
void AN_Smoke_800CE08C(SVECTOR *pos);
void AN_Smoke_800CE0F8(SVECTOR *pos);
void AN_Smoke_800CE164(SVECTOR *pos, SVECTOR *speed, int index, int script);
void AN_Smoke_800CE240(SVECTOR *pos);
void AN_Smoke_800CE2C4(SVECTOR *pos, SVECTOR *speed, int index, int script,
                       char r, char g, char b);
void AN_Smoke_800CE55C(SVECTOR *pos);
void AN_Smoke_800CE5C8(SVECTOR *pos, SVECTOR *speed, int index, int script);
void AN_Smoke_800CE6A4(SVECTOR *pos);
```

The `(pos, speed, index, script, r, g, b)` variant is the parametric
trail — used by bullets and rockets. The simpler `(pos)` variants
spawn pre-baked trails for stage-specific moments.

`smoke_ln` distinguishes itself from `smoke3` by using *line*
(elongated) particles rather than puffs — better for fast-moving
projectiles where round puffs would tear.

## socom.c — SOCOM pistol effects

Snake's main sidearm has:

- Muzzle flash (`AN_Unknown_800D6898`).
- Shell ejection sprite (`AN_Unknown_800D6BCC`).
- Slide-back smoke (`AN_Unknown_800D6EB0`).
- Bullet trail (handled by smoke_ln).
- Impact spark (`AN_Unknown_800D6F6C` / `AN_Unknown_800D7028` /
  `AN_Unknown_800D70E4`).

Each fires from `weapon/socom.c`'s firing-frame callback. The
effects layer onto each other to make one shot look "right".

## Texture sharing

Every effect references its texture by hash via
`GV_StrCode("breath")` etc. The texture must be loaded into the
PSX VRAM (via PCX cache → `DG_GetTexture` lookup) before the
effect spawns. Most effect textures live in:

- `darpa.pcx` / `init.pcx` — generic effect textures (always
  resident).
- Per-stage `.pcx` — stage-specific effects.
- `m1e1.pcx`, `hind.pcx` — vehicle-specific effects.

If the texture isn't loaded, the effect renders solid white.

## How to add a new effect

1. Create `effect/myeffect.c`.
2. Author the bytecode (the 15 opcodes from
   [animconv.md](animconv.md)).
3. Define an `ANIMATION` template referencing it.
4. Export `void AN_MyEffect(MATRIX *m) { NewAnime(m, GM_CurrentMap,
   &anm_myeffect); }`.
5. Add the file to `build/Makefile`.
6. Reference the texture's `.pcx` from your stage (or use one of
   the always-resident ones).
7. Call `AN_MyEffect(...)` from gameplay code.

The hardest part is the bytecode authoring — see
[_unreversed.md](_unreversed.md) for the opcode table that needs
to be filled in before authoring is straightforward.

## See also

- [animconv.md](animconv.md) — the interpreter that runs every
  one of these effects.
- [_unreversed.md](_unreversed.md) — opcodes + variant entry
  points still by-address.
- [`source/okajima/`](../../../../source/okajima/) — companion
  effect library with its own (3D) effects.
- [`source/takabe/`](../../../../source/takabe/) — yet another.
- [`source/thing/emitter.c`](../../../../source/thing/emitter.c) —
  GCL-spawned effect emitter.
