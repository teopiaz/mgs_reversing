# `animal/` — what's not yet reverse-engineered

Every C file under `source/animal/` is **decompiled** — there's no
remaining MIPS assembly to convert. What this doc tracks is the
*meaning* gaps: functions still named by their address, struct
fields still named by their offset, magic constants where the
purpose isn't obvious, and forward-declared opaque externs.

These aren't bugs. The decompilation matches the disc binary
byte-for-byte. They're invitations for someone to play through the
relevant stages, watch what fires when, and rename / annotate.

## Convention

In source, opaque names take three shapes:

- **Function still by address**: `s07c_meryl72_unk1_800C9258`,
  `Demodoll_800DDF18`, `s11e_zako11e_800D34D0`. Format
  `<stage>_<file>_<descriptor>_<address>` or just
  `<Component><Verb>_<address>`.
- **Struct field by offset**: `fE58`, `field_B74`, `field_14`.
  These are members at offset 0xE58 / 0xB74 / 0x14 within their
  containing struct.
- **Magic constants**: arrays like `s01a_word_800C3CD4[8]` or
  `s11i_dword_800C32F0[8]` — eight values copied into a work
  struct at init, used by code that hasn't been fully traced.

Renaming any of these requires:

1. Finding *all* call sites (use `grep -rn`).
2. Updating the matching disc-build symbol map in
   `build/functions.txt` so the byte-identical match still passes
   (or accepting a non-matching rename + flagging it).
3. Cross-checking that the new name reflects what the function /
   field actually does in-game (often the hard step).

## doll/

### Functions named by address

```
s01a_doll_800DBE0C   — mesg drain, called from DollAct each tick
s01a_doll_800DBF28   — route resolution (HZD route lookup)
s01a_doll_800DBFC4   — small char-tuple parser (helper)
s01a_doll_800DC01C   — small short-tuple parser
s01a_doll_800DC074   — small int-tuple parser
s01a_doll_800DC0CC   — read GCL '-r' option (route slot)
s01a_doll_800DC134   — read GCL '-s' option (voice list)
s01a_doll_800DC1AC   — body-init helper (KMD load + motion config)
s01a_doll_800DC570   — read GCL '-f' / '-v' options (purpose unclear)
s01a_doll_800DC648   — animation-table init from -a values
s01a_doll_800DC774   — read GCL '-a' option, set initial action
s01a_doll_800DC7FC   — read GCL '-n' option, set node count
s01a_doll_800DC884   — read GCL '-x' option (purpose unclear)
s01a_doll_800DC9FC   — final init pass (state pointer set)
s01a_doll_800DCAA4   — DollGetResources_*: master init (this is named)
DollDie_800DC8F0     — destructor (named)
DollAct_800DBE9C     — Act dispatcher (named)
NewDoll_800DCD78     — factory (named)

Demodoll_800DD75C    — small head-track helper, no body
Demodoll_800DD764    — calls sna_act_helper2_helper2_80033054
Demodoll_800DD798    — state fn — appears to be "idle"
Demodoll_800DD860    — state fn — appears to be "walking"
Demodoll_800DDB18    — state fn — appears to be "turning"
Demodoll_800DDD14    — state fn — appears to be "talking"
Demodoll_800DDEAC    — state fn — entry / first frame
Demodoll_800DDF18    — Act-internal driver (calls work->fBD0)
Demodoll_800DDF4C    — snap pose to current waypoint
Demodoll_800DDF84    — advance to next waypoint
Demodoll_800DE024    — distance-to-target test
Demodoll_800DE0AC    — turn-and-walk-to-target
Demodoll_800DE25C    — empty (returns 0)
Demodoll_800DE264    — voice-line trigger
Demodoll_800DEA04    — knockdown / death state
```

### DollWork fields by offset

The `DollWork` struct in `doll.h` has 21 unnamed `fXXX` fields and
several `padNN[]` byte arrays. The high-confidence renames
(based on usage tracing in this doc):

