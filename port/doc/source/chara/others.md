# `chara/others/` — small player-side utility actors

Three short files (totaling 357 lines) for utility characters that
don't fit anywhere else.

| File | Lines | Role |
| ---- | ----- | ---- |
| [`intr_cam.c`](../../../../source/chara/others/intr_cam.c) | 120 | "Introductory camera" — flies the camera through a scripted path on stage entry. |
| [`belong.c`](../../../../source/chara/others/belong.c) | 148 | "Belong" — tracks which item each character carries. |
| [`motse.c`](../../../../source/chara/others/motse.c) | 89 | "Motion-sound bridge" — wires animation events to sound triggers. |

## intr_cam.c — introductory camera

The actor a stage spawns to take camera control during the
opening fly-by. When you load a stage and the camera glides from
sky down to Snake, that's `intr_cam`.

### State machine

```c
typedef struct _Work {
    GV_ACT  actor;
    int     name;        /* GV_StrCode of "intr_cam" */
    int     state;       /* 0 = uninit, 1 = active, 2 = fading, 3 = dead */
    int     interp;      /* interp counter */
    SVECTOR pos;         /* current camera pos */
    SVECTOR eye;         /* target eye position (lerped to) */
} Work;
```

States flow:

```
spawn → 0 (uninit)
HASH_ON  → 1 (active — drive gUnkCameraStruct)
HASH_OFF → 2 (fading — lerp back to gameplay cam)
HASH_KILL → 3 (dead — destroy on next tick)
```

### Why it's in `chara/others/`

It's a chara — spawned via `chara $s:intr_cam` from the GCL — but
it's not a player character and doesn't have CONTROL/OBJECT. It's
essentially a "camera-driver actor" parallel to the per-stage
overlay variants (`democame.c`, `11g_demo.c`).

### Used by

- Stage GCL scripts that want a flyby. The script issues:
  ```gcl
  chara $s:intr_cam $s:CAM1 -p ... -e ...
  mesg $s:CAM1 HASH_ON
  delay 240
  mesg $s:CAM1 HASH_OFF
  delay 60
  mesg $s:CAM1 HASH_KILL
  ```

### Relation to the camera pipeline

Documented in [`doc/demo/04-camera-pipeline.md`](../../demo/04-camera-pipeline.md)
under "GCL-scripted: a per-stage overlay actor". This is the
shared (non-stage-specific) variant.

## belong.c — item-belonging tracker

Tracks which character has picked up which item. When Snake
collects a ration, Snake's `belong` table records it. When a
guard is alerted to a missing item, his `belong` table reflects
the loss.

The actor is spawned once per stage and lives at a high level
(GV_ACTOR_LEVEL0 or 1). It maintains a small table indexed by
chara id with item id payload.

Used for:
- The "drop body to wake guard" mechanic — tracking which guard
  was knocked out.
- Item-related event triggers (e.g. handing rations to Meryl).

### Sketch

```c
typedef struct BelongWork {
    GV_ACT actor;
    struct {
        int chara_id;
        int item_id;
        int held_until;     /* tick to drop */
    } entries[16];
} BelongWork;
```

Other code calls `Belong_Add(chara, item)`, `Belong_Remove(chara,
item)`, `Belong_GetItem(chara) → item_id`.

## motse.c — motion-sound bridge

When a character animation has a sound trigger frame (e.g. a
footstep beat at frame 5 of the run anim), motse.c is what fires
the sound. It listens via `mesg` for animation events and
dispatches to `GM_SeSetMode`.

### Why a separate file

Decouples the sound system from the animation system. An
animation segment can declare "sound event N at frame X" without
needing to know what sound N is — motse.c does the lookup.

### Public API

```c
void sna_act_helper2_helper2_80033054(int chara_name, SVECTOR *pos);
```

(Recovered name. The function takes the actor's name + a position
SVECTOR, plays the sound effect bound to that name's current
animation frame.)

Called from `animal/doll/demodoll0.c::Demodoll_800DD764` and
similar sites in `chara/snake/sna_init.c`.

## See also

- [snake.md](snake.md) — the player Snake uses motse.c for
  footsteps + breathing.
- [`source/sound/`](../../../../source/sound/) — the SPU side of
  the sound dispatch.
- [`doc/demo/04-camera-pipeline.md`](../../demo/04-camera-pipeline.md)
  — where intr_cam fits in the camera-source taxonomy.
