# `source/anime/` — particle / sprite-effect actors

The "anime" folder (yes, the joke is the name) holds **2D
texture-strip particle effects** — smoke, breath, blood, sparks,
muzzle flashes, gun lights, sleep "Z" markers. The signature feature
is *animated UV strips*: small textures that run through a sequence
of frames over a few ticks then disappear.

These are not actors with `CONTROL`/`OBJECT` — they're tiny
billboard sprites attached to a parent matrix, decoded from a
compact bytecode-like ANIMATION script.

Two halves:

| Half | Lines | Role |
| ---- | ----- | ---- |
| [animconv/](animconv.md) | 1467 | The interpreter — `AnimeWork` actor, `ANIMATION` script format, `PRESCRIPT` keyframe table, the per-frame eval loop. |
| [effect/](effect.md) | 2771 | The library of authored effects — each `.c` file is a single effect (smoke, breath, mark, fog, sleep-Z, RCM, smoke trails, SOCOM muzzle flash). Each defines a static `ANIMATION` blob + an `AN_<Name>` factory. |

## How effects fire

A typical call from gameplay code:

```c
/* In sna_act.c — when Snake exhales (cold-stage breath effect): */
AN_Breath(&work->body.objs->objs[6].world);
```

`AN_Breath` is a 1-liner that wraps `NewAnime`:

```c
void AN_Breath(MATRIX *matrix)
{
    NewAnime(matrix, GM_CurrentMap, &anm_breath_form);
}
```

`anm_breath_form` is a static `ANIMATION` struct living at the top
of `breath.c`. It points at a tiny bytecode buffer
(`anm_breath_data[]`) that the `AnimeWork` actor's `Act` interprets
each tick.

So spawning an effect:
1. The caller picks an `AN_<name>` factory.
2. `AN_<name>` calls `NewAnime(world_matrix, map, &anim_template)`.
3. `NewAnime` allocates an `AnimeWork` actor at level 5 or 6, copies
   the template's pointer.
4. The actor ticks its `Act`, executing the script per frame.
5. The actor self-destroys when the script runs out.

## Why it's called "anime"

A leftover dev joke. The codebase comment is:

```c
// "It's like one of my Japanese animes..."
// 「これじゃ まるでアニメじゃないか」
```

Some team member saw 30+ different particle effects and exclaimed
something to the effect of "this is just like an anime" — the
folder name stuck.

## Components

- **[animconv.md](animconv.md)** — the interpreter. `AnimeWork`,
  `ANIMATION` / `PRESCRIPT` formats, the script bytecode opcodes.
- **[effect.md](effect.md)** — the catalogue: every `AN_*` exported
  by the effect library, organised by visual category.
- **[_unreversed.md](_unreversed.md)** — what's still by-address
  (especially the 13 `NewAnime_8005Dxxx` variant entry points whose
  exact differences vs `NewAnime` are unclear).

## Used by

The biggest customers:

- **`source/chara/snake/`** — breath puff, footstep dust, blood
  on damage, ration pickup sparkle.
- **`source/animal/`** — guards' breath, blood markers, gun
  flashes (`AN_Socom`).
- **`source/weapon/`** — every weapon spawns its own effects on
  fire / impact.
- **`source/bullet/`** — bullet trail sprites.
- **`source/takabe/breath.c`**, etc. — environmental effects.
- **Cinematic dolls** — `Demodoll_*` in `source/animal/doll/`
  spawns breath / look-target marker.

## Conceptually adjacent folders

- **`source/thing/emitter.c`** — a higher-level "emitter" actor
  spawned by GCL `chara &EMITTER` that calls anime/effect/* in
  bulk (snow flakes, smoke columns).
- **`source/okajima/`** — a per-author effect library that
  parallels anime/effect/ for more complex / 3D effects (blood
  splashes, blur, plasma, etc.).
- **`source/takabe/`** — yet another per-author folder with its
  own effect set (gsplash, ripples, lit_mdl, …).

The split is mostly historical: each lead programmer had their
own folder and pre-baked a library of effects there. Anime/ is
the "shared" library that everyone calls into.
