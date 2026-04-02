/**
 * Port MTS implementation — single-threaded replacement.
 * All task scheduling is replaced with direct function calls.
 */

#define __IN_MTS_NEW__

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "mts.h"
#include "mts_new.h"
#include "mts_pad.h"

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

static int current_task_id = 0;
static unsigned int tick_count = 0;
static void (*exception_func)(void) = NULL;
static void (*vsync_control_func)(void) = NULL;
static int (*vsync_callback_func)(void) = NULL;

/*---------------------------------------------------------------------------*/
/* Task management — simplified single-threaded stubs                        */
/*---------------------------------------------------------------------------*/

void mts_boot(int tasknr, void (*procedure)(void), void *stack_pointer)
{
    (void)stack_pointer;
    current_task_id = tasknr;
    procedure();
}

void mts_boot_task(int tasknr, void (*procedure)(void), void *stack_pointer, long stack_size)
{
    (void)stack_size;
    mts_boot(tasknr, procedure, stack_pointer);
}

int mts_sta_tsk(int tasknr, void (*procedure)(void), void *stack_pointer)
{
    (void)tasknr;
    (void)procedure;
    (void)stack_pointer;
    /* In the real MTS this starts a concurrent task.
       For the port, tasks that need to run (like sound) are stubbed. */
    return 0;
}

void mts_ext_tsk(void)
{
    /* Exit current task — no-op in single-threaded mode */
}

void mts_shutdown(void)
{
}

void mts_task_start(void)
{
}

int mts_get_current_task_id(void)
{
    return current_task_id;
}

int mts_get_task_status(long id)
{
    (void)id;
    return MTS_TASK_READY;
}

/*---------------------------------------------------------------------------*/
/* Messaging — stubs                                                         */
/*---------------------------------------------------------------------------*/

void mts_send(int dst, unsigned char *message)
{
    (void)dst;
    (void)message;
}

int mts_isend(int dst)
{
    (void)dst;
    return 0;
}

int mts_receive(int src, unsigned char *message)
{
    (void)src;
    (void)message;
    return -1;
}

void mts_send_msg(int dst, int data0, int data1)
{
    (void)dst;
    (void)data0;
    (void)data1;
}

int mts_recv_msg(int src, int *data0, int *data1)
{
    (void)src;
    (void)data0;
    (void)data1;
    return -1;
}

/*---------------------------------------------------------------------------*/
/* Sleep / Wake — stubs                                                      */
/*---------------------------------------------------------------------------*/

void mts_slp_tsk(void) {}
void mts_wup_tsk(int dst) { (void)dst; }

/*---------------------------------------------------------------------------*/
/* Semaphores — stubs (single-threaded, no contention)                       */
/*---------------------------------------------------------------------------*/

void mts_lock_sem(int no) { (void)no; }
void mts_unlock_sem(int no) { (void)no; }

/*---------------------------------------------------------------------------*/
/* V-Sync / Timing                                                           */
/*---------------------------------------------------------------------------*/

void mts_init_vsync(void)
{
    tick_count = 0;
}

void mts_set_vsync_task(void)
{
}

void mts_set_vsync_callback_func(int (*func)(void))
{
    vsync_callback_func = func;
}

void mts_set_vsync_control_func(void (*func)(void))
{
    vsync_control_func = func;
}

int mts_wait_vbl(long count)
{
    /* In a full port this would SDL_Delay for count/60 seconds.
       For now just increment tick count. */
    tick_count += (unsigned int)count;
    return 0;
}

int mts_get_tick_count(void)
{
    return (int)tick_count;
}

/*---------------------------------------------------------------------------*/
/* Interrupt                                                                 */
/*---------------------------------------------------------------------------*/

void mts_reset_interrupt_wait(int id) { (void)id; }
void mts_reset_interrupt_overrun(void) {}

/*---------------------------------------------------------------------------*/
/* Debug / Stack check                                                       */
/*---------------------------------------------------------------------------*/

void mts_set_stack_check(long tasknr, void *stack_top, long stack_size)
{
    (void)tasknr;
    (void)stack_top;
    (void)stack_size;
}

void mts_print_process_status(void)
{
    printf("[port] mts_print_process_status: single-threaded mode\n");
}

void mts_set_exception_func(void (*func)(void))
{
    exception_func = func;
}

void mts_get_use_stack_size(int *max, int *now, int *limit)
{
    if (max) *max = 0;
    if (now) *now = 0;
    if (limit) *limit = 0;
}

