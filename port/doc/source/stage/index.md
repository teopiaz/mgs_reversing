# `source/stage/` — per-stage registration tables

One source file per playable stage (and per non-gameplay screen).
The folder's job is small but **everywhere**: each file defines the
chara-hash → factory table the engine uses to spawn that stage's
unique actors when GCL scripts mention them by name.

93 files in total. The folder is a registration directory; the real
gameplay logic for a given stage lives in
[`source/overlays/<stage>/`](../../../../source/overlays/) (for the
half-dozen heavily-scripted stages) and the various per-author
folders (`okajima/`, `takabe/`, `chara/`, `enemy/`, …).

## The chara-table pattern

Every gameplay stage file looks the same:

```c
const NEW_CHARA_ENTRY _StageCharacterEntries_<stage>[] = {
    { CHARA_<TYPE_1>,  NewType1,  NewType1Init  },
    { CHARA_<TYPE_2>,  NewType2,  NewType2Init  },
    /* …stage-private chara types only… */
    { 0, NULL, NULL }                /* terminator */
};
```

`NEW_CHARA_ENTRY` is `(hash, factory, init)` — a 16-bit GCL hash
("`watcher`", "`elev`", "`crane`", etc.) bound to the `New*`
constructor + a one-shot init function that registers the type with
the actor system.

When the engine boots the stage's GCL script and hits a `chara`
directive (`chara watcher x:120 y:0 z:48 …`), it looks the chara
hash up first in `MainCharacterEntries[]` (the global table holding
DEMODOLL, FADEIO, CINEMA, EMITTER, WT_VIEW — see
[`source/contrib/dev/`](../../../../source/contrib/dev/)) and then in
the current `StageCharacterEntries`. The first match wins, so global
actors don't need re-registering per stage but stages can override
("`watcher` here is the canyon-watcher, not the corridor-watcher").

## How a stage's table gets selected

`gamed.c::GM_LoadInitBin` switches on `GM_CurrentMapFlag` (a 16-bit
hash of the stage code, set during `GM_LoadStage` via
`GV_StrCode(name)`) and assigns the right pointer:

```c
switch (GM_CurrentMapFlag) {
    case 0xC693: g_StageEntries = _StageCharacterEntries_d00a; break;
    case 0x46BB: g_StageEntries = _StageCharacterEntries_s01a; break;
    case 0xCAA7: g_StageEntries = _StageCharacterEntries_s02a; break;
    /* …95 cases… */
}
```

The terminator entry `{ 0, NULL, NULL }` lets the lookup walk stop
without needing to carry a count. Stage authors append entries
freely; nothing else has to change.

## Stage naming convention

| Prefix | Meaning |
| ------ | ------- |
| `s*`   | "Stage" — playable gameplay. Hours of in-game time. |
| `d*`   | "Demo" — cinematic-only. No player control; runs a streamed `.dmo` from `DEMO.DAT`. |
| Suffix `r` | "Redux" — the harder-difficulty / boss-rematch alternate. Shares geometry, different actor list. |

The two digits after `s`/`d` group stages by area, the letter
sub-divides:

- `s00*` opening, `s01*` heliport, `s02*` tank hangar, `s03*` cells,
  `s04*` comm tower, `s05*` comms, `s07*` canyon (M1A1 boss), `s08*`
  Sniper Wolf 1, `s10*` underground, `s11*` Hind hangar / escape,
  `s12*` Wolf 2, `s15*` Raven, `s16*` underground tank, `s17*` final
  approach, `s18*` cockpit, `s19*` jeep, `s20*` final escape.
- `d00a, d01a, d03a, d11c, d16e, d18a` etc. — the dramatic
  cinematics between gameplay (d00a = Snake-arrives intro, d11c =
  torture, d18a = Metal Gear reveal).

## Non-gameplay screens (also in this folder)

Several entries don't represent gameplay stages but use the same
registration mechanism so the loader can treat them uniformly:

