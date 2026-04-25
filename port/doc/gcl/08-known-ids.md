# Known IDs Catalog

A reverse-lookup table for the hashed names you'll keep seeing in
scenerio scripts. All hashes are `GV_StrCode()` of a UTF-8 string —
see [source/include/strcode.h](../../../source/include/strcode.h)
for the canonical list.

> **Tip:** to compute a hash yourself, use the helper:
> `python3 port/gcl_tools/constants.py` (the `gv_strcode()` function),
> or grep `port/gcl_tools/mgs_chars.py` for the rotate-left algorithm.

## Using `&NAME` symbols in source

Instead of `$s:21ca` everywhere, the parser accepts a `&NAME` form
that resolves through [mgs_names.py](../../gcl_tools/mgs_names.py):

```gcl
chara &SNAKE &SNAKE                       # was: $s:21ca $s:21ca
mesg arg1 &HASH_ON                        # was: $s:0e4e
trap $s:c776 &SNAKE &HASH_ENTER { ... }   # was: ... $s:21ca $s:0dd2
load "s01a" -m &HASH_MAIN -s 1            # was: -m $s:7df9
```

`&NAME` and `$s:XXXX` compile to byte-identical bytecode (a 2-byte
`STR_ID` literal). Use whichever you find clearer per call site.

To **decompile** with names, pass `--names`:

```bash
python3 gcx2gcl.py disc.gcx -o file.gcl --trailing file.tail --names
```

Without `--names`, `$s:XXXX` is emitted (default — identical to
pre-existing behaviour, fully round-trip safe).

Unknown names raise a parse error rather than silently falling back to
`gv_strcode(NAME)` — that prevents typos from compiling to a different
hash. To register a new name, add it to
[mgs_names.py](../../gcl_tools/mgs_names.py).

---

## Stages (`load "..."` arg / map area names)

The most-used stage IDs (`STAGE_*` in strcode.h):

| Hash      | Name      | What it is                                   |
|-----------|-----------|----------------------------------------------|
| `0x45ca`  | `init`    | Initial boot stage                           |
| `0x655b`  | `title`   | Title screen                                 |
| `0x8d5c`  | `select`  | Stage / save select                          |
| `0x9265`  | `rank`    | Ranking screen                               |
| `0x469b`  | `s00a`    | Heliport (intro)                             |
| `0x46bb`  | `s01a`    | Tank hangar                                  |
| `0x46db..0x46df` | `s02a..s02e` | Cell block / Holding cells              |
| `0x46fb..0x46ff` | `s03a..s03e` | Vents / Armory tunnels                  |
| `0x471b..0x471d` | `s04a..s04c` | Canyon                                  |
| `0x473b`  | `s05a`    | Nuke building 1F                             |
| `0x475b`  | `s06a`    | Nuke building B1                             |
| `0x477b..0x477d` | `s07a..s07c` | Nuke building B2                        |
| `0x479b..0x479d` | `s08a..s08c` | Commander's room                        |
| `0x47bb`  | `s09a`    | Cargo elevator                               |
| `0x4a9b`  | `s10a`    | Cave                                         |
| `0x4abb..0x4ac3` | `s11a..s11i` | Underground base                        |
| `0x4adb..0x4add` | `s12a..s12c` | Communication tower A                   |
| `0x4afb`  | `s13a`    | Communication tower B (rooftop)              |
| `0x4b1f`  | `s14e`    | Snowfield (after stinger)                    |
| `0x4b3b..0x4b3d` | `s15a..s15c` | Blast furnace                           |
| `0x4b5b..0x4b5e` | `s16a..s16d` | Cargo dock                              |
| `0x4b7b`  | `s17a`    | Underground passage                          |
| `0x4b9b`  | `s18a`    | Warehouse north                              |
| `0x4bbb..0x4bbc` | `s19a..s19b` | Underground 2                           |
| `0x4e9b`  | `s20a`    | Final escape                                 |
| `0xc693..0xcb93` | `d00a..d18a` | Cutscene-stage variants                 |
| `0x655b`  | `title`   | Title screen                                 |
| `0x833b`  | `ending`  | Ending sequence                              |

`r`-suffix variants (`s10ar`, `s17ar`, `s18ar`, `s19br` …) are RED
versions used for replay / rescue paths.

---

## Common map IDs (`mapdef <id>`, `map -d <id>`, etc.)

| Hash      | Name        | Usage                                    |
|-----------|-------------|------------------------------------------|
| `0x7df9`  | `メイン` (main) | the main / playable area for the stage |
| `0xeee9`  | `camera`    | camera-only area                         |
| `0xdc55`  | `asiato`    | footprint area                           |
| `0xca85`  | `pool`      | water pool zone                          |
| `0x7833`  | `poolato`   | water-trace zone                         |

