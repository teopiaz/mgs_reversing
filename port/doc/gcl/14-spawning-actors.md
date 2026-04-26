# Spawning Actors in a Stage

Practical reference for the `chara` directive — how to put Snake,
items, enemies, cameras, doors, and effects into a `scenerio.gcl`.

Every actor spawns from a single line:

```gcl
chara &TYPE $s:UNIQUE_HASH \
    <option> <args>
    <option> <args>
    ...
```

- `&TYPE` — actor type from [port/gcl_tools/mgs_names.py](../../gcl_tools/mgs_names.py)
  (e.g. `&SNAKE`, `&ITEM`, `&WATCHER`). Also writeable as a literal
  hash, e.g. `$s:21ca` for Snake.
- `$s:UNIQUE_HASH` — instance id, must be unique within the stage.
  Pick any 4-hex constant; it identifies the actor for `mesg`/`trap`
  events. Convention is to make it visually distinct from the type
  hash.
- Options are command-style (`-p`, `-d`, ...) and parsed by each
  actor's `New<Type>()` function. Tables below list what each kind
  reads.

> **TL;DR**: `chara &SNAKE` is required for the stage to be playable;
> the engine doesn't auto-spawn the player. See
> [05-stage-authoring.md §scenerio.gcl](../editor/05-stage-authoring.md)
> in the editor docs for the surrounding boilerplate (mapdef, lights,
> the HASH_MAP message).

---

## Snake (the player)

```gcl
chara &SNAKE &SNAKE \
    -p $w:800010 $w:800012 $w:800014   # spawn pos (X, Y, Z) — usually inherited
    -d 0 2048 0                         # direction (rot.x, rot.y, rot.z) in PSX byte units (4096 = full turn)
    -o $s:e91e                          # OAR (animation set) override; optional
    -m $s:7693                          # KMD model override; optional (default = stealth Snake)
chara &BREATH $s:6f8b -t 90             # cold-air breath effect; idiomatic, follows every Snake spawn
mesg &SNAKE $s:f9ad &HASH_MAIN          # HASH_MAP message wires Snake to the current MAP record
```

| Option | Argument        | Purpose                                                                 |
| ------ | --------------- | ----------------------------------------------------------------------- |
| `-p`   | X Y Z (ints)    | Spawn position. Use `$w:800010..14` to inherit from the loader.         |
| `-d`   | rx ry rz        | Initial rotation. `ry=2048` faces -Z; `0` faces +Z.                     |
| `-o`   | hash            | OAR (animation pack) override. Stage-specific motions (`s00a` uses `$s:e8fe`, `s01a` uses `$s:e91e`). Optional. |
| `-m`   | hash            | Body KMD override (e.g. wet-suit, captured). Optional — default stealth-suit Snake is fine. |
| `-f`   | flags           | Initial sna_flags1 bits. Optional.                                      |

**Don't forget the `mesg &SNAKE $s:f9ad &HASH_MAIN` line** — without
it `control->map` stays NULL and the next tick's `sna_act` faults on
the missing collision pointer. Every real playable stage has it.

