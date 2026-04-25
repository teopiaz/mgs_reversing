# Cookbook

Walk-throughs of real GCL patterns from the vanilla corpus. Every
snippet comes from [port/gcl/decompiled/](../../gcl/decompiled/) —
line references point at the source file.

---

## 1. The stage init boilerplate

Almost every `scenerio.gcl` starts with 4–5 shared procs. Here's the
entry sequence from [init/scenerio.gcl](../../gcl/decompiled/init/scenerio.gcl):

```gcl
proc sub_DF2A {                          # "go back to the title screen"
    eval($f:000001 = false)              # clear demo-playing flag
    load "title" -m $s:7df9 -s 1
}

proc sub_8CD4 {                          # "increment stage counter"
    eval($b:000000 = $b:000000 + 1 | 128)
}
```

The `+ 1 | 128` pattern is idiomatic — it increments the low 7 bits
and sets bit 7 as a sentinel. Appears in 100+ scripts.

```gcl
proc sub_1FED {                          # standard lighting setup
    light -d 0 -1 0                      # sun direction (pointing down)
    light -c 35 35 35                    # tinted colour
    light -a 51 51 51                    # ambient fill
}
```

---

## 2. Loading another stage with Snake's spawn coords

From [d00a/demo.gcl](../../gcl/decompiled/d00a/demo.gcl):

```gcl
proc sub_3C7F {
    eval($f:000001 = false)
    eval($w:800010 = -3952)              # Snake X
    eval($w:800012 = 104)                # Snake Y
    eval($w:800014 = -534)               # Snake Z
    eval($w:000002 = 1024)               # camera yaw
    eval($w:000004 = 0)                  # camera pitch
    load "s00a" -m $s:7df9 -s 1
}
```

The transition writes Snake's starting position into the game-state
vars (`$w:800010..14`) **before** calling `load`. The new stage's
scenerio reads those to place Snake.

---

## 3. Difficulty branching

From [init/scenerio.gcl](../../gcl/decompiled/init/scenerio.gcl):

```gcl
if ($w:800002 == 3) {
    eval($w:80007A = 1)     # difficulty = easy
} else {
    eval($w:80007A = 2)     # difficulty = normal
}

if ($f:0002CC == 1) {       # bandana flag
    eval($w:80003C = 12)    # item count bonus
}
```

`$w:800002` is the menu-picked difficulty code, `$w:80007A` stores the
active in-game difficulty, `$f:0002CC` is the "has bandana from
previous clear" flag.

---

## 4. Spawning the standard stage cast

From [s01a/scenerio.gcl](../../gcl/decompiled/s01a/scenerio.gcl),
cleaned up:

```gcl
script {
    call(sub_1FED)                              # lighting
    mapdef $s:7df9 -k $s:c681 $s:6da4 -l $s:c681 -h $s:c681 0

    chara $s:23ef $s:23ef                       # camera system
    chara $s:1465 $s:1465                       # kage (shadow) #1
    chara $s:1466 $s:1466                       # kage #2
    chara $s:1467 $s:1467                       # kage #3
    chara $s:dd6d $s:dd6d                       # door(s)
    chara $s:5df7 $s:5df7                       # item pickup
    chara $s:5df7 $s:5df7                       # another item
    chara $s:62fe $s:62fe                       # some prop

    ...
    chara $s:21ca $s:21ca                       # snake (the player!)
    ...

    pad -s                                      # hand control to player
}
```

Order matters. Snake is usually one of the last charas — after
cameras, shadows, and props are in place — so his first `act` tick
sees a fully populated world.

---

## 5. Event triggers with traps

Three common forms, from various `.gcl`:

### 5a. Zone-enter handler

```gcl
trap $s:6373 $s:21ca $s:0dd2 {       # zone 0x6373, subject snake, event "enter"
    if ($f:06006E != 1) {
        eval($f:06006E = 1)
        sound -x sd:FF00001A
    }
}
```

Fires once when Snake enters zone `$s:6373`. Typical use: play a
discovery jingle, unlock a door, spawn backup enemies.

### 5b. One-shot via `ntrap`

```gcl
ntrap $s:8f4c $s:21ca \
    -e sub_handler           # proc to run
    -b 0x10                  # only when the button mask matches
```

`ntrap` is used for pad-triggered stuff (`-b`) and other cases where
you want the binding to disappear after firing.

### 5c. Subject-agnostic broadcast

