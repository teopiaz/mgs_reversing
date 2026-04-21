/**
 * Port MTS implementation — cooperative multitasking via ucontext.
 *
 * The PSX MTS is a single-core cooperative scheduler: tasks yield explicitly
 * at mts_slp_tsk, mts_wait_vbl, mts_lock_sem, mts_send, mts_receive.
 * We replicate this exactly with ucontext (one coroutine per task).
 */

#define __IN_MTS_NEW__

/* Suppress macOS deprecation warning for ucontext — it works fine,
   Apple just wants you to use pthreads/GCD instead. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#define _XOPEN_SOURCE  /* required for ucontext.h on macOS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ucontext.h>
#include <SDL.h>
#include <unistd.h>
#include "mts.h"
#include "mts_new.h"
#include "mts_pad.h"

/*---------------------------------------------------------------------------*/
/* Task state                                                                */
/*---------------------------------------------------------------------------*/

#define TASK_STACK_SIZE (256 * 1024)  /* generous stack per task */

typedef struct {
    ucontext_t   ctx;
    char        *stack;          /* allocated stack memory */
    int          state;          /* MTS_TASK_DEAD, _READY, _SLEEPING, _WAIT_VBL, _PENDING */
    int          wake_count;     /* pre-wake counter for mts_slp_tsk */
    int          vbl_target;     /* target tick for mts_wait_vbl */
    void       (*procedure)(void);
} PortTask;

static PortTask tasks[MTS_NR_TASK];
static int      active_task = -1;
static ucontext_t main_ctx;             /* the main loop's context */
static unsigned int vbl_tick = 0;       /* advanced by main loop */

/* Semaphores: who holds each, and a simple wait queue */
#define MAX_SEM_WAITERS 16
static int sem_holder[MTS_MAX_SEMAPHORE];
static int sem_queue[MTS_MAX_SEMAPHORE][MAX_SEM_WAITERS];
static int sem_queue_len[MTS_MAX_SEMAPHORE];

/* Messaging: simple single-slot mailbox per task */
typedef struct {
    int    has_data;
    int    from;
    unsigned char data[MTS_SZ_MESSAGE];
} Mailbox;
static Mailbox mailboxes[MTS_NR_TASK];

static int current_task_id = 0;

/* Exception / vsync callbacks (kept for compatibility) */
static void (*exception_func)(void) = NULL;
static void (*vsync_control_func)(void) = NULL;
static int  (*vsync_callback_func)(void) = NULL;

/*---------------------------------------------------------------------------*/
/* Context switching                                                         */
/*---------------------------------------------------------------------------*/

/* Yield from the current task back to the main loop (scheduler). */
static void yield_to_main(void)
{
    int me = active_task;
    active_task = -1;
    swapcontext(&tasks[me].ctx, &main_ctx);
}

/* Resume a specific task from the main loop. */
static void resume_task(int id)
{
    active_task = id;
    swapcontext(&main_ctx, &tasks[id].ctx);
}

