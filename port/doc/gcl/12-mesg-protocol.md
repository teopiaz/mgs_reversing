# The `mesg` Protocol

`mesg` is the primary inter-actor communication channel. It's how
scripts tell actors to "do a thing" without spawning new bytecode —
the destination actor's C-side message handler decodes the payload.

Runtime: [source/game/script.c:445 GM_Command_mesg](../../../source/game/script.c#L445).

---

## Wire format

```gcl
mesg <address> <payload-word-1> <payload-word-2> ...
```

- `<address>` — the destination actor handle. Two common forms:
  - `arg1` (when forwarding from a trap, where `arg1` = subject)
  - A hashed name like `$s:21ca` (`SNAKE`) — looks up the actor by ID
- `<payload-word-N>` — up to ~7 short ints. Each is a 16-bit value
  packed into a `GV_MSG.message[N]` slot.

Implementation:

```c
mesg.address = GCL_GetNextParamValue();        // first arg: who
pMsgDst = &mesg.message[0];
while (uParm1 = GCL_GetParamResult(), uParm1 != 0x0) {
    *pMsgDst++ = (short)GCL_StrToInt(uParm1);  // pack subsequent args
}
GV_SendMessage(&mesg);
```

The C-side actor's `recv` callback gets the `GV_MSG` and switches on
`message[0]` (an event hash) to decide what to do.

---

## Common payload patterns

### One-shot toggle: "on" / "off"

```gcl
mesg $s:18e3 $s:0e4e         # snow → on
mesg $s:18e3 $s:c927         # snow → off
mesg arg1 $s:0e4e             # forward "on" to whoever fired this trap
```

`message[0]` is the event hash. The receiver's switch typically
handles `HASH_ON` / `HASH_OFF` by toggling its own visibility / state.

### Door-style: open / close

```gcl
mesg $s:dd6d $s:0e4e          # tell door 0xDD6D to open
```

Door actors have additional state (locked, animating) — opening /
closing flips a flag and triggers an animation.

### Pose / motion change

```gcl
mesg $s:21ca $s:937a 5        # tell snake: motion event, motion id 5
mesg $s:21ca $s:e2e9 1024     # tell snake: turn event, target yaw 1024
mesg $s:21ca $s:62b6 -3000 100 -500   # tell snake: position event, X Y Z
```

Different `message[0]` values use different numbers and meanings of
the rest of the payload.

### Hand-off args from trap

```gcl
trap $s:c776 $s:21ca $s:0dd2 {
    mesg arg1 $s:0e4e         # tell whoever entered (arg1 = snake) to "on"
    mesg arg1 $s:14c9 arg3    # forward the event we received
}
```

`arg3` here is the original event hash that fired the trap.

---

## Message dispatch on the receiver side

Each actor type has a `recv` callback registered with
`GV_RecvMessage`. The callback compares `message[0]` against known
event hashes:

```c
// hypothetical actor recv
int my_actor_recv(GV_ACT *self, GV_MSG *msg)
{
    switch (msg->message[0])
    {
    case HASH_ON:
        ((MyWork *)self)->visible = 1;
        return 1;
    case HASH_OFF:
        ((MyWork *)self)->visible = 0;
        return 1;
    case HASH_POSITION:
        ((MyWork *)self)->x = msg->message[1];
        ((MyWork *)self)->y = msg->message[2];
        ((MyWork *)self)->z = msg->message[3];
        return 1;
    }
    return 0;       // not our message
}
```

If no actor consumes the message, it's silently dropped.

---

## Common event hashes for mesg payloads

(Same set as triggers — see [07-triggers-and-events.md](07-triggers-and-events.md#standard-event-hashes).)

| `message[0]`      | Sigil          | Meaning for receiver           |
|-------------------|----------------|--------------------------------|
| `HASH_ON`         | `$s:0e4e`      | activate / show / start        |
| `HASH_OFF`        | `$s:c927`      | deactivate / hide / stop       |
| `HASH_KILL`       | `$s:3223`      | self-destruct                  |
| `HASH_POSITION`   | `$s:62b6`      | reposition; payload[1..3] = X Y Z |
| `HASH_TURN`       | `$s:e2e9`      | rotate; payload[1] = target yaw |
| `HASH_MOTION`     | `$s:937a`      | play motion id payload[1]      |
| `HASH_GO_MOTION`  | `$s:be0a`      | start motion (force restart)   |
| `HASH_MOVE`       | `$s:4b5d`      | walk to point                  |
| `HASH_STANCE`     | `$s:3238`      | change stance (stand/crouch/prone) |
| `HASH_RUN_MOVE`   | `$s:70fb`      | run-walk to point              |
| `HASH_VOICE`      | `$s:385e`      | play voice clip payload[1]     |
| `HASH_MODE`       | `$s:491d`      | switch mode payload[1]         |
| `HASH_LOOP`       | `$s:ca87`      | loop animation                 |
| `HASH_STOP`       | `$s:5e8b`      | halt motion                    |
| `HASH_SOUND_ON`   | `$s:2761`      | enable sound                   |
| `HASH_SOUND_OFF`  | `$s:ed7f`      | disable sound                  |
| `HASH_SLOW`       | `$s:3e92`      | slow-motion mode               |

---

## `GV_SendMessage` vs script-level `mesg`

Two near-identical paths in C:

| Caller            | API                                | Notes                             |
|-------------------|------------------------------------|-----------------------------------|
| C engine code     | `GV_SendMessage(&msg)`             | direct                            |
| GCL script        | `mesg ...` → `GM_Command_mesg`     | walks options, builds GV_MSG, calls GV_SendMessage |

So writing `mesg` in a script is exactly equivalent to constructing a
`GV_MSG` and calling `GV_SendMessage` — useful to know when reading
the engine source.

---

## Examples from the corpus

### s00a: snow toggle on zone transition

```gcl
trap $s:6373 $s:50ae $s:14c9 {
    if (arg3 == $s:0dd2) {
        mesg $s:18e3 $s:c927          # entering: snow off
    } elseif (arg3 == $s:d5cc) {
        mesg $s:18e3 $s:0e4e          # leaving: snow on
    }
}
```

### s01a: cell-block alert chain

```gcl
trap $s:c776 $s:21ca $s:0dd2 {
    mesg $s:a608 $s:0e4e              # alert nearby zako
    sound -x sd:01010030
    eval($f:040002 = true)
}
```

### s14e: cutscene puppet posing

```gcl
proc sub_pose_wolf {
    mesg $s:962c $s:62b6 -2000 0 5000   # move wolf to (-2000, 0, 5000)
    mesg $s:962c $s:e2e9 2048           # face yaw 2048 (180°)
    mesg $s:962c $s:937a 7              # play motion 7 (aiming)
}
```

---

## Pitfall: blocking vs fire-and-forget

`mesg` is fire-and-forget — control returns to the script
immediately. The destination handles the message during ITS act-tick
later this frame or the next.

If you need to wait for the action to finish:

```gcl
mesg $s:21ca $s:937a 5      # start motion 5
delay -t 60 -e sub_after    # wait 60 frames, then continue
```

There's no event you can subscribe to that says "motion 5 finished"
generically — actors that emit such events do it via more `mesg`
calls back to a coordinator, but each pairing is bespoke.

---

## Multi-recipient broadcasts

`mesg` only takes one address. To send to many actors, repeat:

```gcl
proc sub_alert_all {
    mesg $s:a608 $s:0e4e
    mesg $s:a60c $s:0e4e
    mesg $s:a60d $s:0e4e
}
```

Or use the wildcard target if the engine supports it for the receiver
group (some message types broadcast to all actors of a class).

---

## Debugging unreceived messages

If `mesg ... $s:foo $s:bar` doesn't seem to do anything:

1. **Check the address**: is the target actor actually spawned?
   Search the log for `[gcl] chara: name=0xfoo` — no chara, no
   recipient.
2. **Check the event hash**: typo or wrong byte order? Print it from
   inside the receiver's recv (or grep for the hash in the actor's
   C source).
3. **Check return value**: receivers return non-zero on consume. If
   no actor returns 1, the message is silently dropped — common
   cause for "spell didn't fire".

You can add an `extern` printf in `GV_SendMessage` while debugging
to log every dispatch.