| Field | Likely name | Confidence |
| ----- | ----------- | ---------- |
| `fA86` | `current_route` | high |
| `fA8C[4]` | `route_slots` | high |
| `fA90` | `route_n_points` | high |
| `fA94[32]` | `waypoints` | high |
| `fB94` | `waypoint_idx` | high |
| `fB98` | `waypoint_pos` | high |
| `fBA0` | `target_waypoint_pos` | high |
| `fBA8`/`fBAC` | `current_hzd_addr` / `current_map` | medium |
| `fBD0` | `state_fn` | high |
| `fBD8` | `state_subcounter` | high |
| `fE00[2]` | `[0]=blink_state, [1]=sound_enable` | high |
| `fE04` | `blink_tx_actor` | high |
| `fE18[8]` | `voice_ids` | high |
| `fE38` | `voice_idx` | high |
| `fE3C`/`fE3E` | `skin_idx` / `skin_variant` | medium |
| `fE58` | `alloc_flags` | high |

The remaining `fXXX` fields (`fA78`, `fAB0`, `fBE0`, `fBE4`,
`fBFC`, `fC04`, `fC10..fC20`, `fC30[8]`, `fC40[4]`, `fDF8`,
`fDFE`, `fE08`, `fE0C`, `fE40`, `fE44`, `fE48[16]`,
`fE5C`, `fE60[8]`, `fE80`, `fE82`) need in-game tracing to name
properly.

### GCL options whose purpose is unclear

The `chara &DOLL` directive accepts these options, but their effect
isn't documented:

- **`-x`** — read by `s01a_doll_800DC884` into an unnamed field.
  Possibly cinematic-suppress flag.
- **`-z`** — read by code at the end of `DollGetResources_*`.
  Possibly stage-specific behaviour gate.
- **`-f`** / **`-v`** — `s01a_doll_800DC570` reads both. Possibly
  motion-modifier flags (one is a frame-skip, the other a
  voice-pitch?).

### Magic constants

```c
short s01a_word_800C3CD4[8] = {31000, 15, 30, 60, 90, 32000, 32001, 30000};
```

Copied into `work->fC30[8]` at init. The values look like:

- `31000` / `32000` / `32001` / `30000` — large numbers, possibly
  PSX world-unit thresholds (visibility distances?).
- `15`, `30`, `60`, `90` — tick counts (15 = 0.5s, 30 = 1s,
  60 = 2s, 90 = 3s @ 30Hz). Probably state-timeout values.

```c
int s01a_dword_800C3D04[32] = {
    0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 0, 15,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 29, 30, 31
};
```

Power-of-two table that looks like bit flags. Used by `demodoll.c`
in `Demodoll_800DD6A8` to test pad bits. Each entry is a single
bit; the array is indexed by an action id to map "action X" to
"bitmask Y on the pad input".

```c
short s01a_dword_800C3CE4[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
```

Just a `0..14` identity table. Used in `Demodoll_800DD798` as the
animation-id lookup. Suspicious — the identity could be replaced
with the index, but the indirection is preserved on disc, so
either it's vestigial or an authoring tool added it for some
reason.

## meryl72/

### Functions still by address

[`meryl72.h`](../../../../source/animal/meryl72/meryl72.h) lists
~30 forward-declarations like:

```c
extern void s07c_meryl72_unk1_800C88EC( Meryl72Work *work, int time );
extern void s07c_meryl72_unk1_800C8970( Meryl72Work *work, int time );
extern void s07c_meryl72_unk1_800C8A30( Meryl72Work *work, int time );
…
```

These are action callbacks (every action in s07c.h's `ACTION_*`
table needs a callback). Function bodies are in `action.c`. Each
one has a clear job (one anim, one phase) but the *narrative* role
(this is "Meryl reloading", "Meryl ducking", …) needs in-game
play-through to identify.

The named ones — i.e. the ones we *do* know — are:

```c
ActReadyGun_800C9428      — draws her sidearm into ready stance
ActGrenade_800C9790       — pulls + throws a grenade
ActOverScoutD_800CAEA8    — override action: scouting D variant
ML72_PutBreath_800CB35C   — spawns breath cloud
ML72_PutBlood_800CB2EC    — spawns blood spray on damage
ML72_SetPutChar_800CB584  — installs a put-fx sub-actor
ML72_ClearPutChar_800CB5CC — removes a put-fx sub-actor
```

