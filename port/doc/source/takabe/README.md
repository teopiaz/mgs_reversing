# `source/takabe/` — Takabe's effect / scene library

Per-author folder. Authored by **Takabe**. Holds many of the
*scene-furnishing* visual + scripted-actor pieces — letterbox
bars, screen fades, doors, water, environmental effects, the
CINEMA actor itself.

## Files (~70+, totaling ~10K lines)

### Cinematic infrastructure
| File | Role |
| ---- | ---- |
| [`cinema.c`](../../../../source/takabe/cinema.c) | The `CINEMA` chara — owns cutscene lifetime, draws letterbox bars. See [doc/demo/03-key-actors.md](../../demo/03-key-actors.md). |
| [`fadeio.c`](../../../../source/takabe/fadeio.c) | The `FADEIO` chara — screen fade-in / fade-out. |
| [`cineutil.c`](../../../../source/takabe/cineutil.c) | Cinematic utilities. |
| [`pad_demo.c`](../../../../source/takabe/pad_demo.c) | Pre-baked pad input replay (CINEMA's `-d` option). |
| [`telop.c`](../../../../source/takabe/telop.c) | "Telop" — text overlay for stage names / chapter markers. |
| [`ed_telop.c`](../../../../source/takabe/ed_telop.c) | Ending-credit telop variant. |
| [`ending2.c`](../../../../source/takabe/ending2.c) | Ending sequence helpers. |
| [`optxtscn.c`](../../../../source/takabe/optxtscn.c) | Option / text scene utilities. |

### Camera helpers
| File | Role |
| ---- | ---- |
| [`camshake.c`](../../../../source/takabe/camshake.c) | Camera-shake effect (explosion impact). |

### Doors + opening props
| File | Role |
| ---- | ---- |
| [`door2.c`](../../../../source/takabe/door2.c) | Door actor (the second-iteration version). |
| [`shuter.c`](../../../../source/takabe/shuter.c) | Shutter / sliding-door variant. |
| [`elevator.c`](../../../../source/takabe/elevator.c) | Elevator actor. |
| [`lift.c`](../../../../source/takabe/lift.c) | Lift actor. |
| [`panel.c`](../../../../source/takabe/panel.c) | Wall panel (interactive surface). |

### Water effects
| File | Role |
| ---- | ---- |
| [`wt_view.c`](../../../../source/takabe/wt_view.c) | The famous "WT_VIEW" — a *water visual effect*, not a camera. See [doc/demo/03-key-actors.md](../../demo/03-key-actors.md). |
| [`wt_area.c`](../../../../source/takabe/wt_area.c) | Water-region actor. |
| [`wt_area2.c`](../../../../source/takabe/wt_area2.c) | Water-region variant. |
| [`wsurface.c`](../../../../source/takabe/wsurface.c) | Water surface rendering. |
| [`rsurface.c`](../../../../source/takabe/rsurface.c) | River surface variant. |
| [`gsplash.c`](../../../../source/takabe/gsplash.c) | Generic splash effect. |
| [`ripple.c`](../../../../source/takabe/ripple.c) | Single ripple. |
| [`ripples.c`](../../../../source/takabe/ripples.c) | Ripple-set actor. |

### Environmental hazards
| File | Role |
| ---- | ---- |
| [`elc_damg.c`](../../../../source/takabe/elc_damg.c) | Electric damage zone. |
| [`elc_flr.c`](../../../../source/takabe/elc_flr.c) | Electric floor. |
| [`gas_efct.c`](../../../../source/takabe/gas_efct.c) | Gas-room visual effect. |
| [`gasdamge.c`](../../../../source/takabe/gasdamge.c) | Gas damage application. |
| [`o2_damge.c`](../../../../source/takabe/o2_damge.c) | Oxygen damage (underwater / suffocation). |
| [`furnace.c`](../../../../source/takabe/furnace.c) | Furnace hazard. |
| [`tracktrp.c`](../../../../source/takabe/tracktrp.c) | Track-trap (movement trap). |

### Visual effects
| File | Role |
| ---- | ---- |
| [`prim.c`](../../../../source/takabe/prim.c) | Prim-drawing helpers. |
| [`mosaic.c`](../../../../source/takabe/mosaic.c) | Mosaic / pixelation effect (censorship). |
| [`mirror.c`](../../../../source/takabe/mirror.c) | Mirror reflection. |
| [`spark2.c`](../../../../source/takabe/spark2.c) | Secondary spark variant. |
| [`scn_mask.c`](../../../../source/takabe/scn_mask.c) | Scene mask overlay. |
| [`sepia.c`](../../../../source/takabe/sepia.c) | Sepia-tone filter (flashbacks). |
| [`fog.c`](../../../../source/takabe/fog.c) | (Possibly different from `anime/effect/fog.c`.) |

### Lighting
| File | Role |
| ---- | ---- |
| [`lit_mdl.c`](../../../../source/takabe/lit_mdl.c) | Lit-model variant rendering. |
| [`palette.c`](../../../../source/takabe/palette.c) | Palette helpers. |

### Goggles + special viewing modes
| File | Role |
| ---- | ---- |
| [`goggle.c`](../../../../source/takabe/goggle.c) | Generic goggles effect. |
| [`goggleir.c`](../../../../source/takabe/goggleir.c) | IR / thermal goggles (shows heat sigs). |
| [`focus.c`](../../../../source/takabe/focus.c) | Focus-blur effect. |

### Misc
| File | Role |
| ---- | ---- |
| [`object.c`](../../../../source/takabe/object.c) | Generic interactable. |
| [`thing.c`](../../../../source/takabe/thing.c) | (Conflicting name with `source/thing/` — unrelated.) |
| [`put_obj.c`](../../../../source/takabe/put_obj.c) | Put-object dispatcher. |
| [`life_up.c`](../../../../source/takabe/life_up.c) | Life-up pickup effect. |
| [`shakemdl.c`](../../../../source/takabe/shakemdl.c) | The `SHAKEMDL` chara — model that vibrates per-frame. |
| `breakobj.c, dymc_seg.c, dymc_flr.c, dummy_fl.c, dummy_wl.c` | Various stage-specific helpers. |
| `tex_scrl.c, envmap3.c, ir_cens.c, glass.c, env_snd.c, windcrcl.c, rasen.c, rasen_el.c, sub_efct.c, cat_in.c` | Various — mostly self-named. |

## Pattern

Most files follow:

```c
typedef struct Work {
    GV_ACT actor;
    /* …state… */
} Work;

void *NewSomething(MATRIX *world, ...)
{
    Work *work = GV_NewActor(LEVEL, sizeof(Work));
    GV_SetNamedActor(&work->actor, Act, Die, "<file>.c");
    /* init... */
    return work;
}
```

## Key takeaway: WT_VIEW

The lesson the team learned during the editor work — `wt_view.c`'s
chara hash 0x8E45 is wired as `WT_VIEW` but the function is
`NewWaterView`. It's a water visual effect, **not** a camera
animator (the disc's symbol naming is misleading). See
[doc/demo/03-key-actors.md](../../demo/03-key-actors.md) for full
context.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [effects.md](effects.md) | All ~50 actors categorised — cinema / fade / environmental / lighting / mechanical / camera |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`doc/demo/03-key-actors.md`](../../demo/03-key-actors.md) —
  CINEMA / FADEIO / WT_VIEW are documented as cutscene actors
  there.
- [`source/okajima/`](../okajima/README.md) — sister effect
  library.
- [`source/anime/effect/`](../anime/effect.md) — 2D billboard
  effect library.
