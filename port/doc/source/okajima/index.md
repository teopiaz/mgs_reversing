# `source/okajima/` — Okajima's effect / object library

Per-author folder. Authored by **Okajima** — one of the lead
programmers. ~46 component pairs (.c + .h), covering ~80% of the
3D *non-character* visual effects + a handful of stage-private
gameplay objects.

## Style of code

Everything in `okajima/` follows the same shape:

- One actor per file (`<name>.c` + `<name>.h`).
- Each spawns a small state struct, runs an `Act` callback,
  destroys on done.
- Many are *visual only* — no TARGET, no collision.
- A few are gameplay-relevant (claymore, key items, evntmous).

## Components (high-level)

### Blood + impact effects
| File | Effect |
| ---- | ------ |
| `blood.c` | Generic blood spray |
| `blood_bl.c` | Black-blood variant (Liquid?) |
| `blood_cl.c` | Cleaning / fading variant |
| `d_blood.c` | Drip blood |
| `d_bloodr.c` | Variant |
| `d_bloods.c` | Splash variant |
| `chafgrnd.c` | Chaff-grenade aftermath on the ground |
| `splash.c` / `splash2.c` / `splash3.c` | Water splashes |

### Bubbles (underwater stages)
| File | Effect |
| ---- | ------ |
| `bub_d_sn.c` | Snake's underwater bubbles |
| `bubble_p.c` | Player bubbles |
| `bubble_s.c` / `bubble_t.c` | Stationary / trail bubbles |

### Smoke / particles
| File | Effect |
| ---- | ------ |
| `blur.c` / `blurpure.c` | Motion blur quad |
| `bullet.c` | Bullet impact spark |
| `crane.c` | Crane / construction visual |
| `flr_spa.c` / `wall_spa.c` | Floor / wall sparks |
| `fall_spl.c` | Falling splash |
| `smktrgt.c` | Smoke target marker |
| `smke_ln.c` | Linear smoke trail (alternate to `anime/effect/smoke_ln.c`) |
| `spark.c` | Sparks (impacts, electrical) |
| `stngrnd.c` | Stun-grenade ground ring |

### Effects with gameplay
| File | Effect |
| ---- | ------ |
| `claymore.c` | The claymore mine on the floor (gameplay — directional damage). |
| `key_item.c` | Key-item placeholder (the visual / pickup logic for cards / IDs / disc). |
| `evntmous.c` | Event mouse — possibly a contextual cursor / pointer overlay. |
| `ductmous.c` | Duct-related mouse / animation. |
| `mouse.c` | Generic mouse (pointer? rodent?). |
| `mg_room.c` | Machine-gun room — the s11g sprinkler / chain-gun set piece. |

### Specific objects
| File | Effect |
| ---- | ------ |
| `crane.c` | Crane object (some stage). |
| `hiyoko.c` | Chick (literally a baby chick — Easter-egg sprite). |
| `melt_die.c` | Melt-death effect. |
| `p_lamp.c` | Pole lamp. |
| `pato_lmp.c` | "Pato lamp" — patrol-style sweeping searchlight. |
| `red_alrt.c` | Red-alert visual indicator. |
| `sub_room.c` | Sub-room actor. |
| `uji.c` | Uji (蛆 = maggot? possibly a stage-private effect). |

### Plasma / electrical
| File | Effect |
| ---- | ------ |
| `plasma.c` / `plasma_h.c` | Plasma effect (electrical hazard). |

### Death / damage
| File | Effect |
| ---- | ------ |
| `death_sp.c` | Death particle effect |
| `melt_die.c` | Melt-death |

### Other
| File | Effect |
| ---- | ------ |
| `blink_tx.c` | Eyelid-blink texture overlay (used by DOLL — see `animal/doll/doll.c`). |
| `item_dot.c` | Item-on-radar dot. |
| `pato_lmp.c` | Patrol lamp (sweep). |
| `scn_mark.c` | Scene mark. |
| `stgfd_io.c` | Stage-fade I/O. |

## Coding conventions

- Almost every file has a globally-visible `New*` factory.
- Most use `GV_ACTOR_LEVEL5` or `LEVEL6`.
- Effect-only actors don't have CONTROL — they have a small
  `Work` struct with `pos / time / scale` plus a `DG_PRIM` or
  `DG_OBJS`.

## Why a separate folder

Okajima's code style is distinct from `anime/effect/` — these are
**3D** effects (plasma rays, falling drops, full mesh objects)
where anime/effect/ are 2D billboards. Both libraries coexist
because they cover different visual styles.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (the various
  "mouse" actors, hiyoko / uji easter eggs, mg_room choreography).
- [`source/anime/effect/`](../anime/effect.md) — sister 2D effect
  library.
- [`source/takabe/`](../takabe/index.md) — yet another per-author
  effect library.
- [`source/thing/`](../thing/index.md) — non-character actors.
