/**
 * @file actor.c
 * @brief This file contains the implementation of the actor system.
 *
 * The actor system manages a list of actors, allowing for initialization,
 * execution, and destruction of a generic actors. Actors can be added
 * to different lists, and each list can have its own pause and kill levels.
 * The system also supports deferred destruction of actors and provides
 * functions for memory allocation and initialization.
 */

#define __LIBGV_ACTOR_C__
#include <stdio.h>
#include <libsn.h>
#include "libgv.h"
#include "mts/mts.h"    // for cprintf

// 0x0 No pause
// 0x1 codec
// 0x2 game paused
int SECTION(".sbss") GV_PauseLevel;
STATIC int SECTION(".sbss") dword_800AB92C; //maybe unused

typedef struct {
    GV_ACT start;
    GV_ACT end;
    short  pause_level;
    short  kill_level;
} AList;

static AList BSS ActorList[ GV_ACTOR_LEVEL ];

// initialization structure for the actor lists
// the pause and kill levels are set for each list
static struct
{
    short pause;
    short kill;
} levels[ GV_ACTOR_LEVEL ] =
{
    { GV_PAUSE_NOSTOP, GV_KILL_LEVEL_MANAGER }, // GV_ACTOR_DAEMON
    { GV_PAUSE_NOSTOP, GV_KILL_LEVEL_MANAGER }, // GV_ACTOR_MANAGER
    { GV_LEVEL_STOP,   GV_KILL_LEVEL_NORMAL  }, // GV_ACTOR_ASSIST
    { GV_LEVEL_STOP,   GV_KILL_LEVEL_NORMAL  }, // GV_ACTOR_PREV2
    { GV_LEVEL_NORMAL, GV_KILL_LEVEL_NORMAL  }, // GV_ACTOR_PREV
    { GV_LEVEL_NORMAL, GV_KILL_LEVEL_NORMAL  }, // GV_ACTOR_USER
    { GV_LEVEL_NORMAL, GV_KILL_LEVEL_NORMAL  }, // GV_ACTOR_AFTER
    { GV_LEVEL_STOP,   GV_KILL_LEVEL_NORMAL  }, // GV_ACTOR_AFTER2
    { GV_PAUSE_NOSTOP, GV_KILL_LEVEL_MANAGER }  // GV_ACTOR_DAEMON2
};

/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the actors lists and set the pause and kill levels.
 */
/* ActorList is file-static; the debug panel and test server walk it
   read-only, so hand them the base pointer rather than un-static'ing it. */
void *port_gv_actor_list( int level )
{
    if ( level < 0 || level >= GV_ACTOR_LEVEL ) return NULL;
    return &ActorList[ level ];
}

void GV_InitActorSystem( void )
{
    AList *list;
    int    i;

    list = ActorList;
    for ( i = 0; i < GV_ACTOR_LEVEL; i++ )
    {
        GV_ACT *start, *end;

        start = &list->start;
        end = &list->end;
        start->prev = NULL;
        start->next = end;
        end->prev = start;
        end->next = NULL;

        start->act = start->die = NULL;
        end->act = end->die = NULL;

        list->pause_level = levels[ i ].pause;
        list->kill_level = levels[ i ].kill;
        list++;
    }
    GV_PauseLevel = GV_PAUSE_NOSTOP;
}

/**
 * @brief Set the pause and kill levels for an actor list.
 *
 * @param level of the actor list
 * @param pause level to set for the pause flag
 * @param kill level to set for the kill flag
 */
void GV_ConfigActorSystem( int level, short pause, short kill )
{
    AList *list;

    list = &ActorList[ level ];
    list->pause_level = pause;
    list->kill_level = kill;
}

/**
 * @brief (Unused) Dump the actor system to the console.
 * Iterate over all actors in all actor lists and print their name and update
 * function.
 */