| File | What it is |
| ---- | ---------- |
| `title.c` | Title screen — between pre-opening and main menu. |
| `select.c`, `select1.c`…`select4.c`, `selectd.c` | Stage / VR-mission select screens. |
| `option.c` | Option-menu screen (wraps `onoda/option/opt.c`). |
| `preope.c` | Pre-opening cinematic (wraps `onoda/preope/`). |
| `demosel.c` | Post-clear cinematic gallery (wraps `onoda/demosel/`). |
| `rank.c` | Post-clear ranking / dog-tag display. |
| `abst.c`, `brf.c` | Abstract / briefing scenes (the spinning-globe disc-2 intermission). |
| `change.c` | Stage-change coordinator (wraps `onoda/change/`). |
| `ending.c`, `endingr.c`, `roll.c` | Endings + credit roll. |
| `camera.c`, `sound.c` | Special hooks — `camera` is the bare camera test stage, `sound` is the audio-only stage used by debug. |
| `movie.c` | FMV playback wrapper. |
| `photo_*.c` | Disc-2 photo unlock backdrops. |
| `vab_*.c` | "VAB" = wave-bank loaders (per-area sound banks). Not stages in the gameplay sense; they're registration hooks for the loader's sound section. |

## When a stage needs more than a table

For about a dozen stages the gameplay is intricate enough that the
table-of-charas isn't sufficient — the stage needs its *own
per-frame logic*. Those live in
[`source/overlays/<stage>/`](../../../../source/overlays/):

| Overlay folder | Stage | Why it has its own folder |
| -------------- | ----- | -------------------------- |
| `s04c/` | comm tower top | Otacon-room sequence — multiple interleaved actors |
| `s07a/` | M1A1 canyon | Tank fight choreography + bespoke vehicle |
| `s08b/`, `s08br/` | Sniper Wolf 1 + redux | Sniper-room scripting |
| `s11d/`, `s11e/`, `s11g/`, `s11i/` | Hind chase + Hind boss | Hind-helicopter behaviour, rocket launcher scripting |
| `s12c/` | Sniper Wolf 2 | Snowfield wolf-pack choreography |
| `s15c/` | Vulcan Raven | The big gatling fight |
| `s19b/`, `s19br/` | Jeep escape | Liquid chase logic |
| `d18a/`, `d18ar/` | MG reveal | Custom camera & lighting |
| `title/`, `camera/` | non-gameplay | Bespoke title-screen + free-cam test |

In all cases the `source/stage/<stage>.c` table is still the entry
point — it lists the overlay's actors. The overlay folder provides
the bodies.

## Stage VR ("Special" disc)

The `Integral` package includes a VR-missions disc with its own set
of stages under [`source/stagevr/`](../../../../source/stagevr/) —
same registration pattern, distinct stage codes (the missions are
keyed by VR-disc identifiers). The stage select for VR is
`selectvr.c` in the same folder.

## See also

- [_unreversed.md](_unreversed.md) — per-stage magic constants
  (event tables, initial-alert flags), VR-disc-only stages, the
  full switch in `GM_LoadInitBin`.
- [`source/contrib/dev/`](../../../../source/contrib/dev/) —
  `MainCharacterEntries[]` (the global cross-stage table).
- [`source/overlays/`](../../../../source/overlays/) — heavyweight
  per-stage logic.
- [doc/demo/02-data-flow.md](../../demo/02-data-flow.md) — how the
  DATACNF → demo.gcx → `chara` directive flow reaches these tables.
- [doc/source/game/script.md](../game/script.md) — the GCL command
  dispatch that consults `StageCharacterEntries`.

---

## Port notes

All 88 stages are compiled statically into the port binary. The
Makefile deconflicts the per-stage `_StageCharacterEntries[]` symbol
with a `-D_StageCharacterEntries=_StageCharacterEntries_<stage>`
rename per object file (see `port/Makefile`). At runtime,
`mts_get_bss_tail()` returns the default stage's table
(`_StageCharacterEntries_select`), and the stage loader resolves the
correct table by hashing the stage name.

Three R-variant overlays — `d18ar`, `s08br`, `s19br` — are excluded
from the build because they share symbol names with their non-R
counterparts (see `port/extern_stubs.c` and
[`11-known-issues.md`](../../11-known-issues.md) item 10). Stage
R-variants in the table show up as gameplay-loadable but their
overlay-specific actors will silently fall through to the non-R
versions.

The port autoloader (`PORT_AUTOLOAD_STAGE=s00a`) bypasses the
select-screen flow and calls `GM_LoadStage` directly — useful for
debugging individual stages without navigating the menu chain.
