---
file: source/libhzd/event.c
---

# `libhzd/event.c` — events, traps, GCL bindings

Where the *world* talks to *scripts*. An event is a named point in
space; a bind says "when this event fires, run this GCL block". 638
lines.

## Data structures

### Event — `HZD_EVT`

```c
typedef struct {
    u_short  name;         // strcoded event name
    u_short  type;
    u_short  last;
    short    n_triggers;
    u_short  triggers[6];  // up to 6 conditions
    SVECTOR  pos;
} HZD_EVT;
```

Each event has a strcoded name (`HASH_DOOR_OPEN`, etc.), up to 6
trigger conditions, and a position. Events live in the HZD's
header.

### Bind — `HZD_BIND`

```c
typedef struct {
    short   field_0;            // entity name (who entered/left)
    short   field_2_param_m;    // mask: enter / leave / both
    short   field_4;            // trap name to match
    u_short map;                // which map (per-stage)
    u_char  field_8_param_i_c_flags;
    u_char  field_9_param_s;
    u_char  field_A_param_b;
    u_char  field_B_param_e;
    u_short field_C_param_d;
    u_short field_E_param_d_or_512;
    int     field_10_every;
    int     field_14_proc_and_block;   // GCL proc id + block flags
} HZD_BIND;
```

A bind connects:

- **What entity** (field_0): which actor this fires for (e.g. only
  for the player, not guards).
- **What event/trap** (field_4): name match.
- **What action** (field_14): the GCL proc to invoke + how (block
  or non-blocking).
- **Conditions**: `field_2_param_m` is the enter/leave mask;
  `field_10_every` is "every Nth fire", etc.

These are populated from the GCL `bind` command at boot.

## Public API

```c
void HZD_SetBind(int idx, HZD_BIND *bind, int n);    // populate binds
void HZD_BindMapChange(int mask);                     // re-fire binds for new map
void HZD_SetEvent(HZD_EVT *event, int name);          // map an event
void HZD_ExecBindX(HZD_BIND *bind, HZD_EVT *event, int phase, int arg2);
void HZD_ExecEventRCM(HZD_HDL *hzd, HZD_EVT *event, int arg2);
void HZD_ReExecEvent(HZD_HDL *hzd, HZD_EVT *event, unsigned int flags);
void HZD_ExecLeaveEvent(HZD_HDL *hzd, HZD_EVT *event);
void HZD_EnterTrap(HZD_HDL *hzd, HZD_EVT *event);
HZD_TRP *HZD_CheckBehindTrap(HZD_HDL *hzd, SVECTOR *svec);
```

## Lifecycle

### Boot

1. Stage's `.gcx` loads.
2. GCL `bind` commands populate the bind list via `HZD_SetBind`.
3. HZD file loaded; events accessible.
4. First-frame check: each bind whose `enter` mask matches the
   *current* state fires immediately (so players who start inside
   a trap get the trigger).

### Per-frame

1. Each registered actor reports its position to event.c (via
   `GM_ActControl`'s trap-check pass).
2. `HZD_EnterTrap` / `HZD_ExecLeaveEvent` fire the matching binds.
3. Each fired bind invokes its GCL proc (`field_14_proc_and_block`).

### Map change

`HZD_BindMapChange(mask)` re-evaluates all binds for the new map —
needed when a stage transitions sub-areas (s11d's basement vs.
ground floor).

## `HZD_ExecBindX` — bind firing

The dispatcher. Given a bind that just matched, walks its
condition list, builds a GCL_ARGS struct from the bind's params,
and calls `GCL_ExecProc(proc_id, &args)`.

The args passed to the proc include:

- The triggering entity's name hash.
- The event's name hash.
- The phase (enter / leave).
- Custom `param_d`/`param_b`/etc fields from the bind.

## `HZD_CheckBehindTrap` — "is there a trap behind me"

Used for player-lean-detection: when Snake hugs a wall, this checks
if a trap is on the *other side* (so wall-mounted enemies can
detect him).

## Pitfalls

- **Binds fire on enter and leave.** A proc that doesn't handle
  the leave phase may run unwanted code on exit.
- **Once-bind flags.** The `field_10_every = -1` value is a
  sentinel for "fire once and never again". Check for this in
  custom binds.
- **Same-name event aliasing.** Two events with the same name in
  the HZD file fire the same binds — useful for "any door" events,
  but a footgun if accidental.
- **Bind ordering matters.** Binds fire in registration order. If
  one bind's GCL changes state used by a later bind, the order
  becomes load-bearing.

## Port notes

`event.c` runs unmodified. The bind list survives stage transitions
because GCL re-runs at each load.

## See also

- [collide.md](collide.md) / [zone.md](zone.md) — geometry the
  events sit in.
- [bytecode.md](../libgcl/bytecode.md) — how `bind` commands
  encode.
- [`source/game/script.c`](../../../../source/game/script.c) — the
  game-tier `bind` command implementation.