All other `s07c_meryl72_unk1_800CXXXX` are open targets.

### Meryl72Work fields by offset

```c
UNK             f8BC;            // {field_00..field_1E} — per-tick state
                                 // mirror, possibly "last frame's pose"
void           *fA9C[8];         // 8-entry function-pointer table
                                 // (think handlers? checks?)
short           fAE8/fAEA/fAEC/fAEE;  // four shorts, possibly counters
int             fAF0/fAF4;       // two ints, possibly timeouts
char            fB0A/fB0B;       // anim-step gates
int             fB0C;
int             fB18;
SVECTOR         fB28;
int             fB2C;
int             fB4C, fB58, fB5C, fB60, fB64, fB68;  // patrol-related
SVECTOR         fB6C;
int             field_B74, field_B78;
short           fB96;
int             fB98, fB9C;
int             fC04;
short           fC08, fC0A, fC0C, fC0E;
int             fC10[3], fC1C[6];
short           fC34, fC36;
int             fC38, fC3C;
```

`UNK` (the struct at offset 0x8BC) has its own `field_00..field_1E`
unnamed fields. Likely "pose snapshot" — the values appear to be
written by the action callback and read by the dispatcher.

### action.c is the heavy lift

`action.c` has 1487 lines and ~30 callbacks. Many start by writing
to `work->f8BC` (the UNK), then animating. Renaming each callback
requires watching Meryl in s07c/s09a and noting which one fires
when she's in each visible state.

### Patrol / movement gaps

`work->next_node`, `work->n_patrols`, `work->nodes[32]` are clear,
but the *transitions* between nodes are driven by short integers
in `fAE8..fAEE` and `count3` whose exact roles aren't traced.

## zako11e/

### Functions still by address

Sample (from grep):

```
s11e_zako11e_800D34D0   — KMD-swap helper for LOD (fully understood,
                          just not renamed)
s11e_zako11e_800D354C   — visibility + LOD-swap dispatch (understood)
s11e_zako11e_800D3934   — comm-state init (zero the WatcherUnk)
s11e_zako11e_800D3990   — main resource init (parses GCL options)
s11e_zk11ecom_800D9A20  — combat helper (purpose: probably target-
                          select / LOS test)
Zako11EPushMove_800D889C — pre-control hook (named)
Zako11EActionMain_800D8830 — per-tick action dispatch (named)
ZakoAct_800D3684        — Act callback (named — shared with base zako)
ZAKO11E_SetPutChar_800D8004 — put-fx install (named)
RootFlagCheck_800D34C8  — (no-op stub; same in every animal/)
InitTarget_800D3800     — TARGET registration (named)
```

`zk11eaction.c` (1866 lines) is the largest single file in animal/
and has the most opaque action callbacks. Naming them needs a
designer-led pass through s11e gameplay.

### ZakoWork fields

`field_904`, `field_94C`, `field_B74` — the second / third TARGETs
and the comm-group entry. High-confidence likely names:

| Field | Likely name |
| ----- | ----------- |
| `field_904` | `attack_target` |
| `field_94C` | `touch_target` |
| `field_B74` | `comm_idx` |

The `WatcherUnk unknown` substruct is partially named:

| Field | Known |
| ----- | ----- |
| `unknown.last_set` | command id this guard most recently issued |
| `unknown.last_unset` | command id this guard most recently cleared |
| `unknown.field_14` | (counter — alert-cooldown?) |
| `unknown.field_1E` | (flag — "alert active"?) |
| `unknown.field_00..1C` | various — comm group state |

### Magic constants

```c
extern int  ZAKO11E_EYE_LENGTH_800C3904;
extern SVECTOR ZAKO11E_NO_POINT_800C38FC;
extern SVECTOR ZAKO_TARGET_SIZE_800C38CC, ZAKO_TARGET_FORCE_800C38D4;
extern SVECTOR ZAKO_ATTACK_SIZE_800C38DC, ZAKO_ATTACK_FORCE_800C38E4;
extern SVECTOR ZAKO_TOUCH_SIZE_800C38EC,  ZAKO_TOUCH_FORCE_800C38F4;
```