Reference: [s00a/scenerio.gcl:1730](../../gcl/decompiled/s00a/scenerio.gcl#L1730),
[s01a/scenerio.gcl:2143](../../gcl/decompiled/s01a/scenerio.gcl#L2143).

---

## Items (rations, ammo, equipment)

```gcl
chara &ITEM $s:99a1 \
    -p 0 0 0          # world position (centre of plaza)
    -h 500            # bobbing height above the floor
    -b b:4            # behavior flag — 4 = standard pickable
    -i b:13           # item id (linkvar.h IT_*)
    -n 1              # how many of this item Snake gets on pickup
    -m "RATION"       # display name shown in the bottom-left corner
```

| Option | Argument | Purpose                                              |
| ------ | -------- | ---------------------------------------------------- |
| `-p`   | X Y Z    | World position.                                      |
| `-h`   | height   | Vertical bob amplitude (typical 500).                |
| `-b`   | flag     | Behavior. `b:4` is standard pickable; other values control respawn / one-shot. |
| `-i`   | id       | Item type — see [linkvar.h:215+](../../../source/include/linkvar.h#L215) for the full IT_* enum. |
| `-n`   | count    | How many to give on pickup.                          |
| `-m`   | string   | Display name when picked up.                         |

Common item ids:

| `-i` | Item              | `-i` | Item            |
| ---- | ----------------- | ---- | --------------- |
| 0    | (none)            | 8    | `IT_BodyArmor`  |
| 1    | `IT_Beretta`      | 9    | `IT_Bandage`    |
| 2    | `IT_FAMAS`        | 13   | `IT_Ration`     |
| 3    | `IT_GrenadeM67`   | 14   | `IT_Diazepam`   |
| 4    | `IT_C4`           | 15   | `IT_Goggles`    |
| 5    | `IT_Claymore`     | 16   | `IT_Card1`      |
| 6    | `IT_Stinger`      | …    | (see linkvar.h) |
| 7    | `IT_NikitaMissile`|      |                 |

Reference: [s00a/scenerio.gcl](../../gcl/decompiled/s00a/scenerio.gcl) (multiple `chara &ITEM $s:eeba` rations).

---

## Enemies (WATCHER patrol guards, COMMANDER coordinator)

A patrolling guard:

```gcl
chara &WATCHER $s:1466 \
    -r b:17                # route ID (matches an HZD_PAT route in the .hzd file)
    -b 'P'                 # behavior: 'P' = patrol, 'S' = stand
    -a 'S'                 # alert response: 'S' = standard
    -f 7                   # flags
    -l 192                 # vision-cone length (PSX units, ~256 = ~25m)
    -n 0 30000 0           # spawn pos (X Y Z) — Y=30000 is "deep underground", a sentinel meaning "use route point 0"
```

The COMMANDER is one per stage and spawns the watchers + cameras +
searchlights (acting as their owner for messaging):

```gcl
chara &COMMANDER $s:23ef \
    -n sub_7ECB sub_7ECC sub_7ECD       # watcher think-procs (one per route)
    -s sub_A22F sub_A230                # searchlight procs
    -c sub_8B69 sub_8B6A sub_8B6B       # camera procs
    -v <vec list>                       # waypoints
    -f b:0
```

Watchers/cameras/lights are usually defined inside their own `proc`
blocks, then those procs are passed by symbol to the commander
(`-n sub_7ECB ...`). The commander invokes them at level start.

Reference: [s01a/scenerio.gcl:1907+](../../gcl/decompiled/s01a/scenerio.gcl#L1907),
[s01a/scenerio.gcl:512](../../gcl/decompiled/s01a/scenerio.gcl#L512).

---

## Cameras (surveillance / behind-mode)

```gcl
chara &CAMERA $s:dd6d \
    -l 3800              # cone length / view distance
    -w 300               # cone half-width angle (units of 4096/turn)
    -x 225               # arc speed when sweeping
    -p -16000 2500 4000  # camera position (mounted high)
    -d 256 3840 0        # direction (default look)
    -h b:1               # height variant
    -e sub_9FC3          # event proc (alert handler)
```

Each `chara &CAMERA` is typically wrapped in its own `proc sub_NNNN`
so the COMMANDER can spawn it lazily.

Reference: [s01a/scenerio.gcl:566+](../../gcl/decompiled/s01a/scenerio.gcl#L566).

---

## Searchlights

```gcl
chara &SEARCHLIGHT $s:5df7 \
    -i b:1               # instance index (1, 2, ... within stage)
    -h 1100              # mount height
    -x 250               # sweep speed
    -w 1700              # sweep arc
    -d 306 3010 0        # initial direction
    -p 14140 7780 7940   # mount position
    -a 185               # alert volume / intensity
    -t 306 306 175 3     # patrol angles + dwell time
```

Reference: [s01a/scenerio.gcl:539+](../../gcl/decompiled/s01a/scenerio.gcl#L539).

---

## Doors

```gcl
chara &DOOR $s:59e3 \
    -p 500 0 2250        # position (centre of doorway, on the floor)
    -d 0 0 0             # rotation
    -m $s:6e28           # KMD hash for the door visual
    -t b:2               # type / animation style
    -w 1500              # door slide width
    -r -2000             # slide direction (signed)
```

`&DOOR` is the standard sliding door; `&DOOR2` and `&M_DOOR` are
variants (single-leaf, manual). The KMD assigned to `-m` is loaded
from the disc cache — for custom stages you'll need to author or
reuse a door KMD and ensure it's resident.

Reference: [d11c/scenerio.gcl](../../gcl/decompiled/d11c/scenerio.gcl)
(slide doors), [d03a/scenerio.gcl](../../gcl/decompiled/d03a/scenerio.gcl)
(double doors).

---

## Environmental effects

```gcl
chara &SNOW $s:4692 \
    -l -5000 0 -5000     # bbox lower bound
    -h 5000 5000 5000    # bbox half-extents
    -s 50 -20 0          # base wind vector
    -w 50 -20 -50        # wind variance
    -n 256               # particle count

chara &FADEIO $s:62fe              # fade-in/out (cutscene transitions)
chara &SMOKE $s:bb75 -6000 700 1468 # localised smoke effect
chara &CINEMA $s:6eca -t 30000     # cinema mode (cutscene camera)
```

---

## Common pattern: stage init order

Real playable stages put things in this order (anything else and
something downstream NULL-derefs):

```gcl
script {
    call(sub_LIGHTING)                # `light -d/-c/-a` setup
    mapdef &HASH_MAIN -k -l -h -z 0   # load the map's KMD/HZD/lit/zone caches
    map -b R G B                       # background colour
    map -a &HASH_MAIN                  # bind the map (sets gBinds_800ABA60)
    map -s &HASH_MAIN                  # show the map (triggers GM_UpdateMapGroup)

    chara &SNAKE &SNAKE -p ... -d ...  # the player
    chara &BREATH $s:6f8b -t 90        # cold-air breath
    mesg  &SNAKE $s:f9ad &HASH_MAIN    # HASH_MAP — links Snake.control.map

    # Now everything else: items, enemies, doors, effects, ...
    chara &ITEM $s:99a1 -p ... -i b:13 -n 1 -m "RATION"
    chara &WATCHER $s:1466 -r b:17 ...
    chara &CAMERA $s:dd6d ...
    chara &DOOR $s:59e3 ...
    chara &SNOW $s:4692 ...
}
```

If you skip `map -s` the soliton radar comes up blank
(`HZD_CurrentGroup` never gets set); if you skip the HASH_MAP `mesg`
Snake faults on his first tick. Both lessons learnt the hard way
implementing `s99a` — see git log for the breadcrumbs.

---

## Where actor types are registered

`&SNAKE`, `&ITEM`, `&DOOR` are in
[`source/main/main.c`](../../../source/main/main.c) →
`MainCharacterEntries[]` — globally registered, available in every
stage.

Other actors (`&WATCHER`, `&CAMERA`, `&SEARCHLIGHT`, `&BREATH`,
`&SNOW`, `&FADEIO`, ...) are loaded by **per-stage overlay**: a
`.gcx` snippet under [port/overlays/<stage>/](../../overlays/) plus
the `overlay2.o` (or `.o`) binary that registers their `New<Type>()`
factories in `gBinds_800ABA60`. If you `chara &WATCHER` from a
custom stage that has no overlay registering watchers, you'll see
`[gcl] chara: func not found (hash=0x6e9a)` in the log and the call
silently no-ops. Custom stages currently get `&SNAKE`/`&ITEM`/`&DOOR`
for free; everything else needs additional engine work.

Cross-references:

- [04-commands.md](04-commands.md) — full `chara` command grammar.
- [08-known-ids.md](08-known-ids.md) — symbolic name → hash table.
- [12-mesg-protocol.md](12-mesg-protocol.md) — sending messages to
  spawned actors after they exist (e.g. open a door, kill an actor).
- [05-cookbook.md](05-cookbook.md) — longer-form patterns from the
  vanilla corpus.
