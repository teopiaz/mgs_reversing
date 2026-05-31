# Input System

## PSX Controller Hardware

On the PlayStation, controllers communicate via the SIO (Serial I/O) interface. The MTS kernel polls the controller hardware registers each vblank and stores the result in an `MTS_PAD` struct:

```c
typedef struct {
    short          flag;      // controller type (DIGITAL=0, ANALOG=2)
    unsigned short button;    // button state, ACTIVE-LOW (0 = pressed)
    unsigned char  rx;        // right stick X (128 = center)
    unsigned char  ry;        // right stick Y (128 = center)
    unsigned char  lx;        // left stick X (128 = center)
    unsigned char  ly;        // left stick Y (128 = center)
} MTS_PAD;
```

Key detail: **PSX buttons are active-low**. A bit value of 0 means the button IS pressed. This is the raw hardware convention. The engine inverts this at a higher level.

### PSX Button Bit Layout

```
Bit 0  (0x0001): L2
Bit 1  (0x0002): R2
Bit 2  (0x0004): L1
Bit 3  (0x0008): R1
Bit 4  (0x0010): Triangle
Bit 5  (0x0020): Circle
Bit 6  (0x0040): Cross (X)
Bit 7  (0x0080): Square
Bit 8  (0x0100): Select
Bit 9  (0x0200): L3 (left stick click)
Bit 10 (0x0400): R3 (right stick click)
Bit 11 (0x0800): Start
Bit 12 (0x1000): D-Pad Up
Bit 13 (0x2000): D-Pad Right
Bit 14 (0x4000): D-Pad Down
Bit 15 (0x8000): D-Pad Left
```

## Port Input Layer

### Data Flow

```
SDL_GetKeyboardState() + SDL_GameController
        |
        v
port_update_pad()  -->  port_pad_buttons (active-HIGH, u16)
                        port_pad_lx, port_pad_ly (0-255, 128=center)
        |
        v
mts_PadRead(0)     -->  returns port_pad_buttons (active-HIGH)
mts_get_pad(0,pad) -->  fills MTS_PAD { flag, button=~port_pad_buttons, lx, ly }
        |
        v
GV_UpdatePadSystem()
        |
        v
GV_PadData[0]  -->  { status, press, release, quick, dir, analog, left_dx/dy }
        |
        v
GM_CurrentPadData = GV_PadData  (game reads this)
```

### port_update_pad() (port/mts/mts.c)

Called once per frame from the main loop. Both keyboard and gamepad
mapping are driven by `g_port_config.kb_map[]` / `g_port_config.pad_map[]`
(see `port_config.h`) so the user can remap from the pre-game Controls
page. Defaults match the historical hard-coded table.

```c
static unsigned short port_pad_buttons = 0;
static unsigned char port_pad_lx = 128, port_pad_ly = 128;

static const unsigned short s_btn_bits[PORT_BTN_COUNT] = {
    [PORT_BTN_UP]    = BTN_UP,   [PORT_BTN_DOWN]  = BTN_DOWN,
    [PORT_BTN_LEFT]  = BTN_LEFT, [PORT_BTN_RIGHT] = BTN_RIGHT,
    [PORT_BTN_CROSS] = BTN_CROSS, /* …face buttons, shoulders, start, select */
};

void port_update_pad(void) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    unsigned short b = 0;

    /* Keyboard mapping — table-driven via the user-configurable kb_map. */
    for (int i = 0; i < PORT_BTN_COUNT; i++) {
        int sc = g_port_config.kb_map[i];
        if (sc >= 0 && sc < 512 && keys[sc]) b |= s_btn_bits[i];
    }

    /* Gamepad mapping — also driven by the user's pad_map. Triggers use
     * the analog axis when the pad_map entry is left at PORT_PAD_UNBOUND. */
    if (port_controller) {
        for (int i = 0; i < PORT_BTN_COUNT; i++) {
            int pb = g_port_config.pad_map[i];
            if (pb >= 0 && SDL_GameControllerGetButton(port_controller, pb))
                b |= s_btn_bits[i];
        }
        if (g_port_config.pad_map[PORT_BTN_L2] < 0 &&
            SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)
            b |= BTN_L2;
        if (g_port_config.pad_map[PORT_BTN_R2] < 0 &&
            SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000)
            b |= BTN_R2;

        /* Analog stick — but ONLY when no digital d-pad bit is already set.
         * See the "Switch Pro Controller fix" section below. */
        if (!(b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) {
            Sint16 lx = SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_LEFTX);
            Sint16 ly = SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_LEFTY);
            port_pad_lx = (unsigned char)((lx + 32768) >> 8);
            port_pad_ly = (unsigned char)((ly + 32768) >> 8);
            if (lx < -16000) b |= BTN_LEFT;
            if (lx >  16000) b |= BTN_RIGHT;
            if (ly < -16000) b |= BTN_UP;
            if (ly >  16000) b |= BTN_DOWN;
        } else {
            port_pad_lx = 128;
            port_pad_ly = 128;
        }
    } else {
        port_pad_lx = 128;
        port_pad_ly = 128;
    }

    /* Overlay d-pad bits onto the analog channel. The engine's
     * GV_AnalogToDirection rebuilds UDLR from the analog stick whenever
     * the pad reports MTS_PAD_ANALOG, so a digital press has to reach
     * the engine through lx/ly to survive. */
    if (b & BTN_LEFT)  port_pad_lx = 0;
    if (b & BTN_RIGHT) port_pad_lx = 255;
    if (b & BTN_UP)    port_pad_ly = 0;
    if (b & BTN_DOWN)  port_pad_ly = 255;

    port_pad_buttons = b;  // stored as ACTIVE-HIGH
}
```

