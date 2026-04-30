---
file: source/takabe/ (per-author folder)
---

# `source/takabe/` — Takabe's special-effect / system actors

Per-author folder authored by **Takabe**, one of the lead
programmers. Hosts ~50 actor pairs (.c + .h), spanning environment
effects, fade/overlay infrastructure, and a few gameplay objects.

## Categorisation

### Cinema / fade infrastructure

| File | Effect |
| ---- | ------ |
| `cinema.c` | The "letterbox" cinematic bars + master cinema-mode toggle |
| `cineutil.c` | Helpers for cinematic actors (camera tween, mute) |
| `fadeio.c` | Screen fade in/out (in to gameplay / out to load) |
| `mosaic.c` | Pixelation effect (used during glitch transitions) |
| `panel.c` | "Panel" overlay (split-screen / multi-cam HUD) |
| `palette.c` | Palette swap utilities |
| `sepia.c` | Sepia tone — used in flashback cinematics |
| `telop.c` | TELOP — large-text cinematic captions |
| `ed_telop.c` | TELOP for ending sequences |
| `ending2.c` | Ending sequence orchestration |

### Environmental effects

| File | Effect |
| ---- | ------ |
| `windcrcl.c` | Wind circle — visualise wind direction in some stages |
| `gas_efct.c` / `gasdamge.c` | Toxic gas effect + damage |
| `gsplash.c` | Big splash (water) |
| `ripple.c` / `ripples.c` | Water ripples |
| `rsurface.c` / `wsurface.c` | River/water surface |
| `wt_area.c` / `wt_area2.c` | Water trigger volumes |
| `wt_view.c` | Water-camera view (underwater) |
| `glass.c` | Breakable glass |
| `furnace.c` | Furnace heat distortion |
| `o2_damge.c` | Oxygen damage (underwater suffocation) |

### Lighting / visual modifiers

| File | Effect |
| ---- | ------ |
| `goggle.c` / `goggleir.c` | Thermal / IR goggle overlays |
| `envmap3.c` | Environment-map (cube reflection) |
| `lit_mdl.c` | Per-model lighting override |
| `mirror.c` | Mirror-surface rendering |
| `tex_scrl.c` | Scrolling texture (water surface, conveyor) |
| `optxtscn.c` | "Option text scene" — option-screen background |
| `scn_mask.c` | Scene-mask occlusion |
| `shakemdl.c` | Per-model shake |

### Mechanical objects

| File | Effect |
| ---- | ------ |
| `door2.c` | Alternative door variant |
| `breakobj.c` | Breakable object (explodable scenery) |
| `dummy_fl.c` / `dummy_wl.c` | Dummy floor / wall (cinematic placeholder) |
| `dymc_seg.c` | Dynamic HZD segment (sister to `enemy/dymc_seg.c`) |
| `elc_damg.c` / `elc_flr.c` | Electric damage / floor |
| `elevator.c` | Elevator actor |
| `lift.c` | Lift / platform |
| `panel.c` | Panel UI |
| `shuter.c` | Shutter (wall-mounted) |
| `tracktrp.c` | Track trap (rail-mounted) |
| `life_up.c` | Life-up pickup |
| `ir_cens.c` | IR sensor (security beam) |
| `rasen.c` / `rasen_el.c` | "Rasen" — spiral / spring objects |

### Camera + special

| File | Effect |
| ---- | ------ |
| `camshake.c` | Camera shake API (consumed by `game/camera.c`) |
| `focus.c` | Camera focus (cinematic depth-of-field) |
| `pad_demo.c` | Demo pad-input recording / playback |
| `vib_edit.c` | Vibration editor (debug) |
| `env_snd.c` | Environmental sound (per-zone ambience) |
| `ripple.c` | Water ripple |
| `prim.c` | DG_PRIM helpers |
| `put_obj.c` | DG_PutObjs helpers |
| `spark2.c` | Spark variant |
| `sub_efct.c` | Generic sub-effect |
| `unknown4.c` | Unknown |

## Style of code

Most files follow the same shape:

- `XxxxWork` struct (~64–512 bytes).
- `NewXxxx(...)` factory with GCL-options reading.
- `XxxxAct(...)` per-tick callback.
- `XxxxDie(...)` shutdown.

Each registers as `GV_ACTOR_LEVEL5` / `LEVEL6` (effects).

## `cinema.c` — the cinema master

The most important file in `takabe/`. Owns the cinematic-mode
master state:

- Letterbox bars (top / bottom black borders).
- BGM ducking (drop volume during cinematics).
- HUD hide flag (`STATE_CINEMA` bit in `GM_GameStatus`).
- Pad mask (most buttons disabled; START still skips).

Cinematic enter/exit:

```c
GM_BeginCinema():
    GV_PauseLevel |= 4
    DG_FadeScreen / spawn LETTERBOX
    GM_GameStatus |= STATE_CINEMA
    BGM duck

GM_EndCinema():
    GV_PauseLevel &= ~4
    Despawn LETTERBOX
    GM_GameStatus &= ~STATE_CINEMA
    BGM restore
```

Triggered by GCL `cinema enter` / `cinema leave` commands.

## `fadeio.c` — fade in/out

Standard cinematic fade — black/white over N frames. Triggered by
GCL `fade` command (see [game/script.md](../game/script.md)).
Owns a `FADE` actor that draws a full-screen quad with animated
alpha.

## `goggle.c` / `goggleir.c`

Thermal / IR goggles — a per-pixel post-process. Implemented as a
DG_PRIM full-screen quad with a custom shader-equivalent CLUT
(palette mapping reds to whites for thermal etc.).

## See also

- [`source/anime/effect.md`](../anime/effect.md) — sister 2D
  effects library.
- [`source/okajima/`](../okajima/index.md) — sister 3D effects
  library (different author).
- [`source/game/camera.md`](../game/camera.md) — consumes
  `camshake.c`.