/*---------------------------------------------------------------------------*/
/* Controller — SDL keyboard mapped to PSX pad buttons                       */
/*---------------------------------------------------------------------------*/

#include <SDL.h>

/* PSX button bits (active-high for our mapping) */
#define BTN_UP      0x1000
#define BTN_DOWN    0x4000
#define BTN_LEFT    0x8000
#define BTN_RIGHT   0x2000
#define BTN_CROSS   0x0040
#define BTN_CIRCLE  0x0020
#define BTN_TRIANGLE 0x0010
#define BTN_SQUARE  0x0080
#define BTN_L1      0x0004
#define BTN_L2      0x0001
#define BTN_R1      0x0008
#define BTN_R2      0x0002
#define BTN_START   0x0800
#define BTN_SELECT  0x0100

static unsigned short port_pad_buttons = 0;
static unsigned char port_pad_lx = 128, port_pad_ly = 128;

/* Exported raw keyboard state for camera controls etc. */
unsigned char port_keys[512] = {0};

/* SDL game controller */
static SDL_GameController *port_controller = NULL;

void port_open_controller(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            port_controller = SDL_GameControllerOpen(i);
            if (port_controller)
            {
                printf("[pad] opened controller: %s\n", SDL_GameControllerName(port_controller));
                break;
            }
        }
    }
}

/* Called from main.c event loop */
void port_update_pad(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    /* Copy first 512 scancodes for external use */
    memcpy(port_keys, keys, 512);
    unsigned short b = 0;

    /* Keyboard mapping */
    if (keys[SDL_SCANCODE_UP])      b |= BTN_UP;
    if (keys[SDL_SCANCODE_DOWN])    b |= BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT])    b |= BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])   b |= BTN_RIGHT;
    if (keys[SDL_SCANCODE_X])       b |= BTN_CROSS;
    if (keys[SDL_SCANCODE_Z])       b |= BTN_CIRCLE;
    if (keys[SDL_SCANCODE_S])       b |= BTN_TRIANGLE;
    if (keys[SDL_SCANCODE_A])       b |= BTN_SQUARE;
    if (keys[SDL_SCANCODE_Q])       b |= BTN_L1;
    if (keys[SDL_SCANCODE_1])       b |= BTN_L2;
    if (keys[SDL_SCANCODE_E])       b |= BTN_R1;
    if (keys[SDL_SCANCODE_3])       b |= BTN_R2;
    if (keys[SDL_SCANCODE_RETURN])  b |= BTN_START;
    if (keys[SDL_SCANCODE_BACKSPACE]) b |= BTN_SELECT;

    /* Gamepad mapping */
    if (port_controller)
    {
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= BTN_UP;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= BTN_DOWN;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= BTN_LEFT;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= BTN_RIGHT;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_A))          b |= BTN_CROSS;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_B))          b |= BTN_CIRCLE;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_Y))          b |= BTN_TRIANGLE;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_X))          b |= BTN_SQUARE;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= BTN_L1;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= BTN_R1;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_START))      b |= BTN_START;
        if (SDL_GameControllerGetButton(port_controller, SDL_CONTROLLER_BUTTON_BACK))       b |= BTN_SELECT;

        /* Triggers as L2/R2 */
        if (SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)  b |= BTN_L2;
        if (SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) b |= BTN_R2;

        /* Left stick → analog + d-pad fallback */
        Sint16 lx = SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ly = SDL_GameControllerGetAxis(port_controller, SDL_CONTROLLER_AXIS_LEFTY);
        port_pad_lx = (unsigned char)((lx + 32768) >> 8);
        port_pad_ly = (unsigned char)((ly + 32768) >> 8);

        /* Digital d-pad from stick with deadzone */
        if (lx < -16000) b |= BTN_LEFT;
        if (lx >  16000) b |= BTN_RIGHT;
        if (ly < -16000) b |= BTN_UP;
        if (ly >  16000) b |= BTN_DOWN;
    }
    else
    {
        /* Analog stick from keyboard (fully digital) */
        port_pad_lx = 128;
        port_pad_ly = 128;
        if (b & BTN_LEFT)  port_pad_lx = 0;
        if (b & BTN_RIGHT) port_pad_lx = 255;
        if (b & BTN_UP)    port_pad_ly = 0;
        if (b & BTN_DOWN)  port_pad_ly = 255;
    }

    /* Auto-input for headless testing: replay a scripted button sequence */
    {
        static int auto_frame = 0;
        const char *auto_env = getenv("MGS_AUTO_INPUT");
        if (auto_env && auto_env[0] == '1') {
            auto_frame++;
            /* Scripted sequence: down, circle, circle, circle, down, down, circle
               Each action: press for 5 frames, wait 55 frames */
            struct { int frame; unsigned short btn; } script[] = {
                {  60, BTN_DOWN   },  /* select: move down */
                { 120, BTN_CIRCLE },  /* select: confirm */
                { 180, BTN_CIRCLE },  /* selectd: confirm */
                { 240, BTN_CIRCLE },  /* opening: skip */
                { 300, BTN_DOWN   },  /* select1: move down */
                { 360, BTN_DOWN   },  /* select1: move down */
                { 420, BTN_CIRCLE },  /* select1: confirm (s00a) */
                /* gameplay: press arrows to move Snake */
                { 540, BTN_LEFT   },
                { 600, BTN_UP     },
                { 660, BTN_RIGHT  },
                { 720, BTN_DOWN   },
                { 780, BTN_LEFT   },
            };
            int n = sizeof(script) / sizeof(script[0]);
            for (int i = 0; i < n; i++) {
                if (auto_frame >= script[i].frame && auto_frame < script[i].frame + 5) {
                    b |= script[i].btn;
                }
            }
        }
    }

    port_pad_buttons = b;
}