/* Find the lowest-numbered ready task (PSX priority = lower ID = higher priority). */
static int find_ready_task(void)
{
    for (int i = 0; i < MTS_NR_TASK; i++) {
        if (tasks[i].state == MTS_TASK_READY)
            return i;
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* Scheduler tick — called once per frame from the main loop                 */
/*---------------------------------------------------------------------------*/

void mts_scheduler_tick(void)
{
    vbl_tick++;

    /* Wake tasks whose vbl wait has elapsed */
    for (int i = 0; i < MTS_NR_TASK; i++) {
        if (tasks[i].state == MTS_TASK_WAIT_VBL && (int)(vbl_tick - tasks[i].vbl_target) >= 0) {
            tasks[i].state = MTS_TASK_READY;
        }
    }

    /* Run all ready tasks in priority order until they all yield */
    for (;;) {
        int id = find_ready_task();
        if (id < 0) break;
        resume_task(id);
    }
}

/*---------------------------------------------------------------------------*/
/* Task entry trampoline                                                     */
/*---------------------------------------------------------------------------*/

static void task_trampoline(void)
{
    int me = active_task;
    if (me < 0 || me >= MTS_NR_TASK || !tasks[me].procedure) {
        printf("[mts] trampoline: BAD task %d\n", me);
        if (me >= 0 && me < MTS_NR_TASK) tasks[me].state = MTS_TASK_DEAD;
        yield_to_main();
        return;
    }
    tasks[me].procedure();
    /* If procedure returns (mts_ext_tsk not called), mark dead and yield */
    tasks[me].state = MTS_TASK_DEAD;
    yield_to_main();
}

/*---------------------------------------------------------------------------*/
/* Public API: Task management                                               */
/*---------------------------------------------------------------------------*/

void mts_boot(int tasknr, void (*procedure)(void), void *stack_pointer)
{
    (void)stack_pointer;

    /* Initialize all tasks as dead */
    for (int i = 0; i < MTS_NR_TASK; i++) {
        tasks[i].state = MTS_TASK_DEAD;
        tasks[i].wake_count = 0;
    }

    /* Initialize semaphores */
    for (int i = 0; i < MTS_MAX_SEMAPHORE; i++) {
        sem_holder[i] = SEMAPHORE_NOT_WAITING;
        sem_queue_len[i] = 0;
    }

    /* Initialize mailboxes */
    memset(mailboxes, 0, sizeof(mailboxes));

    /* The boot task (MTSID_GAME) runs as the main context — just call it directly.
       On PSX, mts_boot never returns. For the port, it does, and the main loop
       takes over scheduling. We store the procedure for deferred execution. */
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
    (void)stack_pointer;

    if (tasknr < 0 || tasknr >= MTS_NR_TASK) {
        printf("[mts] sta_tsk: bad task id %d\n", tasknr);
        return -1;
    }

    PortTask *t = &tasks[tasknr];

    /* Allocate stack if needed */
    if (!t->stack) {
        t->stack = (char *)malloc(TASK_STACK_SIZE);
        if (!t->stack) {
            printf("[mts] sta_tsk: malloc failed for task %d\n", tasknr);
            return -1;
        }
    }

    t->state = MTS_TASK_READY;
    t->wake_count = 0;

    /* Set up ucontext — getcontext must be called BEFORE setting procedure,
       because on some platforms getcontext may clobber nearby memory. */
    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = TASK_STACK_SIZE;
    t->ctx.uc_link = &main_ctx;  /* return to main on completion */
    makecontext(&t->ctx, task_trampoline, 0);
    t->procedure = procedure;  /* must be after getcontext/makecontext */

    printf("[mts] sta_tsk(%d) → coroutine %p\n", tasknr, (void *)procedure);

    /* If we're inside a task, yield so the scheduler can decide who runs next.
       If we're in the main context (active_task == -1), the new task will run
       on the next scheduler tick. */
    return 0;
}

void mts_ext_tsk(void)
{
    if (active_task >= 0) {
        tasks[active_task].state = MTS_TASK_DEAD;
        yield_to_main();
    }
}

void mts_shutdown(void)
{
    for (int i = 0; i < MTS_NR_TASK; i++) {
        if (tasks[i].stack) {
            free(tasks[i].stack);
            tasks[i].stack = NULL;
        }
        tasks[i].state = MTS_TASK_DEAD;
    }
}

void mts_task_start(void)
{
}

int mts_get_current_task_id(void)
{
    return active_task >= 0 ? active_task : current_task_id;
}

int mts_get_task_status(long id)
{
    if (id < 0 || id >= MTS_NR_TASK) return MTS_TASK_DEAD;
    return tasks[id].state;
}

/*---------------------------------------------------------------------------*/
/* Sleep / Wake                                                              */
/*---------------------------------------------------------------------------*/

void mts_slp_tsk(void)
{
    if (active_task < 0) return; /* not in a task context */

    PortTask *t = &tasks[active_task];
    if (t->wake_count > 0) {
        /* Already woken before we slept — stay ready */
        t->wake_count = 0;
        t->state = MTS_TASK_READY;
    } else {
        t->wake_count = 0;
        t->state = MTS_TASK_SLEEPING;
    }
    yield_to_main();
}

void mts_wup_tsk(int dst)
{
    if (dst < 0 || dst >= MTS_NR_TASK) return;

    PortTask *t = &tasks[dst];
    if (t->state == MTS_TASK_SLEEPING) {
        t->state = MTS_TASK_READY;
    } else if (t->state == MTS_TASK_READY) {
        t->wake_count++;
    }
}

/*---------------------------------------------------------------------------*/
/* V-Sync wait                                                               */
/*---------------------------------------------------------------------------*/

void mts_init_vsync(void)
{
    vbl_tick = 0;
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
    if (active_task < 0) {
        /* Not in a task context (main loop) — just advance tick */
        vbl_tick += (unsigned int)count;
        return 0;
    }

    PortTask *t = &tasks[active_task];
    t->vbl_target = vbl_tick + (unsigned int)count;
    t->state = MTS_TASK_WAIT_VBL;
    yield_to_main();

    return 0;
}

int mts_get_tick_count(void)
{
    return (int)vbl_tick;
}

/*---------------------------------------------------------------------------*/
/* Semaphores                                                                */
/*---------------------------------------------------------------------------*/

void mts_lock_sem(int no)
{
    if (no < 0 || no >= MTS_MAX_SEMAPHORE) return;
    if (active_task < 0) return; /* main context — no contention */

    if (sem_holder[no] == SEMAPHORE_NOT_WAITING) {
        /* Free — take it */
        sem_holder[no] = active_task;
        return;
    }

    /* Already held — enqueue and wait */
    if (sem_queue_len[no] < MAX_SEM_WAITERS) {
        sem_queue[no][sem_queue_len[no]++] = active_task;
    }
    tasks[active_task].state = MTS_PENDING;
    yield_to_main();
}

void mts_unlock_sem(int no)
{
    if (no < 0 || no >= MTS_MAX_SEMAPHORE) return;

    if (sem_queue_len[no] > 0) {
        /* Hand off to next waiter */
        int next = sem_queue[no][0];
        /* Shift queue */
        for (int i = 1; i < sem_queue_len[no]; i++)
            sem_queue[no][i - 1] = sem_queue[no][i];
        sem_queue_len[no]--;

        sem_holder[no] = next;
        tasks[next].state = MTS_TASK_READY;
    } else {
        sem_holder[no] = SEMAPHORE_NOT_WAITING;
    }
}

/*---------------------------------------------------------------------------*/
/* Messaging                                                                 */
/*---------------------------------------------------------------------------*/

void mts_send(int dst, unsigned char *message)
{
    if (dst < 0 || dst >= MTS_NR_TASK) return;

    Mailbox *mb = &mailboxes[dst];
    memcpy(mb->data, message, MTS_SZ_MESSAGE);
    mb->from = active_task >= 0 ? active_task : current_task_id;
    mb->has_data = 1;

    /* If dst is blocked in receive, wake it */
    if (tasks[dst].state == MTS_TASK_RECEIVING) {
        tasks[dst].state = MTS_TASK_READY;
    }

    /* Yield to let receiver run */
    if (active_task >= 0) {
        tasks[active_task].state = MTS_TASK_SENDING;
        yield_to_main();
    }
}

int mts_isend(int dst)
{
    /* Interrupt-safe send: wake a task waiting on mts_receive(MTS_TASK_INTR).
       Unlike mts_send, does NOT yield — called from interrupt/main context.
       On PSX, this is triggered by SPU IRQ to wake the sound driver task. */
    if (dst < 0 || dst >= MTS_NR_TASK) return 0;
    if (tasks[dst].state != MTS_TASK_RECEIVING) return 0;

    /* Mark mailbox as having interrupt data (from = MTS_TASK_INTR = -1) */
    Mailbox *mb = &mailboxes[dst];
    mb->has_data = 1;
    mb->from = -1; /* MTS_TASK_INTR */

    tasks[dst].state = MTS_TASK_READY;
    return 1;
}

int mts_receive(int src, unsigned char *message)
{
    int me = active_task >= 0 ? active_task : current_task_id;
    Mailbox *mb = &mailboxes[me];

    /* Check if we already have a message */
    if (mb->has_data && (src < 0 || src == mb->from || src == MTS_TASK_ANY)) {
        if (message) memcpy(message, mb->data, MTS_SZ_MESSAGE);
        mb->has_data = 0;
        return mb->from;
    }

    /* Block until message arrives */
    if (active_task >= 0) {
        tasks[active_task].state = MTS_TASK_RECEIVING;
        yield_to_main();

        /* Resumed — check mailbox again */
        if (mb->has_data) {
            if (message) memcpy(message, mb->data, MTS_SZ_MESSAGE);
            mb->has_data = 0;
            return mb->from;
        }
    }

    return -1;
}

void mts_send_msg(int dst, int data0, int data1)
{
    int msg[4] = {data0, data1, 0, 0};
    mts_send(dst, (unsigned char *)msg);
}

int mts_recv_msg(int src, int *data0, int *data1)
{
    int msg[4];
    int result = mts_receive(src, (unsigned char *)msg);
    *data0 = msg[0];
    *data1 = msg[1];
    return result;
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
    printf("[mts] tasks:");
    for (int i = 0; i < MTS_NR_TASK; i++) {
        if (tasks[i].state != MTS_TASK_DEAD)
            printf(" %d=%d", i, tasks[i].state);
    }
    printf("\n");
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

/* Non-static so imgui_debug.cpp can show live pad state in the Other tab. */
unsigned short port_pad_buttons = 0;
unsigned char  port_pad_lx = 128, port_pad_ly = 128;

/* Replay / record state — file-scope so port_replay_start() / port_record_start() can reset */
static FILE *play_file = NULL;
static FILE *rec_file  = NULL;
static char  play_path_buf[256] = {0};
static char  rec_path_buf[256]  = {0};
static int   play_restart_pending = 0;
static int   rec_restart_pending  = 0;
static int   rec_stop_pending     = 0;
/* Counts for replay_status in get_state */
int TEST_HARNESS_processed_inputs = 0;   /* last frame number processed in replay */
int TEST_HARNESS_total_inputs     = 0;   /* total entries in the log file */

/* Count entries in a log file (frame/button pairs). Returned so get_state can
   report total_inputs. Does not affect the caller's file position. */
static int count_log_entries(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int count = 0;
    unsigned int fr; unsigned int btn;
    while (fscanf(f, "%u 0x%X", &fr, &btn) == 2)
        count++;
    fclose(f);
    return count;
}

void TEST_HARNESS_replay_start(const char *path)
{
    if (!path || !path[0]) return;
    strncpy(play_path_buf, path, sizeof(play_path_buf) - 1);
    play_path_buf[sizeof(play_path_buf) - 1] = '\0';
    /* If rec is active, stop it */
    if (rec_file) rec_stop_pending = 1;
    play_restart_pending = 1;
    TEST_HARNESS_processed_inputs = 0;
    TEST_HARNESS_total_inputs     = count_log_entries(path);
    printf("[input] replay_start scheduled: %s (%d entries)\n", path, TEST_HARNESS_total_inputs);
}

void TEST_HARNESS_record_start(const char *path)
{
    if (!path || !path[0]) return;
    strncpy(rec_path_buf, path, sizeof(rec_path_buf) - 1);
    rec_path_buf[sizeof(rec_path_buf) - 1] = '\0';
    /* Stop replay if active */
    if (play_file) { fclose(play_file); play_file = NULL; }
    rec_restart_pending = 1;
    printf("[input] record_start scheduled: %s\n", path);
}

void TEST_HARNESS_record_stop(void)
{
    rec_stop_pending = 1;
}

/* Returns 0=idle, 1=replaying, 2=recording */
int TEST_HARNESS_get_input_status(void)
{
    if (play_file) return 1;
    if (rec_file)  return 2;
    return 0;
}

const char *TEST_HARNESS_get_input_path(void)
{
    if (play_file) return play_path_buf;
    if (rec_file)  return rec_path_buf;
    return "";
}

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

    /* Test harness: override buttons if inject_input was called */
    {
        extern int TEST_HARNESS_override_buttons;
        extern int TEST_HARNESS_override_frames;
        if (TEST_HARNESS_override_frames > 0) {
            b = (unsigned short)TEST_HARNESS_override_buttons;
            TEST_HARNESS_override_frames--;
        }
    }

    port_pad_buttons = b;

    if (port_controller) {
        printf("[pad] buttons=0x%04X lx=%d ly=%d\n", port_pad_buttons, port_pad_lx, port_pad_ly);
    }

    /* Input recording/replay — supports mid-session switching via port_replay_start() */
    {
        static int io_init = 0;
        static unsigned int frame = 0;
        static unsigned short rec_prev_b = 0xFFFF;

        /* Replay state */
        static unsigned int  play_next_frame   = 0;
        static unsigned short play_cur_buttons  = 0;
        static unsigned short play_next_buttons = 0;
        static int           play_need_read    = 1;
        static int           play_eof          = 0;

        if (!io_init) {
            io_init = 1;
            const char *rec_path  = getenv("MGS_INPUT_RECORD");
            const char *play_path = getenv("MGS_INPUT_REPLAY");
            printf("[input] env: RECORD=%s REPLAY=%s\n",
                   rec_path ? rec_path : "(null)", play_path ? play_path : "(null)");
            if (play_path && play_path[0]) {
                if (play_file) fclose(play_file);
                play_file = fopen(play_path, "r");
                if (play_file) {
                    strncpy(play_path_buf, play_path, sizeof(play_path_buf) - 1);
                    play_need_read = 1; play_eof = 0;
                    play_cur_buttons = 0; play_next_frame = 0;
                    printf("[input] replaying from %s\n", play_path);
                } else {
                    printf("[input] FAILED to open replay: %s\n", play_path);
                }
            } else if (rec_path && rec_path[0]) {
                if (rec_file) fclose(rec_file);
                rec_file = fopen(rec_path, "w");
                if (rec_file) {
                    strncpy(rec_path_buf, rec_path, sizeof(rec_path_buf) - 1);
                    printf("[input] recording to %s\n", rec_path);
                } else {
                    printf("[input] FAILED to open record: %s\n", rec_path);
                }
            }
        }

        /* Handle mid-session replay switch (from port_replay_start) */
        if (play_restart_pending) {
            play_restart_pending = 0;
            if (play_file) { fclose(play_file); play_file = NULL; }
            play_file = fopen(play_path_buf, "r");
            if (play_file) {
                play_need_read = 1; play_eof = 0;
                play_cur_buttons = 0; play_next_frame = 0;
                frame = 0;  /* Reset frame counter to match log */
                printf("[input] replay restarted: %s\n", play_path_buf);
            } else {
                printf("[input] FAILED to reopen replay: %s\n", play_path_buf);
            }
        }

        /* Handle mid-session record switch (from port_record_start) */
        if (rec_restart_pending) {
            rec_restart_pending = 0;
            if (rec_file) { fclose(rec_file); rec_file = NULL; }
            rec_file = fopen(rec_path_buf, "w");
            if (rec_file) {
                rec_prev_b = 0xFFFF;
                printf("[input] recording started: %s\n", rec_path_buf);
            } else {
                printf("[input] FAILED to open record: %s\n", rec_path_buf);
            }
        }

        /* Handle record stop */
        if (rec_stop_pending) {
            rec_stop_pending = 0;
            if (rec_file) { fclose(rec_file); rec_file = NULL; }
            printf("[input] recording stopped\n");
        }

        if (rec_file) {
            if (port_pad_buttons != rec_prev_b) {
                char line[32];
                int len = snprintf(line, sizeof(line), "%u 0x%04X\n", frame, port_pad_buttons);
                write(fileno(rec_file), line, len);
                rec_prev_b = port_pad_buttons;
            }
        }

        if (play_file) {
            /* Replay: apply recorded button state at the correct frame */
            if (play_need_read && !play_eof) {
                unsigned int f; unsigned int btn;
                if (fscanf(play_file, "%u 0x%X", &f, &btn) == 2) {
                    play_next_frame   = f;
                    play_next_buttons = (unsigned short)btn;
                } else {
                    play_eof = 1;
                }
                play_need_read = 0;
            }

            while (!play_eof && frame >= play_next_frame) {
                play_cur_buttons = play_next_buttons;
                unsigned int f; unsigned int btn;
                if (fscanf(play_file, "%u 0x%X", &f, &btn) == 2) {
                    play_next_frame   = f;
                    play_next_buttons = (unsigned short)btn;
                } else {
                    play_eof = 1;
                    TEST_HARNESS_processed_inputs = frame;
                    printf("[input] replay ended at frame %u\n", frame);
                }
            }

            if (play_eof) {
                /* Replay finished — stop overriding input so inject_input
                   and keyboard/gamepad work again. */
                fclose(play_file);
                play_file = NULL;
                play_cur_buttons = 0;
            } else {
                port_pad_buttons = play_cur_buttons;
                TEST_HARNESS_processed_inputs = frame;
            }
        }

        frame++;
    }
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
    /* Channel 0 = player 1 (main gameplay input).
       Channel 1 = player 2 / debug controller.  cancel.c reads channel 1 to
       allow skipping cinemas.  We map channel 1 → player 1 buttons so that
       inject_input / keyboard / gamepad can skip opening sequences. */
    (void)channel;
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

#pragma clang diagnostic pop
