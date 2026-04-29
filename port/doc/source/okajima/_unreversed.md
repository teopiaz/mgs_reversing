---
file: source/okajima/ — opaque areas
---

# `okajima/` — opaque areas

`okajima/` houses Okajima's effects + a few gameplay objects.
Decompiled. Per-actor balance constants and a few object semantics
remain opaque.

## `evntmous.c` / `ductmous.c` / `mouse.c`

Three "mouse" actors:

- `mouse.c` — generic mouse (literal rodent? cursor?).
- `evntmous.c` — "event mouse" — possibly a contextual cursor.
- `ductmous.c` — "duct mouse" — duct-related?

Their gameplay role isn't fully understood. Reverse-engineered code
exists but the user-visible effect needs verification on a real
playthrough.

## `uji.c`

"Uji" (蛆 = maggot). Either a stage-private effect or an Easter-egg
sprite. Not used by gameplay but registered.

## `hiyoko.c`

"Hiyoko" (chick — baby chicken). Easter-egg sprite. Registered as
an actor but the trigger for spawning isn't documented.

## Claymore / key-item lifecycle

`claymore.c` / `key_item.c` are gameplay-active but their state
fields (`field_X`) aren't fully named. Pickups work but the exact
event flow needs auditing.

## `mg_room.c`

"Machine-gun room" — the s11g sprinkler / chain-gun set piece.
Decompiled but the choreography (when guns activate, sprinkler
pattern, etc.) is hard-coded magic.

## See also

- [README.md](README.md), [effects.md](effects.md) (in
  `takabe/`) — documented sister folders.