void GV_DumpActorSystem( void )
{
    AList *list;
    int    i;

    list = ActorList;
    cprintf("--DumpActorSystem--\n");

    for ( i = 0; i < GV_ACTOR_LEVEL; i++ )
    {
        cprintf( "Lv %d Pause %d Kill %d\n", i, list->pause_level, list->kill_level );
        {
            GV_ACT *this, *next;

            this = &list->start;
            do
            {
                next = this->next;
                if ( this->act != NULL )
                {
                    int c;
                    if ( this->count > 0 )
                    {
                        c = this->runtime * 100 / this->count;
                    }
                    else
                    {
                        c = 0;
                    }
                    cprintf("Lv%d %04d.%02d %08X %s\n",
                            i,
                            c / 100,
                            c % 100,
                            this->act,
                            this->filename);
                    this->runtime = this->count = 0;
                }
            } while ( ( this = next ) != NULL );
        }
        list++;
    }
}

/**
 * @brief Execute all actors in the actor system.
 * Iterate over all actors in all actor lists and call their update function.
 * This function is invoked continuously and represent the main game loop.
 */
/* Signal-safe actor crash recovery using sigsetjmp/siglongjmp */
#include <setjmp.h>
#include <signal.h>
#include <execinfo.h>
static sigjmp_buf actor_jmp;
static volatile sig_atomic_t actor_in_handler = 0;
static volatile void *actor_last_fault_addr = NULL;
static void *actor_last_bt[16];
static int   actor_last_bt_n = 0;
static void actor_crash_handler_si(int sig, siginfo_t *info, void *ucontext) {
    (void)sig; (void)ucontext;
    if (info) actor_last_fault_addr = info->si_addr;
    /* backtrace(3) is async-signal-safe on macOS and gives us a one-shot
       call chain to dump on the first catch — invaluable for tracking
       down 32→64-bit pointer truncation bugs in the menu/codec code. */
    actor_last_bt_n = backtrace(actor_last_bt, 16);
    if (actor_in_handler) siglongjmp(actor_jmp, 1);
}

void GV_ExecActorSystem( void )
{
    AList *list;
    int    i, pause;

    list = ActorList;

    struct sigaction sa = {0};
    sa.sa_sigaction = actor_crash_handler_si;
    sa.sa_flags = SA_SIGINFO;
    struct sigaction old_segv, old_bus;
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);

    for ( i = GV_ACTOR_LEVEL; i > 0; i-- )
    {
        void ( *act )( GV_ACT * );
        GV_ACT *this, *next;

        pause = GV_PauseLevel;
        if ( ( list->pause_level & pause ) == 0 )
        {
            this = &list->start;
            do
            {
                extern int GM_CurrentMap;

                next = this->next;
                if ( ( act = this->act ) != NULL )
                {
                    actor_in_handler = 1;
                    if (sigsetjmp(actor_jmp, 1) == 0)
                    {
                        act( this );
                    }
                    else
                    {
                        /* Throttle: print at most once per (filename, fnptr)
                           pair so a stuck-faulting actor doesn't drown the log.
                           Includes the fault address (via SA_SIGINFO) to make
                           NULL-deref bugs easier to bisect. */
                        static struct { const char *file; void *fn; } seen[32] = {0};
                        void *fn = (void *)this->act;
                        const char *file = this->filename ? this->filename : "?";
                        int already = 0;
                        for (int si = 0; si < 32 && seen[si].fn; si++) {
                            if (seen[si].fn == fn && seen[si].file == file) { already = 1; break; }
                        }
                        if (!already) {
                            for (int si = 0; si < 32; si++) {
                                if (!seen[si].fn) { seen[si].fn = fn; seen[si].file = file; break; }
                            }
                            fprintf(stderr, "[actor] crash in %s (act=%p) fault_addr=%p — suppressing further\n",
                                    file, fn, (void *)actor_last_fault_addr);
                            if (actor_last_bt_n > 0) {
                                fprintf(stderr, "[actor] backtrace (%d frames):\n", actor_last_bt_n);
                                backtrace_symbols_fd(actor_last_bt, actor_last_bt_n, 2);
                            }
                        }
                    }
                    actor_in_handler = 0;
                }
                GM_CurrentMap = 0;
            } while ( ( this = next ) != NULL );
        }
        list++;
    }

    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGBUS, &old_bus, NULL);
}

