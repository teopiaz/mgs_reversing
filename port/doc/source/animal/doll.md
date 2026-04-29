# `animal/doll/` — generic puppet character

The most-spawned NPC in the game. `Doll` is the chara MGS uses
whenever it needs a humanoid that:

- walks a hand-authored route (a sequence of `HZD_PTP` waypoints),
- plays scripted animations on cue,
- emits voice/sound from a fixed list,
- doesn't have AI of its own (the *script* drives it via `mesg`).

This is the chara that runs s01a's helipad guards on patrol, the
DARPA Chief sitting in his cell, every "scripted background NPC"
in the game. It also doubles as the cinematic-puppet base via
`demodoll.c` / `demodoll0.c`.

Three files:

| File | Lines | Role |
| ---- | ----- | ---- |
| [`doll.c`](../../../../source/animal/doll/doll.c) | 760 | Factory + Act + lifecycle. Parses the GCL `chara &DOLL ...` directive, wires up motion control, runs the per-tick state machine. |
| [`demodoll.c`](../../../../source/animal/doll/demodoll.c) | 548 | State callbacks (the function-pointer "modes" the doll switches between). Walking, turning, speaking, idle, attack-pose. |
| [`demodoll0.c`](../../../../source/animal/doll/demodoll0.c) | 438 | More state callbacks — the second wave (boss-fight states, knockdown/recovery, special-purpose poses). The 0-suffix is just a numbering convention; nothing else is "demodoll1". |

Header:
[`doll.h`](../../../../source/animal/doll/doll.h) defines
`DollWork` and `DollMotion` and exposes only `NewDoll_800DCD78` —
everything else is internal.

## DollWork — the state struct

`DollWork` is one of the larger animal structs (~3.7 KiB). What's
laid out:

```c
typedef struct _DollWork {
    GV_ACT          actor;                // engine actor header
    CONTROL         control;              // pos / collision / msgs
    OBJECT          body;                 // KMD + bone hierarchy
    MOTION_CONTROL  m_ctrl;               // animation player
    MOTION_SEGMENT  oars[42];             // 21 primary + 21 mask anims
    SVECTOR         rots[20];             // bone rotation buffer
    SVECTOR         adjust[20];           // bone adjust deltas (used
                                          // by demodoll head-track)
    MATRIX          light[2];             // per-side light xform

    OBJECT          weapon;               // optional weapon KMD
    GV_ACT         *glight;               // gun-tip light (FAMAS only)

    int             fA78;                 // ?
    short           fA86;                 // current route index
    char            fA8C[4];              // route slot table
    int             fA90;                 // n_points in current route
    SVECTOR         fA94[32];             // expanded waypoint cache

    int             fB94;                 // current waypoint index
    SVECTOR         fB98;                 // current waypoint position
    SVECTOR         fBA0;                 // target waypoint position
    int             fBA8;                 // current HZD addr
    int             fBAC;                 // current map id
    int             fBB0, fBB4, fBB8;     // last-known HZD/map snapshot

    int             fBC4;                 // map id for fBC8
    SVECTOR         fBC8;                 // last-known mov before route teleport

    DollFunc        fBD0;                 // CURRENT STATE FN PTR ←
    int             fBD8;                 // sub-state counter
    int             fBE0, fBE4;           // mode timers

    int             fBE8[5];              // voice queue
    int             fBFC;                 // pad bits the doll reads
    int             fC04;                 // ?
    short           fC08, fC0A, fC0C, fC0E;  // turn/yaw bookkeeping
    int             fC10, fC14, fC18, fC1C; // counters

    short           fC30[8];              // motion timing constants
                                          // (copied from s01a_word_800C3CD4)
    short           fC40[4];              // initial-yaw lookup
    DollMotion      fC48[12];             // 12 per-anim configs
                                          // (motion id + 4 timing SVECTORs)
    int             fDF8;                 // motion-control 'b' option
    short           fDFC;                 // ?
    short           fDFE;                 // 'i' option (HASH_ for mesg)

    short           fE00[2];              // [0]=blink/sound off
                                          // [1]=sound enable (HASH_SOUND_ON
                                          // toggles)
    GV_ACT         *fE04;                 // blink-tx actor (eyelid blink)
    int             fE08, fE0C;
    GV_ACT         *shadow;               // drop shadow (NewShadow)
    int             fE18[8];              // voice IDs ('s' option list)
    int             fE38;                 // current-voice index in fE18[]
    short           fE3C, fE3E;           // skin index / variant
    int             fE40, fE44;           // animation timer / phase
    char            fE48[16];             // ?
    int             fE58;                 // ALLOC FLAGS bitmask:
                                          //   0x02 = body inited
                                          //   0x04 = shadow allocated
                                          //   0x08 = weapon allocated
                                          //   0x10 = glight allocated
                                          //   0x20 = blink-tx allocated
    int             fE5C;
    int             fE60[8];
    short           fE80, fE82;
} DollWork;
```