### mts_PadRead() and mts_get_pad()

These are called by the engine:

```c
long mts_PadRead(int unused) {
    return port_pad_buttons;  // active-HIGH
}

int mts_get_pad(int channel, MTS_PAD *pad) {
    memset(pad, 0, sizeof(*pad));
    if (port_controller) {
        pad->flag = MTS_PAD_ANALOG;    // 2
        pad->lx = port_pad_lx;
        pad->ly = port_pad_ly;
    } else {
        pad->flag = MTS_PAD_DIGITAL;   // 0
        pad->lx = 128;  // center
        pad->ly = 128;
    }
    pad->button = ~port_pad_buttons;  // convert to ACTIVE-LOW for PSX convention
    pad->rx = 128;
    pad->ry = 128;
    return 1;  // controller present
}
```

### GV_UpdatePadSystem() (source/libgv/pad.c)

The original engine code processes raw pad data into the `GV_PAD` structure:

```c
typedef struct {
    u_short status;      // buttons currently held (active-HIGH)
    u_short press;       // buttons pressed this frame (edge-detect)
    u_short release;     // buttons released this frame
    u_short quick;       // quick-tap detection
    short   dir;         // computed direction angle (0-4095, PSX angle units)
    short   analog;      // analog mode flag
    u_char  right_dx;    // right stick delta X
    u_char  right_dy;    // right stick delta Y
    u_char  left_dx;     // left stick delta X
    u_char  left_dy;     // left stick delta Y
} GV_PAD;
```

The processing pipeline:

1. `mts_PadRead(0)` returns raw active-high buttons
2. `GV_ConvertButtonMode()` remaps buttons based on config (button mode A/B)
3. Check `DG_UnDrawFrameCount` -- if nonzero, skip pad update (loading screen)
4. Check `GM_GameStatus` flags:
   - `STATE_PADRELEASE` (0x40000): Clear all buttons (cutscene playback)
   - `STATE_PADMASK` (0x800000): Apply `GV_PadMask` to filter buttons
   - `STATE_PADDEMO` (0x200): Use demo playback data instead of real input
5. Compute `press` (newly pressed) and `release` (newly released) from `status` delta
6. Compute `dir` from d-pad or analog stick via `GV_AnalogToDirection()`

### Default Button Mapping (config-driven)

The table below is the factory default installed by
`port_config_set_defaults` (`port/port_config.c`). Every entry is
remappable from the pre-game Controls page and persisted to
`./port_config.ini` as raw SDL enum integers. The PSX bit column is the
hardware-defined position in `MTS_PAD.button`.

| PSX Button | Hex    | Default Keyboard | Default Gamepad          |
|------------|--------|------------------|--------------------------|
| L2         | 0x0001 | `1`              | Left Trigger (axis > 8000)|
| R2         | 0x0002 | `3`              | Right Trigger (axis > 8000)|
| L1         | 0x0004 | `Q`              | Left Shoulder            |
| R1         | 0x0008 | `E`              | Right Shoulder            |
| Triangle   | 0x0010 | `S`              | Y (north)                |
| Circle     | 0x0020 | `Z`              | B (east)                 |
| Cross      | 0x0040 | `X`              | A (south)                |
| Square     | 0x0080 | `A`              | X (west)                 |
| Select     | 0x0100 | `Backspace`      | Back                     |
| L3         | 0x0200 | (unmapped)       | Left Stick Click         |
| R3         | 0x0400 | (unmapped)       | Right Stick Click        |
| Start      | 0x0800 | `Return`         | Start                    |
| D-Up       | 0x1000 | `Up`             | DPAD_UP                  |
| D-Right    | 0x2000 | `Right`          | DPAD_RIGHT               |
| D-Down     | 0x4000 | `Down`           | DPAD_DOWN                |
| D-Left     | 0x8000 | `Left`           | DPAD_LEFT                |

