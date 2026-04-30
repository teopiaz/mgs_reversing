# `source/stage/` — per-stage overlays

One source file per playable stage. Each holds the
**`StageCharacterEntries[]` table** for that stage — the
chara hash → factory mapping that's used during stage-private
script execution.

## File pattern

Every file looks like:

```c
const NEWCHARA_NAME _StageCharacterEntries_<stage>[] = {
    { CHARA_<TYPE_1>, NewType1, NewType1Init },
    { CHARA_<TYPE_2>, NewType2, NewType2Init },
    /* ...stage-private chara types... */
    { 0, NULL, NULL }   /* terminator */
};
```

The list registers actors that are *only* spawnable in this
stage. Cross-stage actors (DEMODOLL, CINEMA, FADEIO, etc.) live
in `MainCharacterEntries[]` (in `source/contrib/dev/`).

## Stage list

(35+ stage files; selected highlights — see file listing for the
full set.)

| File | Stage | Description |
| ---- | ----- | ----------- |
| `s00a.c` | s00a | Game opening / intro stage |
| `s01a.c` | s01a | Helipad / outdoor area |
| `s02a–s02e.c` | s02 | Tank-hangar levels |
| `s03a–s03e.c` | s03 | Cells / DARPA Chief / torture |
| `s04a–s04c.c` | s04 | Comm tower / Ocelot encounter |
| `s05a.c` | s05 | Communications |
| `s06a.c` | s06 | Misc inner area |
| `s07a–s07c.c` | s07 | Canyon (M1A1 boss) |
| `s08a–s08c.c` | s08 | Sniper Wolf encounter |
| `s09a.c` | s09 | Post-Wolf canyon walk |
| `s10a.c` | s10 | Underground complex |
| `s11a–s11i.c` | s11 | Hind hangar + escape |
| `s12a–s12c.c` | s12 | Wolves / Sniper Wolf rematch |
| `s13a.c` | s13 | Late-game prep |
| `s15a–s15c.c` | s15 | Vulcan Raven |
| `s16a–s16d.c` | s16 | Underground tank |
| `s17a–s17ar.c` | s17 | Final boss approach |
| `s18a / s18ar.c` | s18 | Metal Gear cockpit |
| `s19a / s19ar / s19b / s19br.c` | s19 | Jeep escape |
| `s20a / s20ar.c` | s20 | Final escape / ending |
| `d00a.c, d01a.c, d03a.c, d11c.c, d16e.c, d18a.c, d18ar.c` | d* | Cinematic-only stages (cutscenes between gameplay) |
| `abst.c, brf.c` | abst / brf | Abstract / briefing scenes |
| `change.c` | change | Stage-change dispatcher |
| `roll.c, ending.c, endingr.c` | roll / ending | Credits + ending |
| `select.c, select1..4.c, selectd.c` | select | Menu / stage-select screens |
| `option.c` | option | Option menu wrapper |
| `preope.c, demosel.c, rank.c, title.c` | various | Pre-game UI |
| `camera.c, sound.c` | camera/sound | Special-purpose stage stubs |

The "r" suffix is the **REDUX** variant — the harder-difficulty
or boss-rematch alternate. The "VR" disc has its own VR stages
in `source/stagevr/`.

## How stages are registered

`gamed.c::GM_LoadInitBin` (or related boot code) reads
`GM_CurrentMap` (set during stage load) and switches on the
hash to select the right `_StageCharacterEntries_<stage>[]`:

```c
case 0xC693: StageCharacterEntries = &_StageCharacterEntries_d00a; break;
case 0x46BB: StageCharacterEntries = &_StageCharacterEntries_s01a; break;
/* …35 more cases… */
```

(See `gamed.c:783-820` for the full switch.)

## What each stage file contains

Beyond the chara table, some stage files include:

- A small `Stage<Name>_Init()` function called once on entry.
- A small `Stage<Name>_Tick()` for stage-private per-frame logic.
- Constants tables seeded into `StageWork` (HZD bucket counts,
  initial alert state).

Stages with substantial gameplay scripting (s11g hind chase,
s07c boss) link to per-stage *overlay* code in
`source/overlays/<stage>/` for the actual game logic. The
`stage/<stage>.c` file is the *registration* hook; the
`overlays/<stage>/` folder is the *implementation*.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (per-stage actor
  variants, magic constants, per-stage GCL).
- [`source/overlays/`](../../../../source/overlays/) —
  stage-specific overlay code (much larger than the stub here).
- [`source/contrib/dev/`](../../../../source/contrib/dev/) —
  `MainCharacterEntries[]` (the global / cross-stage chara
  table).
- [doc/demo/02-data-flow.md](../../demo/02-data-flow.md) — how
  GCL scripts in stage DATACNFs reach this code.
