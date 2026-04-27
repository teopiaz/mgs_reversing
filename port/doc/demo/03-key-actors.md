# Key actors in a cutscene

Every cutscene is built out of ~7 actor types that show up over and
over. This file documents the role each plays, the GCL options they
care about, and which source file implements them.

For the full chara hash table see
[`source/include/charalst.h`](../../../source/include/charalst.h).
For the registration list (which factories are available in the
running binary) see
[`port/extern_stubs.c`](../../../port/extern_stubs.c)'s
`MainCharacterEntries[]`.

## CINEMA — `chara &CINEMA $s:HHHH -t N`

| | |
| --- | --- |
| Hash | `0x6ECA` |
| Source | [source/takabe/cinema.c](../../../source/takabe/cinema.c) |
| Constructor | `NewCinema` |
| Level | 3 |

The cutscene's *lifetime owner*. Every demo has exactly one. `-t N`
declares the demo runs for `N` ticks (60 ticks = 1 second) — when
`GV_Clock` exceeds the spawn time + `N`, the cinema actor self-
destructs and triggers the next state transition (typically a `load`
of the next stage).

What CINEMA does NOT do, despite the name: **it doesn't drive the
camera**. Its `Act()` adds two black-rectangle prims to
`DG_Chanl(1)`'s OT, drawing the letterboxing bars at the top + bottom
of the screen. The camera animation is `WT_VIEW`'s job (below).

## WT_VIEW — `chara $s:8E45 $s:HHHH -b X1 Y1 Z1 X2 Y2 Z2 -c R G B`

| | |
| --- | --- |
| Hash | `0x8E45` |
| Source | [source/takabe/wt_view.c](../../../source/takabe/wt_view.c) |
| Constructor | `NewWaterView` |
| Level | 3 |

The cutscene-camera *animator*. Reads its `-b` (bound box, two
SVECTORs) and `-c` (CVECTOR colour) options at spawn, then mutates
`gUnkCameraStruct2_800B7868.eye / .center / .zoom` every frame
according to the cinematic's pre-baked motion curve.

The struct it writes is global and read by `camera.c` — see
[04-camera-pipeline.md](04-camera-pipeline.md) for the chain.

The name "WT_VIEW" is the disc symbol — internally it's "water view"
(`NewWaterView`), an early-development title. It's the one cutscene
actor whose absence makes the cinematic camera appear frozen, so
seeing `chara: func not found (hash=0x8E45)` in stderr means the
runtime never spawned the animator and the camera will sit at
`DG_LookAt(eye=center=zero)` for the entire demo.

## DEMODOLL — `chara &DEMODOLL $s:HHHH -m $s:KMD -p X Y Z [-r yaw] …`

| | |
| --- | --- |
| Hash | `0xCDDD` |
| Source | [source/animal/doll/demodoll.c](../../../source/animal/doll/demodoll.c) |
| Constructor | `NewDemoDoll` |
| Level | 4 (mostly) |

A character playing a canned animation. `-m $s:KMD` references the
geometry blob (Snake, Meryl, a guard, …). `-p X Y Z` is the world
position. `-r b:N` is yaw. Various options choose which animation
clip to play and at what playback rate.

DEMODOLL is the cutscene equivalent of the regular `SNAKE` /
`WATCHER` / `MERYL` actors. The difference: DEMODOLL doesn't read
controller input or run AI — it walks through pre-baked motion data,
which is what makes cutscene replay deterministic.

In a typical scene there are 1–6 DEMODOLLs (Snake + the people he's
talking to, plus any background NPCs).

## EMITTER — `chara &EMITTER $s:HHHH -t $s:tex -s SX SY SZ -p P0 P1 P2 …`

| | |
| --- | --- |
| Hash | `0x2D07` |
| Source | [source/anime/effect/](../../../source/anime/effect/) (various) |
| Constructor | `NewEmitter` (chara dispatch may vary by `-t`) |
| Level | 5 |

