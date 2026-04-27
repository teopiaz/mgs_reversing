# Authoring a custom cutscene

Custom stages built via `tools/import_stage.py` can ship a cutscene
the same way disc stages do — by including a `demo.gcl` next to the
mandatory `scenerio.gcl` in the asset directory. Both compile through
the same toolchain
([`tools/mgs_tools/gcl/`](../../../tools/mgs_tools/gcl/)) and land in
the resulting DATACNF as separate `'g'`-tagged entries.

This file is a hands-on author's guide — minimum viable demo, common
patterns, and the gotchas that show up in practice.

## Status

The custom-cutscene path is **partially wired** as of this writing:

- ✅ The compile path exists. `tools/mgs_tools/gcl/compile.py` round-trips
  any disc demo byte-exact, so authoring a custom one in the same
  syntax works.
- ✅ The runtime path exists. The editor's Demo Player and the live
  game both honour `demo.gcx` for any stage that has it.
- ⚠ The importer currently only emits `scenerio.gcx`.
  `tools/mgs_tools/stage/gcx_writer.py` would need a sibling call
  to produce a second `'g'` entry for `demo.gcl`. Until that lands,
  custom-cutscene authors will need to call `tools/gcl2gcx.py` by
  hand and stash the result.

The rest of this file is forward-looking: it describes the workflow
once the importer side catches up.

## Minimum viable demo

A "blank" cutscene needs three things: a fade-in, a camera
animator, and a CINEMA timer. Drop this into
`port/editor/assets/<stage>/demo.gcl`:

```gcl
# Stage hash 0xNNNN = gv_strcode("<stage>"); replace below.

proc sub_DDD1 {
    light \
        -d 0 -1 0
    light \
        -c 60 60 60
    light \
        -a 40 40 50
}

script {
    call(sub_DDD1)
    mapdef &HASH_MAIN \
        -k $s:NNNN
        -l $s:NNNN
        -h $s:NNNN 0
        -z 0
    map -a &HASH_MAIN
    chara &SNAKE &SNAKE \
        -p 0 0 0
        -d 0 2048 0
    mesg &SNAKE $s:f9ad &HASH_MAIN

    # 5-second fade-in from black
    chara &FADEIO $s:DDD2 \
        -t 60 240          # 60 ticks of black, 240 ticks of fade
        -c 0 0 0

    # Camera that watches a 6000-unit cube around the origin
    chara $s:8e45 $s:DDD3 \
        -b -3000 -3000 -3000 3000 3000 3000
        -c b:80 b:80 b:120

    # Cutscene runs for ~17 seconds at 60Hz
    chara &CINEMA $s:DDD4 \
        -t 1024
}
```

Then:

```bash
# manual until import_stage emits demo.gcx automatically
python3 tools/gcl2gcx.py port/editor/assets/<stage>/demo.gcl \
        -o /tmp/demo.gcx --no-align4
# (drop /tmp/demo.gcx into the stage's DATACNF as a 'g' entry —
#  see tools/mgs_tools/stage/datacnf_writer.py for the layout)
```

In the editor, open the stage and hit **Play** on the Demo tab.

## Common authoring patterns

### Slow camera pan

`WT_VIEW`'s motion is driven by its bound box and an internal animation
table that interpolates between the corners. To get a slow drift,
make the bound box wider on the axis you want movement on:

```gcl
chara $s:8e45 $s:CAM1 \
    -b -8000 0 -2000  8000 0 2000      # 16000 units of pan along X
    -c b:80 b:80 b:120
```

The `-c` colour is post-processed onto the framing — usually
deliberately desaturated (`b:80` ≈ 50% brightness) to suggest
"cinematic" without recolouring the scene.

### Multiple shots in one cutscene

The original game does multiple shots within a cutscene by spawning
multiple `WT_VIEW` actors and using `delay` + `mesg` to enable /
disable them at different times. There's no clean "Director" idiom —
just imperative messaging:

```gcl
proc sub_CUT2 {
    mesg $s:CAM1 $s:DEACT b:0      # turn off shot A
    mesg $s:CAM2 $s:ACT   b:0      # turn on shot B
}

script {
    chara $s:8e45 $s:CAM1 -b … -c …
    chara $s:8e45 $s:CAM2 -b … -c …
    delay 240
    call(sub_CUT2)
    …
}
```

Disc demos are mostly single-WT_VIEW; the multi-shot pattern is
a custom-stage convention.

### A character walks across the room

Spawn a `DEMODOLL` with the character's KMD and an animation hash:

```gcl
chara &DEMODOLL $s:CHR1 \
    -p -2000 0 0          # spawn position
    -m $s:6BD4            # KMD hash (snake / meryl / guard …)
    -r b:128              # facing — 128 = +X (90° yaw)
    -a 0                  # animation index (0 = walk-loop typically)
    -s 60                 # play rate (60 ticks → one full cycle)
```

Animation hashes vary per character. Disc-extracted DEMODOLLs use
hashes registered by `tools/extract_disc.py` — for custom characters
you'd need to bundle the KMD + animation data the same way
`box_NN.kmd` is bundled today.

### Codec call

```gcl
chara &RADIO $s:RAD1 \
    -t sd:01010001        # voice/text id
```

The radio system reads dialogue from `RADIO.DAT` on the disc. Voices
are streamed VOX. Custom dialogue would require authoring entries
into a custom `RADIO.DAT` — not yet supported by the import
pipeline.

### Subtitle pop-up

```gcl
chara &JIMAKU $s:SUB1 \
    -t 180                # 3 seconds at 60Hz
    -m "Snake."           # short ASCII strings work; longer needs
                          # m"…" with stage-font glyph table
```

Stage-font glyphs (Japanese kanji) are encoded as `{XXXX}` markers
inside `m"…"` strings. See
[`tools/mgs_tools/gcl/README.md`](../../../tools/mgs_tools/gcl/README.md)
for the encoding convention.

## Gotchas in practice

### "Camera never moves"

Almost always: the WT_VIEW chara hash (`0x8E45`) is missing from
`MainCharacterEntries[]` in `port/extern_stubs.c`. Already fixed for
the editor + live game in this repo, but worth checking if you
forked an older snapshot.

See [04-camera-pipeline.md](04-camera-pipeline.md) for the camera
chain; if you're hitting "frozen view" *with* WT_VIEW registered,
that doc lists the four-step diagnosis.

### "Cutscene runs forever / engine doesn't return to gameplay"

`CINEMA -t N` is the lifetime timer. When CINEMA's `Act` sees
`GV_Clock` exceed `spawn_tick + N`, it self-destructs and *typically*
its destructor signals the `load` of the next stage. If your cutscene
is supposed to drop into gameplay, end with an explicit `load`:

```gcl
proc sub_END {
    eval($f:000001 = b:0)         # clear "in-cutscene" flag
    load "<next-stage>" -m &HASH_MAIN -s b:1
}

script {
    …
    chara &CINEMA $s:CIN1 -t 600
    delay 600                      # wait for CINEMA to expire
    call(sub_END)
}
```

### "Things spawn invisibly"

Double-check the chara has a position option (`-p X Y Z` for most
actors, `-n X Y Z` for some — see the chara handler's
`GCL_GetOption('p')` vs `('n')`). An actor with no pos is usually
non-spatial (CINEMA, FADEIO) and won't render anyway.

### "Engine crashes on first tick"

Look for `chara: func not found (hash=0xNNNN)` in stderr — that's a
benign "skip", not a crash. A real crash is a dereference inside an
actor's `Act` callback; the editor's `GV_ExecActorSystem` is wrapped
in a `SIGSEGV` handler so the editor stays alive even if one actor
faults. The crashing actor's name shows up in the next
`GV_DumpActorSystem` output (the file the actor was registered from).

### "My demo plays but Snake / Meryl / guard models don't appear"

The DEMODOLL's `-m $s:HHHH` references a KMD. The KMD must be in
the stage's DAR (or bundled by `tools/import_stage.py`). For disc
characters use the disc's hash (`gv_strcode("snk")`,
`gv_strcode("mer")`, etc.); for custom characters bundle the KMD
the same way `box_NN.kmd` and `door_dd.kmd` are bundled.

## Reference disc cutscenes

Worth reading the source of these to learn idioms:

| Stage | What's good about it | Notable techniques |
| ----- | -------------------- | ------------------ |
| `d00a` | Opening helipad fly-by — minimal | Single CINEMA, single WT_VIEW, lots of EMITTER snow |
| `d01a` | Codec briefing — multi-character | `RADIO` + `JIMAKU` + 2 DEMODOLLs |
| `d18a` | Bunsin demo — complex | Multiple DEMODOLLs, mid-scene proc calls |
| `d11c` | Door + door open animation | Demonstrates `DOOR` actor in cutscene context |

Decompiled text source for all of these lives at
`port/gcl/decompiled/<stage>/demo.gcl`.
