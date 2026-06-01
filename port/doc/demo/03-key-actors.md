# Key actors in a cutscene

Every cutscene is built out of a handful of recurring actor types.
This file documents the role each plays, the GCL `chara` hash it
registers under, which source file implements it, and which actor
priority level it spawns at.

Authoritative source for hashes and constructors is
[`source/include/charalst.h`](../../../source/include/charalst.h)
(the `CHARA_*` macros) and
[`source/include/strcode.h`](../../../source/include/strcode.h)
(the matching `CHARAID_*` symbolic constants). The factories that
are actually linked into the running binary are listed in
[`port/extern_stubs.c`](../../../port/extern_stubs.c)'s
`MainCharacterEntries[]`.

Actor priority levels referenced below — from
[`source/libgv/libgv.h`](../../../source/libgv/libgv.h):

| Level | Constant            | Used by               |
| ----- | ------------------- | --------------------- |
| 0     | `GV_ACTOR_DAEMON`   | engine daemons        |
| 1     | `GV_ACTOR_MANAGER`  | GameWork, radio, HUD  |
| 2     | `GV_ACTOR_ASSIST`   | assist passes         |
| 3     | `GV_ACTOR_PREV2`    | very-early actors     |
| 4     | `GV_ACTOR_PREV`     | character actors      |
| 5     | `GV_ACTOR_USER`     | gameplay objects, VFX |
| 6     | `GV_ACTOR_AFTER`    | post-process          |
| 7     | `GV_ACTOR_AFTER2`   | late post-process     |
| 8     | `GV_ACTOR_DAEMON2`  | terminal daemons      |

## CINEMA

| | |
| --- | --- |
| Hash | `0x7A05` (`GV_StrCode("シネマスクリーン")`) |
| Source | [`source/takabe/cinema.c`](../../../source/takabe/cinema.c) |
| Constructor | `NewCinemaScreenSet(name, where, argc, argv)` |
| Level | 3 (`GV_ACTOR_PREV2`) |

The cinema-bar actor. Its `Act()` adds two strips to
`DG_Chanl(1)`'s OT — a top bar and a bottom bar — drawing the
familiar widescreen-letterbox masking that signals "you're in a
cutscene". The two strips are emitted as `POLY_G4` (during the
fade-in / fade-out grayscale ramp, drawn under subtractive blend)
and as opaque `TILE` once fully on.

What CINEMA does **not** do, despite the name: it doesn't drive
the camera, doesn't sequence the cutscene, doesn't load anything.
It's a pure 2D overlay. Camera control lives in the demo runtime
(see WT_VIEW below).

`NewCinemaScreen(time, type)` / `NewCinemaScreenClose(work)` are
the direct-call entry points; `NewCinemaScreenSet` is the GCL
wrapper.

## WT_VIEW

| | |
| --- | --- |
| Hash | `0x8E45` (`GV_StrCode("水中主観")` — "underwater 1st-person") |
| Source | [`source/takabe/wt_view.c`](../../../source/takabe/wt_view.c) |
| Constructor | `NewWaterView(name, where, argc, argv)` |
| Level | 3 (`GV_ACTOR_PREV2`) |

A **water visual effect**, *not* a camera animator (the symbol
name is misleading). Its `Act()` reads `DG_Chanls[1].eye.t[]` —
the current camera position — and, when that point is inside the
bound box passed in via GCL, allocates sprite + tile primitives
and draws an animated underwater tint. Outside the bound box it
frees the prims and idles. It writes nothing to camera state.

Where the cutscene camera actually comes from depends on the
demo's type ([01-overview.md](01-overview.md)):

- **Streamed** (`demo -s` / `demo -f`):
  [`source/kojo/demo.c::FrameRunDemo`](../../../source/kojo/demo.c)
  writes `gUnkCameraStruct2_800B7868.eye/center` per frame from the
  baked `DMO_DAT.eye_x/y/z`. This is what drives every disc-shipped
  dramatic cinematic.
- **GCL-scripted only** (no `demo -s`): nothing is built in. The
  camera stays at whatever the previous gameplay tick left it. Some
  per-stage overlays spawn a custom camera actor that writes
  `gUnkCameraStruct2` directly —
  [`source/overlays/s19b/takabe/democame.c`](../../../source/overlays/s19b/takabe/democame.c),
  [`source/overlays/s11g/okajima/11g_demo.c`](../../../source/overlays/s11g/okajima/11g_demo.c),
  [`source/chara/others/intr_cam.c`](../../../source/chara/others/intr_cam.c)
  are the most common. These are **not** WT_VIEW.

So a `chara: func not found (hash=0x8E45)` log means the stage will
have no animated water effect, but it does **not** explain a frozen
cinematic camera — that points at the streamed-demo path instead
(see [09-streamed-demos.md](09-streamed-demos.md)).

