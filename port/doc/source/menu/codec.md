---
file: source/menu/{radio,radioanim,radiofacedraw,radiomem,radiomes,radiotable,radiotex}.c + face.h
---

# `menu/radio*` — codec system

The radio-call (codec) system: voice-acted dialogue with animated
portraits, branching conversation, frequency-based contact list.

## Architectural overview

```
RADIO actor (radio.c)
   ↓
  +─ orchestrator: pick conversation, manage state
  +─ radioanim.c : per-frame face animation
  +─ radiofacedraw.c : portrait sprite submission
  +─ radiomes.c : subtitle text reveal
  +─ radiomem.c : "remember this freq" UI
  +─ radiotable.c : contact directory
  +─ radiotex.c : face texture management
```

All seven files cooperate; together they're "the codec".

## Files

### `radio.c`

The orchestrator. Owns the RADIO chara actor (registered with
`MainCharacterEntries`).

State machine:

```
IDLE → RING (incoming call) → CONNECTED → SPEAKING → END
                  ↑                           ↓
                  └── player dials freq ──────┘
```

- `RING`: the "kshhhk" sound + ring icon overlay.
- `CONNECTED`: portrait scrolls in from right.
- `SPEAKING`: dialogue text + lip-flap animation.
- `END`: portrait scrolls out + return to gameplay.

Each line of dialogue triggers a `radio.dat` script entry (see
`source/libgcl/bytecode.md` for opcode list).

### `radioanim.c` — face animation

Per-frame animation of the active speaker's face:

- Lip-flap: read voice amplitude → select mouth-open variant.
- Eye blink: random; ~once per few seconds.
- Mood: `radio.dat` `RDCODE_ANIM` opcodes set state (angry,
  worried, etc.) → swap face texture set.

### `radiofacedraw.c` — portrait submission

Builds the portrait quad each frame:

- Read current speaker's `face` struct.
- Construct DG_PRIM textured quad in `DG_Chanl(2)` (codec channel).
- Apply scroll-in / scroll-out animation.

### `radiomes.c` — subtitle reveal

Types out the subtitle character-by-character as the voice plays:

- Read text string + duration from `radio.dat`.
- Per-frame, advance a "characters revealed" counter at the rate
  `(strlen / duration)`.
- Render each revealed character via the `font/` rasteriser.

### `radiomem.c` — "remember frequency" UI

The "press SELECT to remember this frequency" prompt that lets the
player save a contact's freq for future calls.

- Triggered by `RDCODE_MEMSAVE` opcode in radio.dat.
- Renders a small overlay; player presses SELECT or skips.
- On accept, calls `radiotable.c::AddContact(freq)`.

### `radiotable.c` — contact directory

Stores and queries the player's known frequencies:

```c
typedef struct CONTACT {
    short freq;          // BCD-encoded frequency
    short name_id;       // strcoded name
    short flags;
} CONTACT;
extern CONTACT contacts[16];
```

When the player opens the codec menu, the directory is shown. New
contacts come from `RDCODE_ADD_CONTACT` script ops or saved freqs.

### `radiotex.c` — face texture management

Each speaker has multiple face textures (neutral / talking / angry
/ etc.). `radiotex.c` manages loading them:

- At codec start, identify the active speaker.
- Load that speaker's face texture set from `face.dat`.
- During the call, swap textures based on `radio.dat` mood opcodes.
- On end, unload (free the texture slots).

`face.h` defines the FACE struct.

## Voice playback

Voices are `.vox` files (sample-streamed audio). `radio.c`
coordinates with `sound/` — when a `RDCODE_VOICE` is encountered:

```
GM_PlayVox(vox_id)  → sound/sd_str.c streams from CD
                       (or RAM in port)
```

The voice's amplitude envelope is fed to `radioanim.c` for lip-flap.

## Port notes

The port had a significant codec bug that was fixed (see
[codec_radio_issue.md](#) memory):

- `mts_sta_tsk` was a no-op stub on the port.
- `FACE.DAT` parser had a 32→64-bit bug.

Both were fixed; the codec now runs end-to-end.

## Pitfalls

- **The codec is high-priority.** It runs at `GV_ACTOR_LEVEL2` and
  consumes input even during pause. Setting `GV_PauseLevel` for
  game-pause doesn't pause codec.
- **Voice + subtitle sync depends on accurate VOX playback.** If
  the VOX is short-played (CD fault, or port stub), subtitles
  out-pace the voice.
- **Face texture set count is bounded.** Loading too many speakers
  can exhaust the resident texture cache.

## See also

- [`source/sound/`](../sound/README.md) — VOX streaming.
- [`source/libgcl/bytecode.md`](../libgcl/bytecode.md) — radio.dat
  opcode space.
- [`source/font/`](../font/README.md) — text rendering.