### Config File Format

The Controls page writes raw SDL enum integers to `port_config.ini` so
the file is portable across SDL versions (the names are decoded only for
display via `SDL_GetScancodeName` / `SDL_GameControllerGetStringForButton`).

```ini
[keyboard] ; values are SDL_Scancode integers
up       = 82  ; Up
down     = 81  ; Down
left     = 80  ; Left
right    = 79  ; Right
cross    = 27  ; X
circle   = 29  ; Z
…

[gamepad] ; values are SDL_GameControllerButton ints; -1 = unbound (triggers used for L2/R2)
up       = 11  ; dpup
down     = 12  ; dpdown
…
```

`PortButton` is an enum in `port_config.h` whose order is stable; new
buttons must go at the end before `PORT_BTN_COUNT`. Both arrays are
indexed by `PortButton`. `-1` (i.e. `PORT_PAD_UNBOUND`) on a gamepad
slot routes L2/R2 through the trigger-axis path; the same value on the
keyboard slot means the button is unbound.

## Gamepad Support

### Controller Detection

```c
static SDL_GameController *port_controller = NULL;

void port_open_controller(void) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            port_controller = SDL_GameControllerOpen(i);
            if (port_controller) {
                printf("[pad] opened controller: %s\n",
                       SDL_GameControllerName(port_controller));
                break;
            }
        }
    }
}
```

Called at startup and on `SDL_CONTROLLERDEVICEADDED` events (hotplug).

### Analog Stick Processing

The left stick is mapped to both analog values and digital d-pad:

```c
// Analog: SDL range (-32768..32767) -> PSX range (0..255, 128=center)
port_pad_lx = (unsigned char)((lx + 32768) >> 8);
port_pad_ly = (unsigned char)((ly + 32768) >> 8);

// Digital: deadzone of 16000 (out of 32767)
if (lx < -16000) b |= BTN_LEFT;
if (lx >  16000) b |= BTN_RIGHT;
if (ly < -16000) b |= BTN_UP;
if (ly >  16000) b |= BTN_DOWN;
```

When no controller is connected, keyboard d-pad sets analog to extremes:

```c
port_pad_lx = 128;
port_pad_ly = 128;
if (b & BTN_LEFT)  port_pad_lx = 0;
if (b & BTN_RIGHT) port_pad_lx = 255;
if (b & BTN_UP)    port_pad_ly = 0;
if (b & BTN_DOWN)  port_pad_ly = 255;
```

### Trigger-to-Button Mapping

SDL triggers are analog axes (0-32767). The port uses a threshold of 8000:

```c
if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)
    b |= BTN_L2;
if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000)
    b |= BTN_R2;
```

## Bugs Fixed

### 1. signed char analog center (128 -> -128)

**Problem**: `MTS_PAD.lx` and `MTS_PAD.ly` are `unsigned char` (0-255, center=128). However, `GV_AnalogToDirection()` computes direction from analog stick position. On PSX, `signed char` center (128) wraps to -128, but the digital pad mode checks prevented this path. In the port, if the controller reported analog mode with lx=128, ly=128, the direction computation treated 128 as -128, producing a phantom UP+LEFT input.

**Fix**: When no physical gamepad is connected, report `MTS_PAD_DIGITAL` flag instead of `MTS_PAD_ANALOG`. The engine skips analog direction computation for digital pads.

```c
if (port_controller) {
    pad->flag = MTS_PAD_ANALOG;
    pad->lx = port_pad_lx;
    pad->ly = port_pad_ly;
} else {
    pad->flag = MTS_PAD_DIGITAL;  // prevents GV_AnalogToDirection
    pad->lx = 128;
    pad->ly = 128;
}
```

### 2. DG_HikituriFlagOld was a function stub

**Problem**: `DG_HikituriFlagOld` was accidentally declared as a function stub instead of an `int` variable. The rendering code checks `if (DG_HikituriFlagOld != DG_HikituriFlag)` to detect frame-buffer swap state changes. With the stub, `DG_DrawOTag()` was never called because the condition was always false (comparing function address to int).

**Fix**: Declared `DG_HikituriFlagOld` as a proper `int` variable. In `game_tick()`, manually update it each frame:

```c
DG_HikituriFlagOld = DG_HikituriFlag;
DG_HikituriFlag = 0;
```

### 3. GM_GameStatus STATE_PADRELEASE never cleared

**Problem**: `GM_GameStatus` flag `STATE_PADRELEASE` (0x40000) is set during stage opening sequences (cutscenes, camera pans). On PSX, the game clears this flag when transitioning to gameplay state. In the port, the flag was never cleared, causing `GV_UpdatePadSystem` to zero all button input permanently after the first stage load.

