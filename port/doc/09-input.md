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

Called once per frame from the main loop. Reads keyboard and gamepad state, combines into `port_pad_buttons`:

```c
static unsigned short port_pad_buttons = 0;
static unsigned char port_pad_lx = 128, port_pad_ly = 128;

void port_update_pad(void) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    unsigned short b = 0;

    // Keyboard mapping
    if (keys[SDL_SCANCODE_UP])      b |= 0x1000;  // BTN_UP
    if (keys[SDL_SCANCODE_DOWN])    b |= 0x4000;  // BTN_DOWN
    if (keys[SDL_SCANCODE_LEFT])    b |= 0x8000;  // BTN_LEFT
    if (keys[SDL_SCANCODE_RIGHT])   b |= 0x2000;  // BTN_RIGHT
    if (keys[SDL_SCANCODE_X])       b |= 0x0040;  // BTN_CROSS
    if (keys[SDL_SCANCODE_Z])       b |= 0x0020;  // BTN_CIRCLE
    // ... etc

    // Gamepad mapping (if connected)
    if (port_controller) {
        // D-pad buttons
        // Face buttons (A=Cross, B=Circle, Y=Triangle, X=Square)
        // Shoulders (LB=L1, RB=R1)
        // Triggers: axis > 8000 threshold -> L2/R2

        // Left stick -> analog values
        Sint16 lx = SDL_GameControllerGetAxis(..., SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ly = SDL_GameControllerGetAxis(..., SDL_CONTROLLER_AXIS_LEFTY);
        port_pad_lx = (unsigned char)((lx + 32768) >> 8);  // map -32768..32767 to 0..255
        port_pad_ly = (unsigned char)((ly + 32768) >> 8);

        // Digital d-pad from stick (16000 deadzone)
        if (lx < -16000) b |= BTN_LEFT;
        if (lx >  16000) b |= BTN_RIGHT;
        if (ly < -16000) b |= BTN_UP;
        if (ly >  16000) b |= BTN_DOWN;
    }

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

### Complete Button Mapping Table

| PSX Button | Hex    | Keyboard         | SDL Scancode           | Gamepad                  |
|------------|--------|------------------|------------------------|--------------------------|
| L2         | 0x0001 | 1                | SDL_SCANCODE_1         | Left Trigger (>8000)     |
| R2         | 0x0002 | 3                | SDL_SCANCODE_3         | Right Trigger (>8000)    |
| L1         | 0x0004 | Q                | SDL_SCANCODE_Q         | Left Shoulder            |
| R1         | 0x0008 | E                | SDL_SCANCODE_E         | Right Shoulder           |
| Triangle   | 0x0010 | S                | SDL_SCANCODE_S         | Y (north)                |
| Circle     | 0x0020 | Z                | SDL_SCANCODE_Z         | B (east)                 |
| Cross      | 0x0040 | X                | SDL_SCANCODE_X         | A (south)                |
| Square     | 0x0080 | A                | SDL_SCANCODE_A         | X (west)                 |
| Select     | 0x0100 | Backspace        | SDL_SCANCODE_BACKSPACE | Back                     |
| L3         | 0x0200 | (unmapped)       | --                     | Left Stick Click         |
| R3         | 0x0400 | (unmapped)       | --                     | Right Stick Click        |
| Start      | 0x0800 | Enter/Return     | SDL_SCANCODE_RETURN    | Start                    |
| D-Up       | 0x1000 | Arrow Up         | SDL_SCANCODE_UP        | D-Pad Up / LStick Up     |
| D-Right    | 0x2000 | Arrow Right      | SDL_SCANCODE_RIGHT     | D-Pad Right / LStick Right|
| D-Down     | 0x4000 | Arrow Down       | SDL_SCANCODE_DOWN      | D-Pad Down / LStick Down |
| D-Left     | 0x8000 | Arrow Left       | SDL_SCANCODE_LEFT      | D-Pad Left / LStick Left |

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