void mts_init_controller(void) {}

int mts_get_pad(int channel, MTS_PAD *pad)
{
    (void)channel;
    memset(pad, 0, sizeof(*pad));
    if (port_controller)
    {
        pad->flag = MTS_PAD_ANALOG;
        pad->lx = port_pad_lx;
        pad->ly = port_pad_ly;
    }
    else
    {
        pad->flag = MTS_PAD_DIGITAL;
        pad->lx = 128;
        pad->ly = 128;
    }
    pad->button = ~port_pad_buttons; /* PSX uses active-low */
    pad->rx = 128;
    pad->ry = 128;
    return 1;
}

void *mts_get_controller_data(int channel)
{
    (void)channel;
    return NULL;
}

int mts_read_pad(int channel)
{
    /* Only return buttons for port 0 (player 1).
       Port 2 is the PSX debug controller — returning player 1 data
       for it triggers debug prints every frame. */
    if (channel != 0) return 0;
    return port_pad_buttons;
}

void mts_set_pad_vibration(int channel, int time) { (void)channel; (void)time; }
void mts_set_pad_vibration2(int channel, int value) { (void)channel; (void)value; }
int mts_get_pad_vibration_type(int channel) { (void)channel; return 0; }

void mts_stop_controller(void) {}
long mts_PadRead(int unused) { (void)unused; return port_pad_buttons; }
int mts_control_vibration(int enable) { (void)enable; return 0; }

void mts_reset_graph(void) {}

/*---------------------------------------------------------------------------*/
/* SIO lock stubs                                                            */
/*---------------------------------------------------------------------------*/

void mts_lock_sio(void) {}
void mts_unlock_sio(void) {}

void SetExMask(void) {}
/* On PSX, returns address where overlay binaries are loaded.
   In our port, stage overlays are compiled statically.
   Return the default (select) overlay's character table. */
void *mts_get_bss_tail(void)
{
    extern void *_StageCharacterEntries_select;
    return &_StageCharacterEntries_select;
}

/*---------------------------------------------------------------------------*/
/* Stream / IO — redirect to stdio                                           */
/*---------------------------------------------------------------------------*/

int set_stdout_stream(int stream)
{
    (void)stream;
    return 0;
}

void reset_stdout_stream(void) {}
void set_output_stream(int stream) { (void)stream; }

/* The original MTS overrides fprintf and cprintf for its stream system.
   We provide them here to satisfy the linker when compiling source files
   that call them. Note: the PSX fprintf takes int stream, not FILE*. */
#ifdef __IN_MTS_NEW__
/* Avoid conflict with stdio's fprintf — the PSX version has a different signature.
   Source files include mts.h which declares: int fprintf(int stream, const char *format, ...);
   We implement this PSX-compatible version. */

/* These are defined as weak symbols so they don't conflict with stdio fprintf
   when not needed. In practice, PSX code calls fprintf(stream_id, ...) where
   stream_id is an int, not a FILE*. */
#endif

int cprintf(const char *format, ...)
{
    va_list ap;
    int ret;
    va_start(ap, format);
    ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}