```gcl
trap $s:14c9 $s:21ca $s:0dd2 {       # $s:14c9 = "？" wildcard zone
    ...
}
```

`$s:14c9` = `GV_StrCode("？")` is the wildcard match — any zone.
Useful for "do X whenever Snake enters any zone".

---

## 6. Codec calls

From [s00a/scenerio.gcl](../../gcl/decompiled/s00a/scenerio.gcl):

```gcl
radio -c 14048 t:01010855 0      # Deepthroat, dialog 01010855, mode 0
radio -c 14085 t:01010366 0      # Campbell
radio -c 14112 t:01010377 0      # Otacon
radio -m                         # hangup
```

The first arg is a contact ID (not hashed — just a small integer that
indexes into the CODEC's contact list). The `t:` TABLE reference
points into `RADIO.DAT` at a specific dialog offset.

Contact IDs from [strcode.h](../../../source/include/strcode.h):

| ID    | Character     |
|-------|---------------|
| 14007 | Staff         |
| 14015 | Meryl         |
| 14048 | Deepthroat    |
| 14085 | Campbell      |
| 14096 | Mei Ling      |
| 14112 | Otacon        |
| 14152 | Nastasha      |
| 14180 | Master (Miller) |

---

## 7. Delayed actions

From [d00a/demo.gcl](../../gcl/decompiled/d00a/demo.gcl):

```gcl
proc sub_later {
    mesg $s:21ca $s:0e4e        # send "on" to snake
    pad -s
}

script {
    pad -r                       # disable input
    demo t:00000015              # play cutscene
    delay -t 90 -e sub_later     # 90 frames after cutscene, re-enable
}
```

`delay -t 90` waits 90 frames (~1.5 s at 60 Hz / 3 s at PSX 30 Hz)
before firing. The delayed proc runs once and the scheduler actor
destroys itself.

Negative time (`delay -t -30 ...`) is "active mode" — fires even
through cutscenes that pause normal actors.

---

## 8. A complete tiny stage scenerio

The shortest vanilla scenerio is [sound/scenerio.gcl](../../gcl/decompiled/sound/scenerio.gcl)
(sound test screen). Here it is whole with annotation:

```gcl
proc sub_DF2A {                 # go back to title
    eval($f:000001 = false)
    load "title" -m $s:7df9 -s 1
}

proc sub_8CD4 {                 # stage counter increment
    eval($b:000000 = $b:000000 + 1 | 128)
}

proc sub_1D3C {                 # the user picked "exit"
    eval($b:000049 = 7)
    call(sub_DF2A)
}

script {
    eval($s:80000E = $s:7df9)           # currentMap = "main"
    map -d $s:7df9 $s:eee9              # declare area "camera"
    map -a $s:7df9                      # activate
    chara $s:81c7 $s:81c7 \              # the DEMOSEL chara (sound test UI)
        -e sub_1D3C m"変始到音を上７き保存しますか{C03F}"
    menu -r 0                            # menu engine reset
    map -a $s:80000E                     # rebind current map
    map -s $s:80000E                     # set current
}
```

The `m"..."` is the confirmation dialog shown on the sound test screen,
asking whether to save changes before exit (approximate Japanese:
"Would you like to save settings?").

---

## 9. Restart / game-over

From various stage scripts:

```gcl
# Triggered by game-over menu:
restart           # soft restart - reload current stage from last checkpoint
```

```gcl
# Harder form, used when changing equipment or difficulty:
load "" -r 0      # empty stage name + restart flag = full re-enter
```

---

## 10. Debug scaffolding (add freely while developing)

```gcl
print "entered sub_my_handler"
print "snake is at"
eval($w:800000 = 0)                 # spurious assignment to make this non-empty
print $w:800010                     # (doesn't work — `print` only handles strings)
```

`print` writes to stdout and the port's `log.txt`. Use it liberally in
your overlays to confirm handlers fire. Not compiled out in retail —
vanilla scripts have 17 `print` sites.

---

## Compiling and testing your cookbook recipes

Drop any of the above into a stage overlay and watch it run:

```bash
# 1. Write your modified scenerio.gcl
cd port/overlays/s01a/
$EDITOR scenerio.gcl

# 2. Compile (compile.sh finds the .tail blob automatically)
cd ..
bash compile.sh

# 3. Run
cd ..
PORT_GL=1 ./mgs ./ISO/mgs.cue
```

The runtime's `[gcl-overlay] hit:` log line confirms your version is
loaded (see [gcl_overlay.c](../../gcl_overlay.c)).
