# `chara/torture/` — the torture-room cast

Stage **s03c** — the torture sequence after Snake is captured by
Ocelot. Six character actors + helpers, ~3500 lines. Each
character has dedicated logic because they have unique scripted
behaviours that don't reuse generic guard AI.

## Files

| File | Lines | Character / role |
| ---- | ----- | ---------------- |
| [`bed.c`](../../../../source/chara/torture/bed.c) | ~? | The torture rack itself — interactive prop with "shock" / "rest" states. |
| [`boxall.c`](../../../../source/chara/torture/boxall.c) | ~? | Container of all box variants used in the scene. |
| [`info.c`](../../../../source/chara/torture/info.c) | ~? | Info panel / monitor overlay. |
| [`jfamas.c`](../../../../source/chara/torture/jfamas.c) | ~? | A guard with FAMAS in the torture room — variant of zako with custom AI for this scene. |
| [`johnny.c`](../../../../source/chara/torture/johnny.c) | ~? | Johnny Sasaki — the comic-relief guard outside the cell. |
| [`johnny2.c`](../../../../source/chara/torture/johnny2.c) | ~? | Johnny variant for a second appearance. |
| [`ninja.c`](../../../../source/chara/torture/ninja.c) | ~? | Cyborg Ninja — appears mid-torture. |
| [`otacom.c`](../../../../source/chara/torture/otacom.c) | ~? | Otacon (Hal Emmerich) — codec-call participant during the scene. |
| [`revolver.c`](../../../../source/chara/torture/revolver.c) | ~? | Revolver Ocelot — the torturer. |
| [`sne_03c.c`](../../../../source/chara/torture/sne_03c.c) | ~? | Snake variant for s03c — strapped-to-table Snake state. |
| [`torture.c`](../../../../source/chara/torture/torture.c) | ~? | Stage-coordinator actor that ties everything together. |
| [`unknown5.c`](../../../../source/chara/torture/unknown5.c) | ~? | Unidentified support actor. |
| [`unknown7.c`](../../../../source/chara/torture/unknown7.c) | ~? | Unidentified support actor. |

## Scene structure

The torture room is unique in that it's:

1. **Interactive cinematic** — Snake is strapped to the rack
   (sne_03c) and the player has to mash buttons to resist; failing
   ends the scene with Meryl dying.
2. **Multi-character drama** — Ocelot, the player Snake, Johnny
   outside, eventually Ninja breaks in. Each needs custom code
   because their behaviour is heavily scripted.
3. **Codec calls** — Otacon talks to Snake mid-scene; that uses
   the standard `RADIO` actor but with stage-specific timing.

## Per-character notes

### `revolver.c` — Ocelot

Ocelot's torture animations + dialogue triggers + the famous
"You're pretty good." line. State machine:

```
intro → speak → torture (loop with fail-detect) → angry → escape
```

Each state has its own animation set + voice cue list. The
"fail-detect" branches if the player can't keep up with button
mashing.

### `sne_03c.c` — strapped Snake

Snake variant *during the torture scene*. Cannot move, cannot
fire, cannot pick up items. Just:

- Reads pad input (mash X to resist).
- Plays struggle / scream animations on input.
- Drains stamina if the player isn't mashing fast enough.

When the scene ends, this actor is destroyed and the regular
`SnaInitWork` from `chara/snake/` is re-spawned.

### `johnny.c` / `johnny2.c` — Johnny Sasaki

The bumbling guard outside Snake's cell who constantly has
diarrhea (running joke). Custom AI:

- Walks to the latrine periodically.
- Squats in the latrine for ~15 seconds.
- Returns to post.

When Snake plays the listen-for-diarrhea moment, this actor's
behaviour is what generates the audio cue.

### `ninja.c` — Cyborg Ninja

The cybernetically-enhanced (formerly Gray Fox) ninja that breaks
into the torture room mid-scene. Has unique:

- Optical-camo state (invisible / partially-visible / visible).
- Phase-through-walls movement.
- Sword-attack animations.

The optical-camo affects rendering — `DG_FLAG_TRANS` toggling
based on his visibility state.

### `otacom.c` — Otacon

Otacon is technically just a voice on the codec, but
`otacom.c` is the actor that triggers his lines at the right
points in the torture scene + handles the codec-icon overlay.

### `bed.c` — the torture bed/rack

Interactive prop with a TARGET volume — when the player loses,
the bed delivers shock damage to Snake's TARGET.

### `boxall.c`

A container/registry that holds all the boxes / props in the
room. Spawns them, manages their lifecycle, releases on stage
exit.

### `torture.c` — scene coordinator

The "manager" actor that:

- Owns the scene state machine (intro → torture loop → ninja-
  break-in → outro).
- Sequences character spawns / despawns.
- Drives the GCL `mesg` chain that switches each character into
  their next state.

Conceptually similar to the master `GameWork` actor in
`gamed.c` but scoped to one stage's cinematic.

## Relation to other folders

- The standard `chara/snake/` is **replaced** with `sne_03c.c`
  for the duration of the scene.
- `enemy/` guards are **suppressed** — torture room has its own
  J-FAMAS guard via `jfamas.c`.
- The cinematic camera comes from a per-stage overlay actor
  (or from streamed `.dmo` data), not from the torture/ files.

## See also

- [snake.md](snake.md) — full Snake.
- [`source/overlays/s03c/`](../../../../source/overlays/s03c) —
  s03c-specific overlay code that links in this folder.
- [`doc/demo/`](../../demo/) — scene's cinematic uses streamed
  dmos for some camera moves.