---

## Character IDs (`chara <type> <name>`)

The most-spawned actors across all 135 vanilla scenerios. Full list
(220+ entries) in [strcode.h](../../../source/include/strcode.h#L168).

### Players & humanoids

| Hash      | Name (CHARAID_…)      | What                              |
|-----------|------------------------|-----------------------------------|
| `0x21ca`  | `SNAKE` (`スネーク`)    | the player                        |
| `0xa608`  | `ZAKO11A` (`ざこ１１ａ`) | guard variant A                  |
| `0xa60c`  | `ZAKO11E`              | guard variant E                   |
| `0xa60d`  | `ZAKO11F`              | guard variant F                   |
| `0xed87`  | `ZAKO` (`ざこ`)         | generic enemy                     |
| `0x7cf7`  | `ZAKOCOM`              | enemy commander                   |
| `0x6e9a`  | `WATCHER` (`巡回兵`)    | patrolling guard                  |
| `0xc6d7`  | `COMMANDER` (`コマンダー`) | named commander                |
| `0x30ba`  | `NINJA` (`忍者`)        | Cyborg Ninja                      |
| `0x962c`  | `WOLF2` (`ウルフ`)      | Sniper Wolf                       |
| `0xa76f`  | `PSYCHOMANTIS`         | Psycho Mantis                     |
| `0xcb1f`  | `GODZILA` (`ゴジラ`)    | Metal Gear REX (codename)         |
| `0x7bf2`  | `LIQUID` (`リキッド`)   | Liquid Snake                      |
| `0x1ef9`  | `JOHNNY` (`ジョニー`)   | Johnny Sasaki                     |
| `0x1158`  | `BLOODY_MERYL`         | wounded Meryl                     |
| `0xf4b0`  | `PSYCHOMERYL`          | Mantis-puppet Meryl               |
| `0x4754`  | `MGREX`                | actual Metal Gear (boss)          |

### Environment / stage props

| Hash      | Name                   | What                               |
|-----------|------------------------|------------------------------------|
| `0x117c`  | `KAGE`                 | shadow under actors                |
| `0x18e3`  | `SNOW` (`雪`)           | falling snow                       |
| `0x1a02`  | `BUBBLE` (`泡`)         | water bubbles                      |
| `0x170c`  | `SMOKE` (`煙`)          | smoke effect                       |
| `0xb58d`  | `DRUMCAN` (`ドラム缶`)  | barrel                             |
| `0x4be8`  | `DRUMCAN2` (`ドラム缶２`)| barrel variant                    |
| `0xb997`  | `DOOR` (`ドア`)         | door                               |
| `0x73f8`  | `DOOR2` (`ドア２`)      | door variant                       |
| `0xb98c`  | `M_DOOR`               | M-door                             |
| `0x425f`  | `LIFT` (`リフト`)       | elevator                           |
| `0x921b`  | `LIFT2` (`リフト２`)    | elevator variant                   |
| `0x2abc`  | `ELEVATOR` (`エレベータ`)| elevator (different)              |
| `0x788d`  | `CHAIR` (`椅子`)        | chair                              |
| `0xcc45`  | `CONTAINER` (`コンテナ`)| shipping container                 |
| `0xa3fb`  | `CRANE` (`クレーン`)    | crane                              |
| `0x8e70`  | `GLASS` (`ガラス`)      | breakable glass                    |
| `0xec77`  | `WALL` (`障害物`)       | obstacle wall                      |
| `0x9d00`  | `DMYFLOOR` (`落し穴`)   | invisible pit / drop trigger       |
| `0x58f0`  | `DMYWALL` (`塗り壁`)    | invisible wall                     |
| `0xb103`  | `DYNWALL` (`透明壁`)    | dynamic invisible wall             |
| `0xaf6c`  | `DYNFLOOR` (`透明床`)   | dynamic invisible floor            |

### Items / weapons / pickups

| Hash      | Name             | What                                  |
|-----------|------------------|---------------------------------------|
| `0x8767`  | `ITEM` (`アイテム`)| generic item pickup (max-spawned chara — 776 sites!) |
| `0x917b`  | `ITEM_DOT`       | item indicator                        |
| `0x5d43`  | (`HASH_ITEM`)    | item event tag                        |
| `0xc6ac`  | `KEY_ITEM`       | unique key item                       |
| `0x3c0c`  | `CLAYMORE` (`クレイモア地雷`) | claymore mine               |

### Camera / cinematic

| Hash      | Name             | What                                  |
|-----------|------------------|---------------------------------------|
| `0x6e90`  | `CAMERA` (`カメラ`)| primary camera actor                 |
| `0x56cc`  | `CAMERA2` (`カメラ２`)| secondary camera                  |
| `0x7bc2`  | `CAMERA_SHAKE`   | screen shake                           |
| `0xe97e`  | `DEMODOLL` (`デモ人形`)| cutscene puppet (silhouette actor) |
| `0xb4e6`  | `DEMOCANCEL`     | "press X to skip" prompt              |
| `0x7a05`  | `CINEMA` (`シネマスクリーン`)| letterbox bars               |
| `0x3686`  | `DEMOSEL` (`デモ劇場`)| demo selection / soundtest UI    |

### UI / menus / system

| Hash      | Name             | What                                  |
|-----------|------------------|---------------------------------------|
| `0x6d78`  | `MONITOR1` (`モニタ１`)| monitor display                  |
| `0x9f7d`  | `DISPLAY`        | text display                          |
| `0x7ff7`  | `TELOP` (`テロップ`)| ticker / opening crawl               |
| `0xa5dc`  | `PAUSE_MENU`     | pause menu                            |
| `0x9302`  | `SAVE_DATA` (`セーブデータ`)| save-data screen           |
| `0x53c7`  | `LOAD_DATA` (`ロードデータ`)| load-data screen           |
| `0xc5b7`  | `SAVEMANAGER`    | save/load orchestrator                |
| `0xfed1`  | `PADVIBRATE` (`パッド振動`)| controller rumble            |
| `0xcbf8`  | `PADCONTROL` (`パッドコントロール`)| input mapping        |

### Effects / particles

| Hash      | Name             | What                                  |
|-----------|------------------|---------------------------------------|
| `0xa12e`  | `FADEIO`         | fade in / fade out                    |
| `0x32e5`  | `EMITTER`        | particle emitter                      |
| `0xa9dd`  | `EMITTER2`       | particle emitter v2                   |
| `0x600d`  | `SCN_BOMB`       | scenery explosion                     |
| `0xc73e`  | `FALL_SPLASH`    | water splash on fall                  |
| `0x4e95`  | `BLOOD_CL` (`血溜り`)| blood pool                          |
| `0x6a4c`  | `BLOOD_BL` (`血溜り２`)| blood pool variant                |

### Stage-specific

| Hash      | Name             | What                                  |
|-----------|------------------|---------------------------------------|
| `0x6c66`  | `VIB_EDIT`       | vibration setup screen                |
| `0xd4a5`  | `MOUSE`          | rat                                   |
| `0xae06`  | `NOBU_WINMNGR`   | window-manager (Nobu Akimoto's debug) |
| `0x3d26`  | `KOBA_WINMNGR`   | window-manager (Kobayashi's debug)    |

---

## Sound / SD codes

`sd:XXXXXXXX` opcodes are 32-bit. The bit layout is loosely:

```
0xCCSSWWPP    CC = category, SS = subcategory,
              WW = waveform, PP = pitch
```

There's no master table — pull samples from real scripts and tweak.
A few common ones:

| SD code        | Used for                                 |
|----------------|------------------------------------------|
| `sd:01010001`  | door open                                |
| `sd:01010030`  | guard alert beep                         |
| `sd:01010855`  | radio incoming-call ring                 |
| `sd:01FFFF0A`  | menu confirm                             |
| `sd:FF000007`  | item pickup                              |
| `sd:FF00001A`  | discovery jingle                         |

---

## Radio contact IDs (decimal)

Used as the first arg of `radio -c <contact> ...`. NOT hashed — small
integers indexing into the codec contact list:

| ID    | Character                                 |
|-------|-------------------------------------------|
| 14007 | Staff                                     |
| 14015 | Meryl                                     |
| 14048 | Deepthroat                                |
| 14085 | Campbell (commanding officer)             |
| 14096 | Mei Ling (radar / save)                   |
| 14112 | Otacon                                    |
| 14152 | Nastasha (weapons)                        |
| 14180 | Master (Master Miller)                    |

---

## Computing your own hashes

`GV_StrCode()` is a 16-bit rolling hash; identical to:

```python
def gv_strcode(s):
    h = 0
    for b in s.encode("utf-8"):
        h = (((h << 5) | (h >> 11)) + b) & 0xFFFF
    return h
```

Examples:

```
gv_strcode("snake")    = 0x992d   # the KMD model name (different from CHARAID!)
gv_strcode("スネーク") = 0x21ca   # CHARAID_SNAKE (the actor)
gv_strcode("main")     = 0xc8bb   # but Konami used 0x7df9 = "メイン"
gv_strcode("メイン")   = 0x7df9   # which is the canonical "main map" id
```

> **Footgun**: many Konami names are Japanese, not their ASCII
> equivalents. Always check strcode.h before assuming "main" hashes
> to `0xc8bb` — it doesn't, the canonical is `メイン` = `0x7df9`.