## DEMODOLL

| | |
| --- | --- |
| Hash | `0xE97E` (`GV_StrCode("デモ人形")` — "demo doll") |
| Source | [`source/animal/doll/doll.c`](../../../source/animal/doll/doll.c) |
| Constructor | `NewDemoDoll(name, where, argc, argv)` |
| Level | 4 (`GV_ACTOR_PREV`) |

A character playing a canned animation. The `chara &DEMODOLL`
directive references a KMD (Snake, Meryl, a guard, …), a world
position, and an animation clip; DEMODOLL spawns the actor with
those settings, sets `step_size = 0` so the doll doesn't run its
own movement integration, and walks through the pre-baked motion
data each tick.

DEMODOLL is the cutscene equivalent of the regular `SNAKE` /
`WATCHER` / `MERYL` actors. The difference: it doesn't read
controller input or run AI — which is what makes cutscene replay
deterministic. In a typical scene there are 1–6 DEMODOLLs (Snake
+ the people he's talking to, plus background NPCs).

(`source/animal/doll/doll.c` is the runtime; `demodoll.c` /
`demodoll0.c` in the same folder hold per-clip animation tables
and stage-specific behaviour hooks.)

## EMITTER

| | |
| --- | --- |
| Hash | `0x32E5` (`GV_StrCode("ジン発光")`) |
| Source | [`source/thing/emitter.c`](../../../source/thing/emitter.c) |
| Constructor | `NewEmitter(name, where, argc, argv)` |
| Level | 5 (`GV_ACTOR_USER`) |

Particle / sprite spawner. Used for snow, dust, smoke, sparks,
light shafts. The GCL `chara` invocation reads its spawn vectors
via `GCL_GetOption('p')` — a list of 3 XYZ points. The texture
and motion preset come from `name` (the chara-instance hash, which
maps to one of the pre-baked emitter prototypes loaded by
`GetResources`).

A variant `CHARAID_EMITTER2 = 0xA9DD` covers a separate emitter
flavour (different default particle behaviour) — same source
file, same constructor by way of the chara table.

A single d-stage cutscene can have 5–15 emitters (e.g. the d00a
opening with snow drifting across the helipad).

## FADEIO

| | |
| --- | --- |
| Hash | `0xA12E` (`GV_StrCode("白黒フェド")` — "black/white fade") |
| Source | [`source/takabe/fadeio.c`](../../../source/takabe/fadeio.c) |
| Constructor | `NewFadeInOutSet(name, where)` |
| Level | 3 (`GV_ACTOR_PREV2`) |

Screen fade. Almost every cutscene starts with a fade-in and ends
with a fade-out. The actor adds a screen-sized opaque quad to
`DG_Chanl(1)`'s OT and ramps its color from black/white toward the
clear color over the ramp speed read from GCL option `-s` (with
mode/color from `-m`):

```
mode bit 0:  0 = MODE_FADEOUT, 1 = MODE_FADEIN
mode bit 1:  0 = MODE_BLACK,   1 = MODE_WHITE
```

`NewFadeInOut(mode, shade)` is the direct-call entry; `NewFadeInOutSet`
is the GCL wrapper used by `chara &FADEIO`.

## WALL / DYNWALL / DMYWALL

Three related actors that pose static or pseudo-static geometry
during cutscenes. The stage's main geometry KMD is already loaded;
these add or hide specific pieces.

| Type | Hash | Constructor | Source | Level |
| ---- | ---- | ----------- | ------ | ----- |
| WALL    | `0xEC77` (`障害物`)  | `NewWallGcl`        | [`source/enemy/wall.c`](../../../source/enemy/wall.c)         | 5 (USER) |
| DYNWALL | `0xB103` (`透明壁`)  | `NewDynamicWallSet` | [`source/takabe/dymc_seg.c`](../../../source/takabe/dymc_seg.c) | 5 (USER) |
| DMYWALL | `0x58F0` (`塗り壁`)  | `NewDummyWall`      | [`source/takabe/dummy_wl.c`](../../../source/takabe/dummy_wl.c) | 5 (USER) |

WALL registers a specific named KMD as renderable per-frame.
DYNWALL is the moving / animated variant (sliding doors,
opening shutters). DMYWALL is the placeholder used as a stand-in
during cinematic transitions — it's invisible but occupies space
so collision and visibility queries don't fall through.

(There's also `CHARAID_DYNFLOOR = 0xAF6C` — same pattern for a
horizontal surface.)

## Lamps — LAMP / PILOTLAMP / PATOLAMP

Three lamp actors with distinct hashes and behaviours.

| Type      | Hash    | Constructor       | Source                          | Level |
| --------- | ------- | ----------------- | ------------------------------- | ----- |
| TEXTURE   | `0x1AD3` (`テクスチャ`) | `NewTextureLamp` | [`source/game/lamp.c`](../../../source/game/lamp.c)           | varies |
| PILOTLAMP | `0x169C` (`パイロットランプ`) | `NewPilotLamp` | [`source/okajima/p_lamp.c`](../../../source/okajima/p_lamp.c) | varies |
| PATOLAMP  | `0x30CE` (`パトランプ`) | `NewPatrolLamp` | [`source/okajima/pato_lmp.c`](../../../source/okajima/pato_lmp.c) | 4 (PREV) |

TEXTURE-lamp toggles a CLUT slot to brighten / darken a region
(static lighting). PILOTLAMP is the small blinking indicator on
machinery. PATOLAMP is the rotating searchlight ("patrol lamp") —
the sweeping red beam on watchtowers + alert state.

The original codebase does not register a generic `CHARA_LAMP` —
the three named variants above are what GCL scripts spawn.

## SHAKEMODEL

| | |
| --- | --- |
| Hash | `0xBA52` |
| Source | [`source/takabe/shakemdl.c`](../../../source/takabe/shakemdl.c) |
| Constructor | `NewShakeModelGCL(name, where, argc, argv)` |
| Level | 5 (`GV_ACTOR_USER`) |

Model that "shakes" — wobbles slightly on a configurable axis each
frame for a procedural vibration effect. Used for environmental
dressing: rotating pipes, humming ductwork, vibrating machinery
during alerts.

`NewShakeModel(model, axis, scale)` is the direct-call entry;
`NewShakeModelGCL` is the GCL wrapper.

## Cutscene-cooperating systems (not GCL chara entries)

Two systems show up in every cutscene's behaviour but are **not**
spawned via `chara` directives:

### Subtitles (JIMAKU)

The subtitle layer is driven by
[`source/game/jimctrl.c`](../../../source/game/jimctrl.c)'s
`MENU_JimakuWrite` API, called by the streamed-demo runtime and by
in-script `mesg` events. Rendering is in
[`source/menu/jimaku.c`](../../../source/menu/jimaku.c). It uses
the font rasteriser ([source/font/](../source/font/index.md)) plus
the stage's tail-font KMD for kanji glyphs not in the resident set.
There's no `CHARA_JIMAKU` in `charalst.h`; subtitles aren't an
actor in the GCL sense, they're a callable system.

### Codec dialog (RADIO)

The codec is run by
[`source/menu/radio.c`](../../../source/menu/radio.c) — caller
portrait + voice playback through the VOX streamer. Triggered by
gameplay code or, in a cutscene, by an `OnCodec` / `mesg` event in
the GCL script. Like JIMAKU, it has no `chara` hash; it's the
HUD-tier counterpart of the cutscene actors above.

In the editor's embedded player, audio isn't pumped, so codec
calls draw portraits but stay silent.

## "func not found (hash=0xNNNN)" — what to do

When `GCL_ExecScript` walks the demo and hits a `chara $s:NNNN`
whose factory isn't registered in `MainCharacterEntries[]`, it
logs:

```
[gcl] chara: func not found (hash=0xNNNN)
```

To fix:

1. Look up the hash in
   [`source/include/charalst.h`](../../../source/include/charalst.h)
   — find the `CHARA_<NAME>` macro whose first field matches.
2. Add the same `{ hash, constructor }` entry to
   [`port/extern_stubs.c`](../../../port/extern_stubs.c)'s
   `MainCharacterEntries[]`.
3. Confirm the constructor's source file is being compiled into
   `port/obj/`. Most disc-shipped cutscene actors are linked
   already; some demo-only ones from per-stage overlays (e.g. the
   stage-private chara tables in `source/stage/*.c`) may need to
   be added.

If the hash is *not* in `charalst.h` at all, the GCL script is
referencing an actor the disc binary doesn't even define — that's
either a custom stage's authored actor that needs adding to the
codebase, or a typo in the script's `chara` line.

## See also

- [`source/contrib/dev/`](../../../source/contrib/dev/) —
  `MainCharacterEntries[]` (the global cross-stage chara table).
- [`source/include/charalst.h`](../../../source/include/charalst.h)
  — every `CHARA_*` macro the engine recognises.
- [`source/include/strcode.h`](../../../source/include/strcode.h)
  — matching `CHARAID_*` symbolic constants for use in C.
- [02-data-flow.md](02-data-flow.md) — how a `chara` directive in
  a `.gcx` file reaches the constructor in the running binary.
- [09-streamed-demos.md](09-streamed-demos.md) — the
  pre-baked-camera path that DEMODOLL animations sync to.