Particle / sprite spawner. Used for smoke, dust, sparks, drifting
snow, light shafts, etc. Highly parameterised — `-t` chooses the
texture, `-s` the sprite scale envelope, `-p` lists 3 spawn points,
`-l` lifetime, `-r` rate, etc.

A single d-stage cutscene can have 5–15 emitters (e.g. the d00a
opening with snow drifting across the helipad).

## WALL / DYNWALL / DMYWALL

| | |
| --- | --- |
| Hashes | `0xD14A`, `0xC056`, `0x6FC4`, … |
| Source | [source/enemy/wall.c](../../../source/enemy/wall.c) |
| Constructor | `NewWall` |
| Level | 5 |

Static-prop placeholders. The disc stage's geometry KMD is loaded
once; `WALL` actors register specific KMDs as renderable per-frame
via the OT pipeline. Cutscenes sometimes spawn additional walls to
add or hide pieces of geometry.

DYNWALL is the destructible variant; DMYWALL is the "dummy" used as
a placeholder during cinematic transitions.

## LAMP / PATO_LAMP

| | |
| --- | --- |
| Hashes | `0x?` (LAMP), `0x5BA4` (PATO_LAMP) |
| Source | [source/game/lamp.c](../../../source/game/lamp.c), [source/okajima/pato_lmp.c](../../../source/okajima/pato_lmp.c) |
| Level | 4 |

Light sources — directional, point, or rotating (PATO_LAMP is the
sweeping searchlight). Cutscenes pose them statically; gameplay
scenes have them animate (e.g. patrolling watch towers).

## SHAKEMDL — `chara $s:790B …`

| | |
| --- | --- |
| Hash | `0x790B` |
| Source | [source/takabe/shakemdl.c](../../../source/takabe/shakemdl.c) |
| Constructor | `NewShakeModel` |
| Level | 5 |

Model that "shakes" — wobbles slightly each frame for a procedural
breathing / vibration effect. Used for environmental dressing
(rotors, animated pipes, ductwork humming).

## FADEIO — `chara &FADEIO $s:HHHH -t T1 T2 -c R G B`

| | |
| --- | --- |
| Hashes | `0x0003`, `0x0004` |
| Source | [source/takabe/fadeio.c](../../../source/takabe/fadeio.c) |
| Constructor | `NewFadeIo` |
| Level | 3 |

Screen fade. Almost every cutscene starts with one fading from black
and ends with another fading to black. Pure 2D — adds a screen-sized
quad to `DG_Chanl(1)`'s OT with an alpha that interpolates over `-t`
ticks.

## RADIO — `chara &RADIO $s:HHHH …`

| | |
| --- | --- |
| Hash | `0x24E1` |
| Source | [source/menu/radio.c](../../../source/menu/radio.c) |

Codec call. Triggers the side-portrait HUD + voice playback. In the
editor's embedded player, audio is not pumped, so codec calls draw
the portraits but stay silent.

## JIMAKU — `chara &JIMAKU $s:HHHH …`

| | |
| --- | --- |
| Hash | `0xEC9D` |
| Source | [source/menu/jimaku.c](../../../source/menu/jimaku.c) |

Subtitle. Renders Japanese / English text in the bottom of the
screen, sourced from the stage's font + radio dialogue tables.

## "func not found (hash=0xNNNN)" — what to do

When `GCL_ExecScript` walks the demo and hits a `chara $s:NNNN` whose
factory isn't registered in `MainCharacterEntries[]`, it logs:

```
[gcl] chara: func not found (hash=0xNNNN)
```

To fix, look up the hash in
[`source/include/charalst.h`](../../../source/include/charalst.h),
find the `CHARA_<NAME>` macro, and add it to
[`port/extern_stubs.c`](../../../port/extern_stubs.c)'s
`MainCharacterEntries[]` array near the existing demo entries. If
the constructor's source file isn't compiled into `port/obj/`,
that's a separate fix — most disc cutscene actors are linked
already, but some demo-only ones from per-stage overlays may not be.
