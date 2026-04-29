---
file: source/libgv/pad.c
---

# `libgv/pad.c` — controller input

Reads PSX pad data via the MTS pad task and exposes a buttons-edges
+ analog struct (`GV_PAD`) per controller.

## `GV_PAD` struct

```c
typedef struct {
    u_short  status;        // currently-held buttons
    u_short  press;         // edge: pressed-this-frame (status & ~prev_status)
    u_short  release;       // edge: released-this-frame
    u_short  quick;         // tap: pressed and released within N frames
    short    dir;           // digital direction encoded as 0x0xx0
    short    analog;        // analog stick mode (DIGITAL/ANAJOY/ANALOG)
    u_char   right_dx;      // analog right stick X (0..255, 128 = centre)
    u_char   right_dy;
    u_char   left_dx;
    u_char   left_dy;
} GV_PAD;
extern GV_PAD GV_PadData[4];           // 4 controllers
```

`GV_PadData[0]` is player 1, [1] is player 2 (multitap supported in
some VR modes).

## Button name aliases

`pad.c` exposes both **PSX button names** and **SNES-style aliases**
for code clarity. Same physical button:

| PSX | SNES alias | Mask | Symbol |
| --- | ---------- | ---- | ------ |
| ○ Circle | A | 0x0020 | `PAD_CIRCLE` / `PAD_A` |
| × Cross | B | 0x0040 | `PAD_CROSS` / `PAD_B` |
| △ Triangle | X | 0x0010 | `PAD_TRIANGLE` / `PAD_X` |
| □ Square | Y | 0x0080 | `PAD_SQUARE` / `PAD_Y` |
| L1/L2/R1/R2 | (same) | 0x0004/0x0001/0x0008/0x0002 | `PAD_L1..R2` |
| L3/R3 | AL/AR | 0x0200/0x0400 | `PAD_AL`/`PAD_AR` |
| START / SELECT | (same) | 0x0800/0x0100 | `PAD_STA`/`PAD_SEL` |
| ↑↓←→ | UDLR | 0x1000/0x4000/0x8000/0x2000 | `PAD_U..R` |

The convention varies per author — `chara/snake/` uses PSX names,
`menu/` mostly uses SNES. Both work.

## `dir_table[16]` — D-pad → angle

Maps the 4-bit (UP, RIGHT, DOWN, LEFT) combination to a packed
direction angle (`0x0000`..`0x0E00`):

```
  Combo                Angle (12-bit fixed)
  ---------            -----
  UP                   0x0800   (up)
  UP+RIGHT             0x0600
  RIGHT                0x0400
  DOWN+RIGHT           0x0200
  DOWN                 0x0000
  DOWN+LEFT            0x0E00
  LEFT                 0x0C00
  UP+LEFT              0x0A00
  invalid combos       0x0000
```

Used by `GV_GetPadDirNoPadOrg` to feed a directional angle straight
into the player movement code.

## Button-mode swap

PSX MGS lets the player remap CIRCLE/CROSS/SQUARE in the options
menu (Type A / B / C). `GV_ConvertButtonMode` runs every pad read to
remap incoming hardware buttons:

| Mode | Swap |
| ---- | ---- |
| A | (none) — default |
| B | CIRCLE ↔ CROSS |
| C | CIRCLE ↔ SQUARE |

The same swap is applied to `press` and `release` (hence the
`for i = 1; i >= 0` loop swapping with `<<= 16`).

## Analog mode handling

Three states per controller:

```c
enum {
    GV_PAD_DIGITAL = 0,
    GV_PAD_ANAJOY  = 1,    // analog joystick (NeGcon-style)
    GV_PAD_ANALOG  = 2,    // dual-shock analog
};
```

`GV_AnalogToDirection` collapses analog stick deflection into a
synthetic D-pad press if the user's pad is in analog mode but the
game code wants digital input (most of MGS).

## VR pad emulation

Gated on `#ifdef VR_EXE`. `sub_800165B0` reads the `GV_DemoPadStatus`
demo recording and synthesises an MTS_PAD packet, so the demo
playback can drive Snake from a recorded button sequence. Used
during VR Missions tutorials.

## Pad mask

```c
extern int GV_PadMask;
void GV_OriginPadSystem(int);
int  GV_GetPadOrigin(void);
```

`GV_PadMask` zeroes specific buttons globally — used by codec to
prevent gameplay buttons from leaking through during a call.
`GV_OriginPadSystem` snapshots the current state as "origin", so
edge detection (`press`/`release`) ignores buttons already held when
control transferred (e.g. when a cinematic ends with a button still
held from the cinematic skip).

## Public API

```c
void GV_InitPadSystem(void);
void GV_UpdatePadSystem(void);          // called once per frame from gamed.c
void GV_OriginPadSystem(int idx);
int  GV_GetPadOrigin(void);
int  GV_GetPadDirNoPadOrg(unsigned int pad_idx);
```

## Pitfalls

- **Edge fields are pre-mask but post-button-mode.** Reading
  `pad->press` gives you the remapped (CIRCLE/CROSS swapped if Type
  B) edge but *not* the masked variant. If you set `GV_PadMask`
  globally, masked buttons still appear in `press`/`release`.
- **Analog stick "centre" is 128, not 0.** Subtract before
  thresholding.
- **The dir field is 12-bit fixed.** Combine with `rsin/rcos` like
  any GV angle, not in degrees.

## Port notes

The port replaces the MTS pad task with SDL key-state polling.
[port/pad.c](../../../../port/pad.c) translates SDL events into
`MTS_PAD` packets so the rest of `pad.c` is unchanged.

## See also

- [`source/menu/menuman.c`](../../../../source/menu/menuman.c) —
  pause-menu driving consumer.
- [`source/chara/snake/`](../chara/snake.md) — main gameplay
  consumer.