The `ZAKO_*_SIZE` / `ZAKO_*_FORCE` pairs are TARGET dimensions —
how big the hit/attack/touch volumes are. Values aren't documented
but can be derived by playing the game with debug overlays.

`ZAKO11E_EYE_LENGTH` = how far this guard sees. Uniquely per-
variant; the s11e zako sees ~5000 units IIRC but isn't recovered
to a named meaningful value here.

## zako11f/

### Functions still by address

Sample:

```
ZAKO11FAct_800C88AC          — Act callback (named)
ZAKO11FDie_800C8E2C          — destructor (named)
NewZako11F                   — factory (named)
Zako11FGetResources_800C9070 — resource init (named)
ReadNodes_800C8E4C           — patrol-node parser (named)
InitTarget_800C8A10          — TARGET init (named)
RootFlagCheck_800C86F0       — no-op stub
s11i_zako11f_800C86F8        — KMD swap util (similar to e variant
                                but unused — kept for future LOD?)
s11i_zako11f_800C8774        — visibility+group state update
s11i_zako11f_800C8B3C        — comm-state init
s11i_zako11f_800C8DB8        — mesg drain
s11i_zako11f_800C8EE8        — small parser (b:N or numeric byte)
s11i_zako11f_800C8F40        — small parser (16-bit int)
s11i_zako11f_800C8F98        — constants-table init (8 entries)
```

`action.c` (2001 lines) and `zk11fcom.c` (985 lines) have the
largest concentration of unnamed action callbacks. Same need as
zako11e — designer-led identification.

### Apparent dead code

`s11i_zako11f_800C86F8` (the LOD helper) is present but never
called. Either (a) leftover from copy-paste of zako11e, (b) used
by an action we haven't traced, or (c) referenced via fn-ptr table
indirection. Worth investigating.

### Magic constants

```c
int s11i_dword_800C32F0[8] = { … };
```

Eight 32-bit values copied into the work struct by
`s11i_zako11f_800C8F98`. Same shape as zako11e's
`s11e_word_800C3CD4[8]` but different values.

## Cross-component patterns

### `RootFlagCheck` is a no-op everywhere

Every animal component has a `RootFlagCheck_<addr>` that does
nothing. It's a vestigial hook from an earlier prototype where
"root" damage zones (the per-bone defends[]) had per-frame
revalidation. The function survived as a stub.

### Comm-state struct (`WatcherUnk`)

Shared across `zako11e`, `zako11f`, and `enemy/watcher.c`. Its
exact layout is in `enemy.h`. Several fields (`field_00..1C`,
`field_14`, `field_1E`) are unnamed but appear to be "alert
broadcast" + "command request" pairs.

### Function-pointer arrays

`work->fA9C[8]` in Meryl72 and similar arrays in zako11e/f. Likely
"per-state handler" tables — when an action wants a sub-handler
(e.g. fire-on-frame-N), it indexes into one of these arrays.
Not yet traced.

## How to help

If you want to chip away at this list:

1. Pick a **renamed** function that has the most call sites
   (`grep -rn s07c_meryl72_unk1_800C9258 source/`).
2. Read the body — the surrounding context (which fields it
   reads/writes, which other named fns it calls) usually reveals
   the role.
3. Play the matching stage with the function under a debugger
   breakpoint to confirm.
4. Submit a rename PR — keep the disc-match by also updating
   `build/functions.txt`. Or skip the matching constraint and
   just patch the source if the goal is documentation.

The decompilation tools (decomp.me presets, the `build/decompme_asm.py`
script) are still helpful even for already-decomp'd code — they
let you compare before/after C and verify the rename didn't change
codegen.

## See also

- [`build/functions.txt`](../../../../build/functions.txt) — the
  canonical "all functions" list with addresses.
- [README.md](README.md) — animal/ component overview.
- Each component's doc (`doll.md`, `meryl72.md`, `zako11e.md`,
  `zako11f.md`) — for the in-context view of each function.
