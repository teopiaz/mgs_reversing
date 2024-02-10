#include "psyq.h"
#include "libfs/libfs.h"
#include "libgcl/libgcl.h"

extern FS_FILE_INFO_8009D49C gDirFiles_8009D49C[];

int SafetyCheck( int, int, int );

int Safety_800C45F8( int lba, int timeout )
{
    CdlLOC loc;
    char   result[8];
    int    start;
    int    open;

    printf( "SafeCheckStart\n" );
    CdIntToPos( lba, &loc );

    while ( 1 )
    {
        printf( "TRY\n" );
        start = VSync( -1 );

        while ( 1 )
        {
            if ( SafetyCheck( loc.minute, loc.second, loc.sector ) >= 0 )
            {
                break;
            }

            if ( timeout > 0 && ( VSync( -1 ) - start ) > timeout )
            {
                printf( "TIMEOUT\n" );
                break;
            }
        }

        CdControlB( CdlNop, NULL, result );
        CdControlB( CdlNop, NULL, result );

        open = result[0] & ( CdlStatShellOpen | CdlStatError );
        if ( !open )
        {
            printf( "TRY END\n" );
            break;
        }

        printf( "OPEN\n" );
    }

    printf( "SafeCheckEND\n" );
    return 1;
}

void Safety_800C4714( void )
{
    char param;

    param = CdlModeSpeed | CdlModeSize1;
    while ( !CdControl( CdlSetmode, &param, NULL ) );
    mts_wait_vbl_800895F4(3);
    while ( !CdControl( CdlDemute, NULL, NULL ) );
}

void Safety_800C476C( int timeout )
{
    Safety_800C45F8( gDirFiles_8009D49C[0].field_4_sector, timeout );
    Safety_800C4714();
}

int Safety_800C47A0( void )
{
    int timeout;

    timeout = 0;
    if ( GCL_GetOption_80020968( 't' ) )
    {
        timeout = GCL_GetNextParamValue_80020AD4() * 60;
    }

    Safety_800C476C( timeout );
    return 1;
}
