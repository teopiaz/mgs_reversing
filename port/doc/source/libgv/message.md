---
file: source/libgv/message.c
---

# `libgv/message.c` — the message bus

A tiny double-buffered message ring shared by every actor.

## Mental model

Each tick, actors **post** messages with `GV_SendMessage` and
**read** with `GV_ReceiveMessage`. Messages from one tick are
delivered on the *next* tick — the bus is a one-frame delay queue,
which is essential because actors execute in a deterministic
priority order: an actor at level 5 can post to level 4 and the
recipient sees it next frame regardless of which ran first.

```
   tick N         tick N+1
   --------       --------
   send(M)   →    receive(M)
                  send(M2)  →    tick N+2: receive(M2)
```

## Buffer layout

```c
typedef struct {        // memleak-leaked layout
    unsigned short address;     // recipient (strcoded actor name)
    unsigned short _len;        // run-length of pending msgs at this address
    unsigned short message[7];  // payload (up to 7 shorts)
    unsigned short message_len;
} GV_MSG;

typedef struct {                // private to message.c
    int     num;                // count of valid msgs (0..15)
    GV_MSG  msg[16];            // ring
} MESSAGE_LIST;

extern MESSAGE_LIST message_list_800B0320[2];   // double buffer
static int which_buffer;                         // 0 or 1, flips each tick
```

`which_buffer` toggles in `GV_ClearMessageSystem` (called by
`gamed.c` after the actor system runs). One buffer is *being read*
by actors; the other is *being filled* for next frame.

## API

```c
int  GV_SendMessage(GV_MSG *send);
int  GV_ReceiveMessage(int address, GV_MSG **msg_ptr);
void GV_InitMessageSystem(void);
void GV_ClearMessageSystem(void);
```

### `GV_SendMessage`

Pushes onto the *off* buffer (`message_list[1 - which_buffer]`).
Returns -1 if the off buffer is full (16 msgs).

Special behaviour: if a previous message at the same `address`
already exists, the new entry inherits `_len = old._len + 1` and
the old entry is **shifted forward** to keep the run contiguous.
`_len` therefore is the count of currently-pending messages for
that address; the receiver uses it to walk the run.

### `GV_ReceiveMessage`

Reads from the *on* buffer (`message_list[which_buffer]`). Linear
scan for an entry matching the address; returns `_len` (= number of
matching messages) and writes the *first* match's address into
`*msg_ptr` so the caller can iterate `msg[0..len-1]`.

```c
GV_MSG *msgs;
int n = GV_ReceiveMessage(my_addr, &msgs);
for (int i = 0; i < n; i++)
    handle(&msgs[i]);
```

If `GV_PauseLevel != 0`, returns 0 — messages are *not* delivered
during pause / codec, so freezing is automatic for any actor that
guards on receive.

## Address scheme

`GV_MSG::address` is a strcoded chara name: `GV_StrCode("WATCHER")`
returns 0xC356, the canonical watcher address. `enemy/enemy.c` and
many actors compare incoming `msg->message[0]` (the *type*) against
hash constants like `0x430F` ("change route"), `0xF1BD` ("phase
out"), etc.

The hash constants themselves come from `GV_StrCode` of textual
event names — `0x430F = HASH_CHANGE_ROUTE` and similar. Many of
these don't have symbolic names yet; they appear as raw hex in
RootFlagCheck-style switch statements.

## Pitfalls

- **One-frame delay is invariant.** Don't try to "send and immediately
  receive" — the message goes onto the off buffer; only flips next
  frame. If you need same-frame coupling, share state via a global
  or a direct function call, not the message bus.
- **16-message cap per frame.** A burst of >16 messages in one frame
  drops the overflow. Watch for `return -1`. In practice the budget
  is generous (a typical frame uses 2–4) but cinematic alarms can
  spike.
- **Pause swallows messages.** During codec / pause `GV_ReceiveMessage`
  returns 0 even if messages are queued. They stay queued — once
  pause clears, they deliver. So pausing during an alarm doesn't
  lose the alarm (good) but also doesn't deliver it until unpause
  (which is why some actors check both pause level and message
  state).
- **Run-length tracking is fragile.** The `_len` field is updated
  by `GV_SendMessage` itself — if you bypass it and write directly
  to the buffer, runs corrupt.

## Port notes

Unmodified. The message bus runs entirely in software with no PSX-
specific dependency.

## See also

- [actor.md](actor.md) — actors call `GV_ReceiveMessage` from their
  `act` callback.
- [`source/enemy/watcher.c`](../../../../source/enemy/watcher.c)
  `RootFlagCheck_800C3EE8` — canonical example of a message-handler
  switch.
- [`source/libgv/strcode.c`](../../../../source/libgv/strcode.c) —
  the hash function that produces addresses.
