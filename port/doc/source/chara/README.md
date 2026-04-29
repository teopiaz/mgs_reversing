# `source/chara/` — player + player-side characters

The actors Snake spawns or rides, plus the per-stage
"specialised" characters that don't fit `enemy/` or `animal/`.

| Sub-folder | Lines | Role |
| ---------- | ----- | ---- |
| [snake/](snake.md) | 9496 | The player character. By far the largest single component in the codebase — `sna_init.c` alone is 8684 lines. Movement, weapons, collision, animation, all of it. |
| [snake_vr/](snake_vr.md) | ~few hundred | The VR-mission Snake variant. Same skeleton, simplified state. |
| [hind2/](hind2.md) | 1126 | The Hind helicopter boss — Liquid's chopper in s11g. Includes its missile launcher (`hd_bul2.c`). |
| [others/](others.md) | 357 | Short utility actors — `intr_cam.c` (intro camera), `belong.c` (item-belonging tracker), `motse.c` (motion/sound bridge). |
| [torture/](torture.md) | ~3500 | The torture-scene actors: bed, Ocelot, Otacon, ninja, revolver. Stage s03c-specific. |

## Why "chara" vs "enemy" / "animal"

- **`chara/`** holds player & player-affiliated characters
  + bosses + scene-specific NPCs.
- **`enemy/`** holds the *generic* guard AI base
  (WATCHER/COMMANDER/ZAKO).
- **`animal/`** holds *stage-specialised* NPCs and the cinematic
  puppet base.

The torture/ subfolder is here because Ocelot et al. are story-
critical characters (with full named state, custom behaviours)
rather than reusable enemies. They live for one stage and are
authored end-to-end.

## Components

- [snake.md](snake.md) — Snake the player. Massive surface; the
  doc covers the structure of `SnaInitWork`, the action-fn-ptr
  state machine, weapon dispatch, collision integration.
- [snake_vr.md](snake_vr.md) — VR-mission variant.
- [hind2.md](hind2.md) — Hind helicopter boss.
- [others.md](others.md) — three small utilities.
- [torture.md](torture.md) — the torture-room cast.
- [_unreversed.md](_unreversed.md) — what's still by-address.