`fE58` is the **alloc-bitmap**: each bit marks "owns one resource".
`DollDie_800DC8F0` walks the bits and frees only what was actually
allocated, so partial-init failures during `NewDoll` (e.g. shadow
KMD missing) clean up correctly.

`fBD0` is the **state pointer** — the function called every tick
to drive the doll's behaviour. See *State machine* below.

## NewDoll — the factory

Called by `GM_Command_chara` when the GCL hits `chara &DOLL ...`:

```c
void *NewDoll_800DCD78(int name, int where, int argc, char **argv)
{
    DollWork *work = GV_NewActor(GV_ACTOR_LEVEL4, sizeof(DollWork));
    if (work) {
        GV_SetNamedActor(&work->actor,
                         DollAct_800DBE9C,
                         DollDie_800DC8F0,
                         "doll.c");
        if (DollGetResources_800DCAA4(work, name, where) < 0) {
            GV_DestroyActor(&work->actor);
            return NULL;
        }
    }
    return work;
}
```

`name` and `where` come from the chara directive: `name` is the
actor's `control->name` (a GV_StrCode hash used for `mesg` lookup);
`where` is the script-binding map id.

`DollGetResources_800DCAA4` is the one-stop init that parses every
GCL option the doll cares about:

| Option | Field | Meaning |
| ------ | ----- | ------- |
| `-m $s:HHHH` | model id | body KMD hash (GV_StrCode of e.g. "snake") |
| `-o $s:HHHH` | motion id | OAR (animation) hash, defaults to model id |
| `-c $s:R G B` | light colour | per-side ambient |
| `-p X Y Z` | mov | spawn position (or first waypoint if absent) |
| `-d Y` | rot.vy | spawn yaw (PSX 4096 = 360°) |
| `-g <hzd-flags>` | exclude_flag | surface-flag mask to ignore |
| `-s VOX1 VOX2 ...` | fE18[] | voice IDs the doll cycles through |
| `-b N` | motion-control b | per-character motion variant |
| `-w $s:HHHH` | weapon | weapon KMD hash; bound to bone 4 |
| `-r <route>` | route slot | which HZD route table entry |
| `-a <anim>` | initial action | starting state |
| `-n N` | nodes | node count for non-route patrols |
| `-x <??>` | x-flag | (purpose unclear — see _unreversed.md) |
| `-h N` | fE3E | skin variant index |
| `-i HHHH` | fDFE | mesg id for "kill me" handshake |
| `-j <id>` | blink-tx | spawn an eyelid-blink overlay actor |
| `-e <hash>` | event id | HZD trap binding |
| `-z <??>` | z-flag | (purpose unclear) |
| `-f <??>` | f-flag | (purpose unclear) |
| `-v <??>` | voice variant | sometimes overrides `-s` |

Each option is read with `GCL_GetOption('x')` returning a pointer
into the directive's bytecode if present, NULL otherwise.

After parsing, `DollGetResources_800DCAA4` runs in this order:

1. `GM_InitControl(&work->control, name, where)` — register the
   CONTROL in the global table. This binds the doll's `mesg`
   address.
2. `GM_InitObject(body, model_kmd, BODY_FLAG, motion_oar)` —
   look up the KMD + OAR in `GV_CacheSystem`, allocate `DG_OBJS`.
   **Returns -1 silently on cache miss**; if this happens, body.objs
   stays NULL and the next step crashes (which is why the editor
   added a port-only NULL guard — see
   [`port/editor/03-control-and-motion.md`](../03-control-and-motion.md)).
3. `GM_ConfigObjectJoint(body)` — point `body->objs->rots` at
   `work->rots[]` so the render pipeline sees the bone rotations.
4. `GM_ConfigMotionControl(body, m_ctrl, oar, m_segs1, m_segs2,
                            control, rots)` — wire the animation
   player to the body + control. Now `GM_ActMotion` will play the
   selected action's segment data.
5. `GM_ConfigMotionAdjust(body, work->adjust)` — install the
   per-bone adjust array (used by head-track / aim modifiers).
6. Optional: `NewShadow(...)`, weapon `GM_InitObject`,
   `NewGunLight_800D3AD4` for FAMAS.
7. Route resolution: `s01a_doll_800DBF28` walks the HZD's
   `routes[]` array, picks the route slot at index
   `work->fA8C[work->fA86]`, copies its waypoints into `work->fA94[]`.
8. Initial pose: place mov at `fA94[0]`, snap rot to a value
   derived from `fA94[0].pad & 0x300`.