/**
 * @brief Deferred destruction of actors in the actor system.
 *
 * @param kill The level at which to destroy actors.
 */
void GV_DestroyActorSystem( int kill )
{
    AList *list;
    int    i;

    list = ActorList;
    for ( i = GV_ACTOR_LEVEL; i > 0; i-- )
    {
        GV_ACT *this, *next;

        if ( list->kill_level <= kill )
        {
            this = &list->start;
            do
            {
                next = this->next;
                if ( this->act || this->die )
                {
                    GV_DestroyActor( this );
                }
            } while ( ( this = next ) != NULL );
        }
        list++;
    }
}

/**
 * @brief Initialize an actor and add it at the end of the selected list
 *
 * @param level The id of the list where the actor will be added.
 * @param this The actor to add.
 * @param free The function to call when freeing the actor.
 */
void GV_InitActor( int level, void *this, GV_FREEFUNC free )
{
    GV_ACT *actor, *next, *prev;

    actor = (GV_ACT *)this;
    next = &ActorList[ level ].end;
    prev = next->prev;

    next->prev = actor;
    prev->next = actor;
    actor->next = next;
    actor->prev = prev;

    actor->act = actor->die = NULL;
    actor->free = free;
}

/**
 * @brief Allocate memory for an actor and initialize it.
 *
 * @param level The id of the execution list where the actor will be added.
 * @param size The size of the actor.
 * @return GV_ACT* The allocated actor.
 */
void *GV_NewActor( int level, int size )
{
    GV_ACT *this;

    this = GV_Malloc( size );
    if ( this != NULL )
    {
        GV_ZeroMemory( this, size );
        GV_InitActor( level, this, GV_Free );
    }
    return (void *)this;
}

void GV_SetNamedActor( void *this, void *act, void *die, const char *name )
{
    GV_ACT *actor;

    actor = (GV_ACT *)this;
    actor->act = (GV_ACTFUNC)act;
    actor->die = (GV_ACTFUNC)die;
    actor->filename = name;
    actor->runtime = actor->count = 0;
}

// Removes from linked list and calls shutdown/free funcs
/**
 * @brief Destroys an actor quickly by removing it from the actor list.
 * If the actor has a destructor and memory freeing functions, they are called.
 *
 * @param this Pointer to the actor to be destroyed.
 */
void GV_DestroyActorQuick( void *this )
{
    GV_ACT *actor, *next, *prev;
    void ( *die )( GV_ACT * ) ;
    void ( *free )( void * ) ;

    actor = (GV_ACT *)this;
    next = actor->next;
    prev = actor->prev;

    next->prev = prev;
    prev->next = next;
    actor->next = actor->prev = NULL;

    if ( ( die = actor->die ) != NULL )
    {
        die( actor );
    }

    if ( ( free = actor->free ) != NULL )
    {
        free( actor );
    }
}

/**
 * @brief Schedule the destruction of an actor for the next activation cycle
 * by setting its update function.
 *
 * @param actor The actor to destroy.
 */
void GV_DestroyActor( void *this )
{
    ( (GV_ACT *)this )->act = (GV_ACTFUNC)GV_DestroyActorQuick;
}

void GV_DestroyOtherActor( void *target )
{
    AList  *list;
    int     i;

    list = ActorList;
    for ( i = GV_ACTOR_LEVEL; i > 0; i-- )
    {
        GV_ACT *this, *next;

        this = &list->start;
        do
        {
            next = this->next;
            if ( this == target )
            {
                GV_DestroyActor( this );
                return;
            }
        } while ( ( this = next ) != NULL );
        list++;
    }

    printf("#");
}