**Fix**: Added a `PORT_BUILD` guard in the game status processing that clears `STATE_PADRELEASE` when the game reaches `WORKING` state:

```c
#ifdef PORT_BUILD
    // Clear PADRELEASE when transitioning to gameplay
    if (game_state == GM_STATE_WORKING) {
        GM_GameStatus &= ~STATE_PADRELEASE;
    }
#endif
```

### 4. Switch Pro Controller DPAD_LEFT reported as DOWN

**Problem**: The Nintendo Switch Pro Controller's SDL mapping pipes its
d-pad HAT through both `SDL_CONTROLLER_BUTTON_DPAD_*` AND the LEFTX/LEFTY
axes. Pressing DPAD_LEFT correctly produced `BTN_LEFT`, but also reported
`AXIS_LEFTY = +32767`. Our analog-stick fallback then ORed in `BTN_DOWN`
(via `ly > 16000`) and `mts_get_pad` always reports `MTS_PAD_ANALOG`
whenever a controller is connected — so `GV_AnalogToDirection` cleared
the UDLR bits and rebuilt them purely from the analog stick (`ly = 255`
→ `PAD_DOWN`). Net result: every LEFT press reached the game as DOWN.

**Fix**: Skip the analog-stick read entirely when any d-pad bit is
already set in `b` (from keyboard arrow or gamepad DPAD button). The
d-pad-to-analog overlay below then paints `lx/ly` from the digital
bits, so the engine sees the same direction it would have read from a
clean analog stick — without the HAT-to-axis pollution. Implementation
at the top of `port_update_pad()` in `port/mts/mts.c`. Same change also
makes keyboard arrows work when a controller is also plugged in (they
used to be silently dropped for the same reason).

### 5. d-pad presses lost because the engine reads analog only

**Problem**: `mts_get_pad` reports `MTS_PAD_ANALOG` whenever a controller
is present, so `GV_AnalogToDirection` clears the UDLR bits from
`pad->button` and rebuilds them from `pad->lx / pad->ly`. Any d-pad
input that doesn't also move the stick (keyboard, gamepad DPAD) was
silently dropped — `lx, ly` stayed at center, `dir = 0`.

**Fix**: After deciding the analog channel, force-overlay the d-pad
bits onto `port_pad_lx/ly`. If `BTN_LEFT` is set, write `port_pad_lx =
0`; `BTN_RIGHT` → 255; `BTN_UP` → 0 on ly; `BTN_DOWN` → 255. The engine
then derives the same direction it would have from an actual stick
push. Runs in both the controller-connected and keyboard-only branches.

## Auto-Input System

For headless testing, the port supports a scripted button-press sequence:

```bash
MGS_AUTO_INPUT=1 ./mgs
```

When enabled, `port_update_pad()` overlays pre-programmed button presses at specific frame numbers:

```c
struct { int frame; unsigned short btn; } script[] = {
    {  60, BTN_DOWN   },  // Menu: move cursor down
    { 120, BTN_CIRCLE },  // Menu: confirm selection
    { 180, BTN_CIRCLE },  // Disc select: confirm
    { 240, BTN_CIRCLE },  // Opening: skip
    { 300, BTN_DOWN   },  // Stage select: move down
    { 360, BTN_DOWN   },  // Stage select: move down
    { 420, BTN_CIRCLE },  // Stage select: confirm (loads s00a)
};
```

Each button press lasts 5 frames. This navigates the title screen menus and loads the first gameplay stage (`s00a`) automatically, useful for CI/automated testing.

## Debug Keys

These are handled in `port_poll_events()` in `main.c`, outside the game's input system:

| Key    | Action                                              |
|--------|-----------------------------------------------------|
| Escape | Quit                                                |
| Tab    | Toggle VRAM debug view (full 1024x512 VRAM display) |
| P      | Toggle ImGui actor inspector overlay                |
| W/A/S/D| Free-fly camera movement (dev only)                 |
| Arrows | Free-fly camera rotation (dev only, via `port_keys[]`) |
| Shift  | Fast camera movement (400 units/frame vs 100)       |
| Q/E    | Camera vertical movement (down/up)                  |

The free-fly camera reads `port_keys[]` (raw SDL scancode array) in `update_camera()` and directly sets `DG_Chanls[1]` (3D channel) view matrix via `DG_LookAt()`.

## Raw Keyboard State

`port_keys[512]` is a copy of `SDL_GetKeyboardState()` updated each frame. It uses SDL scancodes (not keycodes). This array is exported globally for the free-fly camera and any other code that needs direct keyboard access outside the PSX pad abstraction.

```c
unsigned char port_keys[512];  // indexed by SDL_SCANCODE_*

// Updated each frame:
const Uint8 *keys = SDL_GetKeyboardState(NULL);
memcpy(port_keys, keys, 512);
```