If any allocation fails, `DollGetResources_800DCAA4` returns -1
and `NewDoll` calls `GV_DestroyActor`. The destructor walks
`fE58` and frees only what got allocated.

## DollAct — the per-tick driver

The actor's `Act` callback runs every frame at GV_ACTOR_LEVEL4:

```c
void DollAct_800DBE9C(DollWork *work)
{
    if (GM_CheckMessage(&work->actor, work->control.name, HASH_KILL)) {
        GV_DestroyActor(&work->actor);
        return;
    }
    s01a_doll_800DBE0C(work);          /* drain mesgs (HASH_SOUND_ON/OFF) */
    GM_ActControl(&work->control);     /* integrate step + collide */
    GM_ActObject2(&work->body);        /* render pose */
    Demodoll_800DDF18(work);           /* call current state fn */
    DG_GetLightMatrix2(&work->control.mov, work->light);
    if (GM_CheckMessage(&work->actor, work->control.name, HASH_KILL)) {
        GV_DestroyActor(&work->actor);
    }
}
```

The double KILL check brackets the body so a script can fire
`mesg &DOLL HASH_KILL` from anywhere — the doll dies the same
tick. `Demodoll_800DDF18` is the one that calls `work->fBD0(work,
work->fBD8)` — i.e. it runs the *current state*.

`GM_ActControl` runs **before** the state fn, which means the
state fn sees this frame's collision-corrected position. The
state fn typically writes `control.step` for *next* frame.

## State machine — `work->fBD0`

A doll has 5–10 distinct states it cycles through. Each is a
`DollFunc` (`void(DollWork *, int)`) — the int arg is `fBD8`, a
sub-state counter the state itself manages. State transitions
happen by overwriting `work->fBD0` with a new function pointer.

The named ones in `demodoll.c` / `demodoll0.c`:

| State fn | Purpose |
| -------- | ------- |
| `Demodoll_800DD798` | Idle — face camera, do canned breathing |
| `Demodoll_800DD860` | Walking — follow the route waypoints |
| `Demodoll_800DDB18` | Turning toward a target |
| `Demodoll_800DDD14` | Talking — head track + voice playback |
| `Demodoll_800DDEAC` | Initial entry — wait one frame, transition |
| `Demodoll_800DDF4C` | Snap pose to current waypoint (script-cued) |
| `Demodoll_800DDF84` | Advance to next waypoint |
| `Demodoll_800DEA04` | Knockdown / death |

State fns generally have this shape:

```c
void Demodoll_<name>(DollWork *work, int subcounter)
{
    if (subcounter == 0) {
        /* Entered this state — do one-shot setup. */
        GM_ConfigObjectAction(&work->body, ANIM_FOR_THIS_STATE, 0, 4);
        work->fBE0 = 0;
    }

    if (transition_condition(work)) {
        work->fBD0 = NextStateFn;
        work->fBD8 = 0;
        return;
    }

    /* Continue this state's per-tick work. */
    work->control.turn.vy = compute_turn(work);
    /* …etc. */
}
```

`SetMode(work, NewState)`-style helpers don't exist as named funcs
in doll/ — the state-pointer assignment is done inline. Some
state fns also call `GM_ConfigObjectAction(body, ANIM_ID, 0, 4)`
to switch animation on entry.

## Route following

The most-used pattern. `fA94[]` holds up to 32 expanded waypoints
(each is a HZD `HZD_PTP` with `pad` flag bits). `Demodoll_800DDF84`
advances to the next waypoint and queries
`HZD_GetAddress(map->hzd, &fA94[next], -1)` for the new HZD bucket.

`fBA0.pad & 0x1F == 0x1F` is the route's "end of life" sentinel —
when the doll reaches a waypoint with all 5 low bits set, it
`GV_DestroyActor`s itself. This is how scripted dolls remove
themselves at the end of their patrol.

The waypoint's `pad & 0x300 >> 8` is an initial-yaw index into
`fC40[4]` — useful for "stand facing X direction at this point".

`Demodoll_800DE024(work, 250)` is the "are we close enough to the
target waypoint?" test (250 PSX units = ~25cm).
`Demodoll_800DE0AC` is the variant that *also* turns the body
toward the target while moving.

## Voice playback

`fE18[8]` stores up to 8 VOX IDs from the GCL `-s` option:

```gcl
chara &DOLL $s:NPC1 \
    -m $s:darpa \
    -p 1234 0 5678 \
    -s sd:01010001 sd:01010002 sd:01010003 \
    ...
```

`Demodoll_800DD75C` / `Demodoll_800DD764` (head-track helpers) and
`Demodoll_800DE264` (voice-trigger) walk the list and play one ID
at a time via `GM_VoxStream`. `fE38` is the cursor; when it
overflows past `fE18[]`'s population, voice stops.

