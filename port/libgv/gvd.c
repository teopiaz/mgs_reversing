#define __LIBGV_GVD_C__
#include "libgv.h"

#include <stdio.h>
#include <libsn.h>
#include "common.h"
#include "mts/mts.h"
#include "libdg/libdg.h"    // for DG_HikituriFlag

#define PORT_NORMAL_SIZE  0x2000000  /* 32 MiB, matches port_memory.c reservation */
#define PORT_PACKET_SIZE  0x200000   /* 2 MiB each */
extern void *port_normal_memory, *port_packet_memory0, *port_packet_memory1;
extern unsigned char *GV_ResidentMemoryBottom;

/*---------------------------------------------------------------------------*/

char   SECTION(".sbss") *GM_StageName;
short *SECTION(".sbss")  GM_CurrentPadData;
int    SECTION(".sbss")  GV_Clock;
int    SECTION(".sbss")  GV_PassageTime;

int GV_Time = 0;
STATIC int GV_LastTickCount = 0;

typedef struct {
    GV_ACT actor;
} Work;

static Work BSS GV_Work;

/*---------------------------------------------------------------------------*/

static void Excep_callback( void )
{
    printf( "HANGUP: %s\n", GV_DebugMes );
}

static void Act( Work *work )
{
    int ticks;

    GV_Time++;

    ticks = mts_get_tick_count();

    GV_PassageTime = ticks - GV_LastTickCount;
    GV_LastTickCount = ticks;

    if ( DG_HikituriFlag == 0 )
    {
        // Flip active buffer
        GV_Clock ^= 1;
    }

    if ( GV_PauseLevel == 0 )
    {
        GV_ClearMessageSystem();
    }
}

void GV_ResetPacketMemory( void )
{
    GV_InitMemorySystem( GV_PACKET_MEMORY0, GV_MEMORY_DYNAMIC, port_packet_memory0, PORT_PACKET_SIZE );
    GV_InitMemorySystem( GV_PACKET_MEMORY1, GV_MEMORY_DYNAMIC, port_packet_memory1, PORT_PACKET_SIZE );
}

void GV_SetPacketTempMemory( void )
{
    GV_InitMemorySystem( GV_PACKET_MEMORY0, GV_MEMORY_STATIC, port_packet_memory0, PORT_PACKET_SIZE * 2 );
    GV_InitMemorySystem( GV_PACKET_MEMORY1, GV_MEMORY_STATIC, NULL, 0 );
}

static void ResetMemorySystem( void )
{
    GV_InitMemorySystemAll();
    GV_ResetPacketMemory();
    GV_InitMemorySystem( GV_NORMAL_MEMORY, GV_MEMORY_STATIC, port_normal_memory, PORT_NORMAL_SIZE );
    printf( "RESIDENT TOP %X\n", (unsigned int)GV_ResidentMemoryBottom );
}

void GV_ResetSystem( void )
{
    GV_InitMessageSystem();
    GV_InitPadSystem();
}

void GV_ResetMemory( void )
{
    GV_FreeCacheSystem();
    ResetMemorySystem();
}

void GV_StartDaemon( void )
{
    GV_InitActorSystem();
    GV_InitResidentMemory();
    GV_ResetLoader();
    GV_InitCacheSystem();
    GV_ResetSystem();

    GV_InitActor( GV_ACTOR_DAEMON, &GV_Work.actor, NULL );
    GV_SetNamedActor( &GV_Work.actor, Act, NULL, "gvd.c" );

    GV_Clock = 0;
    GV_Time = 0;

    mts_set_exception_func( Excep_callback );
}
