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

A "blank" cutscene needs three things: a fade-in, a CINEMA lifetime
timer, and (optionally) a camera animator. Custom cutscenes have no
built-in camera-animation primitive — the disc relied on baked `.dmo`
records (which need an offline pipeline we don't have) or per-stage
overlay actors compiled into C. For a custom GCL-only demo your
options are: (a) leave the camera static and let it inherit whatever
the gameplay scene framed, or (b) use the `camera` GCL command to
configure a fixed framing via the engine's HZD camera system.

Drop this into `port/editor/assets/<stage>/demo.gcl`:

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

    # Optional water effect — only if the scene shows water
    # chara &WT_VIEW $s:DDD3 \
    #     -b -3000 -3000 -3000 3000 3000 3000
    #     -c b:80 b:80 b:120

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

### Camera framing

Custom GCL has no built-in animated cutscene camera (see "Status"
above). Practical approaches:

**Static fixed framing via the `camera` GCL command.** This
configures one of the engine's HZD camera slots (`GM_CameraList[N]`,
N = 0..7) and switches to it. The set is identical to in-game zone
cameras — useful for a single shot that holds for the whole demo.

```gcl
camera \
    -s 4                                  # slot 4
    d:CAM_FIX d:CAM_INTERP_LINER d:CAM_CAM_TO_TRG \
    -3362,1759,4936                       # eye  position
    -2475,770, 6672                       # target
    1                                     # enable
```

**Multi-shot via multiple slots + delay.** Pre-configure two slots,
then switch between them mid-script.

```gcl
script {
    camera -s 4 d:CAM_FIX d:CAM_INTERP_LINER d:CAM_CAM_TO_TRG \
           -3362,1759,4936 -2475,770,6672 1
    camera -s 5 d:CAM_FIX d:CAM_INTERP_LINER d:CAM_CAM_TO_TRG \
            1500,1759,4936  -200,770,6672  1
    camera -t 4                            # track slot 4
    delay 240
    camera -t 5                            # cut to slot 5
    delay 360
}
```

**Animated framing.** Currently requires a stage-specific overlay
actor written in C (see [04-camera-pipeline.md](04-camera-pipeline.md)
for examples — `democame.c`, `11g_demo.c`, `intr_cam.c`). Each
writes `gUnkCameraStruct2_800B7868.eye/center` per tick along a
hand-coded path. There's no GCL-only equivalent. Re-using one of
these overlays from a custom stage means linking that overlay's
object file into the editor + live game build.

Disc dramatic cinematics avoid this problem entirely by going
*streamed*: an offline pipeline produces a `.dmo` file that
`demothrd.c::FrameRunDemo` plays back from `DEMO.DAT` per frame.
That pipeline is not yet ported (see
[09-streamed-demos.md](09-streamed-demos.md)).

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

Expected for most GCL-only custom demos — there's no built-in
cutscene-camera animator (see "Camera framing" above for what *is*
available). If you wanted motion you need either the `camera`
GCL-command multi-slot trick, an overlay actor, or to wait for the
streamed-demo pipeline to port. WT_VIEW is **not** the answer
despite the name; it's a water visual effect.

See [04-camera-pipeline.md](04-camera-pipeline.md) for the actual
camera chain.

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
| `d00a` | Opening helipad fly-by — minimal | Single CINEMA, baked camera via `demo -f "s0102a0.dmo"`, lots of EMITTER snow |
| `d01a` | Codec briefing — multi-character | `RADIO` + `JIMAKU` + 2 DEMODOLLs |
| `d18a` | Bunsin demo — complex | Multiple DEMODOLLs, mid-scene proc calls |
| `d11c` | Door + door open animation | Demonstrates `DOOR` actor in cutscene context |

Decompiled text source for all of these lives at
`port/gcl/decompiled/<stage>/demo.gcl`.