The HASH_SOUND_OFF mesg flips `fE00[1]` to 0, which gates voice
playback off without resetting the cursor — useful for muting
dolls during a cinematic transition.

## Mesg handlers

`s01a_doll_800DBE0C` runs first thing each tick to drain pending
GV messages:

```c
control->n_messages = GV_ReceiveMessage(control->name, &control->messages);
for (each message) {
    switch (msg->message[0]) {
        case HASH_SOUND_ON:  work->fE00[1] = 1; break;
        case HASH_SOUND_OFF: work->fE00[1] = 0; break;
    }
}
```

Other mesg handlers (HASH_KILL, HASH_MOVE2, HASH_MAP) are
processed by `GM_ActControl` itself — the doll doesn't need to
intercept them.

## Cinematic puppet path — DEMODOLL

When the GCL fires `chara &DEMODOLL` (hash `0xCDDD`) instead of
`&DOLL`, the *same code* runs but the directive sets
`control->step_size = 0` so collision is skipped. The doll's pos
is then driven entirely by per-frame `DMO_ADJ` records via
`source/kojo/demo.c::demothrd_8007CFE8`. See
[`port/doc/demo/03-key-actors.md`](../../demo/03-key-actors.md)
and [`port/doc/demo/10-dmo-format.md`](../../demo/10-dmo-format.md).

The naming "demodoll" leaks into both contexts — `demodoll.c` is
where the *general* state functions live, not just the cinematic
ones. The cinematic-specific code is in `source/kojo/demo.c`, not
here.

## DollMotion — the per-anim timing table

```c
typedef struct _DollMotion {
    int     index;          // animation id (action passed to GM_ConfigObjectAction)
    SVECTOR entries[4];     // 4 timing SVECTORs:
                            //   [0].vx/vy/vz = enter/middle/exit timestamps
                            //   [1] = step delta multipliers
                            //   [2] = sound-trigger frames
                            //   [3] = camera-shake frames
} DollMotion;
```

`fC48[12]` is the doll's table — up to 12 simultaneously-loaded
animations with per-action timing. Used by the state fns to
schedule events relative to animation playback.

## Common patterns

### Switching state with a clean entry

```c
work->fBD0     = NewState;
work->fBD8     = 0;
work->control.turn.vz = 0;       // clear pitch turn
work->control.turn.vx = 0;       // clear roll turn
```

The `turn.vz = turn.vx = 0` always pairs with state changes —
new states almost always want to face level (no roll, no pitch).
Yaw (`turn.vy`) is preserved.

### Animation change on state entry

```c
if (subcounter == 0) {
    GM_ConfigObjectAction(&work->body,
                          s01a_dword_800C3CE4[ANIM_INDEX],
                          0,        /* start frame */
                          4);       /* interp frames */
    work->fBE0 = 0;
}
```

`s01a_dword_800C3CE4[]` is a stage-private table of animation
indices — index→action-id mapping for the doll's available anims.

### "Has the player walked away?" check

```c
if ((work->fBFC & 0x1FFF) != 0) {
    /* player triggered something — switch to alarmed state */
    work->fBD0 = AlarmedState;
}
```

`fBFC` is a bitmask of pad-input events the doll cares about
(player pressed action button while in dialogue range, etc.).

## Open questions

See [_unreversed.md](_unreversed.md) for what's still by-address.
The major opaque areas:

- The exact semantics of `-x`, `-z`, `-f`, `-v` GCL options.
- `fE48[16]` — uninspected during decomp.
- Several state fns named by address that don't appear in any
  call sites we've fully decoded.
- The `fC30[8]` / `fC40[4]` constants table seeded from
  `s01a_word_800C3CD4` — sourced from s01a but used everywhere.

## Files at a glance

```
doll.h          — DollWork (3.7 KiB), DollFunc, DollMotion, NewDoll proto.
doll.c          — Factory, init, GCL parsing, route resolution,
                  destructor (free-by-bitmap), Act dispatch loop.
demodoll.c      — Idle / walking / turning / talking states +
                  voice-playback helpers + waypoint-distance tests.
demodoll0.c     — Knockdown / damaged / special-case states +
                  head-track adjustments (work->adjust[2/6/7]).
```

## See also

- [03-control-and-motion.md](../03-control-and-motion.md) — what
  `GM_ActControl`, `GM_ConfigMotionControl`, `GM_ActObject2` do.
- [02-game-loop.md](../02-game-loop.md) — when the actor's `Act` is
  called.
- [`port/doc/demo/03-key-actors.md`](../../demo/03-key-actors.md) —
  DEMODOLL chara hash + cinematic side.
- [`source/chara/snake/`](../../../../source/chara/snake/) — the
  same patterns at 10× scale (Snake is essentially a giant doll
  with player-input wiring).
