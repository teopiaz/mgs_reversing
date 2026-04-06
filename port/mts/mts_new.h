#ifndef __MTS_NEW_H__
#define __MTS_NEW_H__

#include "mts.h"

/* Stub kernel.h types — port doesn't use PSX threads */
struct TCB;

#define SEMAPHORE_NOT_WAITING -1
#define SEMAPHORE_LAST_IN_QUEUE -1

enum {
    MTS_SYS_START = 0,
    MTS_SYS_EXIT = 1,
};

typedef struct MTS_SYS_MSG
{
    int       code;
    int       tasknr;
    void    (*procedure)(void);
    void     *stack_pointer;
} MTS_SYS_MSG;

typedef struct MTS_ITASK
{
    struct MTS_ITASK *next;
    int               tasknr;
    unsigned int      last;
    unsigned int      target;
    int (*callback)(void);
} MTS_ITASK;

typedef struct MTS_TASK
{
    signed char state;
    signed char task_queue;
    signed char next_task;
    signed char src;
    MTS_ITASK  *intr;
    union {
        int (*callback)(void);
        unsigned char *message;
    } u;
    signed char wake_count;
    signed char next_sem;
    char        overrun;
    signed char pending;
    void       *stack_top;
    int         stack_size;
    int         tid;
    struct TCB *tcb;
} MTS_TASK;

#define MTS_STACK_COOKIE 0x12435687

/* mts_new.c */
void mts_lock_sio( void );
void mts_unlock_sio( void );
void mts_task_start(void);
void mts_scheduler_tick(void);

/* mask.c */
extern void SetExMask(void);
extern void *mts_get_bss_tail(void);

#define mts_assert(cond, line, ...) ((void)0)

#ifdef DEV_EXE
#define MTS_BUILD_DATE  __DATE__
#define MTS_BUILD_TIME  __TIME__
#else
#define MTS_BUILD_DATE  "Jul 11 1998"
#define MTS_BUILD_TIME  "22:16:33"
#endif

#endif /* __MTS_NEW_H__ */
