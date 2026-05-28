#include "libhzd.h"

#include "common.h"
#include "libdg/libdg.h"
#include "libgv/libgv.h"
#include "inline_n.h"

/*possible funcs
 HZD_GetLevelHazard
 HZD_GetLevelHeight
 HZD_LevelHazardCheckOne
 HZD_GetLevelAtr
*/

static void CopyVector(SVECTOR *src, HZD_VEC *dst)
{
    dst->x = src->vx;
    dst->y = src->vy;
    dst->z = src->vz;
}

/* On 64-bit, pointer size changes struct layout vs PSX hardcoded offsets.
   Use static side-channels for floor pointers to avoid mismatch. */
static HZD_FLR *port_max_floor = NULL;
static HZD_FLR *port_min_floor = NULL;

typedef struct {
    char     unused1[0x8];  // 00
    int      side;          // 08
    HZD_VEC  point;         // 0C
    char     unused2[0x20]; // 14
    HZD_VEC  f34;           // 34
    /* 0x3C-0x43: was max_floor/min_floor pointers on PSX (4 bytes each).
       On port, floor pointers are in port_max_floor/port_min_floor statics. */
    char     unused3[8];    // 3C-43
    int      max_level;     // 44
    int      min_level;     // 48
} SCRPAD_DATA;

#define SCRPAD ((SCRPAD_DATA *)SCRPAD_ADDR)

#define SIDE      (*(int *)(SCRPAD_ADDR + 0x8))
#define POINT     (*(HZD_VEC *)(SCRPAD_ADDR + 0xC))
#define MAX_FLOOR (port_max_floor)
#define MIN_FLOOR (port_min_floor)

STATIC int HZD_LevelCheckInPoint(HZD_FLR *floor)
{
    int p0, p1, p2, p3, p4;

    p0 = POINT.long_access[0];
    p1 = floor->p1.long_access[0];
    p2 = floor->p2.long_access[0];

    gte_ldsxy3( p1, p2, p0 );
    gte_nclip();
    p3 = floor->p3.long_access[0];
    gte_stopz( &SIDE );

    if ( SIDE >= 0 )
    {
        gte_ldsxy3( p2, p3, p0 );
        gte_nclip();
        p4 = floor->p4.long_access[0];
        gte_stopz( &SIDE );

        if ( SIDE < 0 ) return 0;

        gte_ldsxy3( p3, p4, p0 );
        gte_nclip();
        gte_stopz( &SIDE );

        if ( SIDE < 0 ) return 0;

        gte_ldsxy3( p4, p1, p0 );
        gte_nclip();
        gte_stopz( &SIDE );

        return SIDE >= 0;
    }
    else
    {
        gte_ldsxy3( p2, p3, p0 );
        gte_nclip();
        p4 = floor->p4.long_access[0];
        gte_stopz( &SIDE );

        if ( SIDE > 0 ) return 0;

        gte_ldsxy3( p3, p4, p0 );
        gte_nclip();
        gte_stopz( &SIDE );

        if ( SIDE > 0 ) return 0;

        gte_ldsxy3( p4, p1, p0 );
        gte_nclip();
        gte_stopz( &SIDE );

        return SIDE <= 0;
    }

}

static inline void HZD_LevelPointHeight_helper(void)
{
    // what were the original parameters for this crap? trying to make
    // the source and destination two arguments swaps the registers
    short *scratch2 = ( short * )SCRPAD_ADDR;

    scratch2[3] = *(short *)(SCRPAD_ADDR + 0x038);
    scratch2[2] = -*(short *)(SCRPAD_ADDR + 0x03a);
}

static inline void assign_subtract( int idx, short idx2, short idx3, short *val )
{
    ((short*)SCRPAD_ADDR)[idx] = ((short*)SCRPAD_ADDR)[idx2] - val[idx3];
}

STATIC int SlopeFloorLevel(HZD_FLR *floor)
{
    short *test;
    int x, y;

    assign_subtract( 26, 6, 0, ( short * )&floor->p1 );
    assign_subtract( 27, 7, 1, ( short * )&floor->p1 );

    //todo: fix below, probably some inline
    test = ( short * )(SCRPAD_ADDR + 0x038);
    test[0] = floor->p1.h;
    do {} while(0);
    test[y = 1] = floor->p2.h;

    HZD_LevelPointHeight_helper();

    gte_ldsxy3(0, *( int * )(SCRPAD_ADDR + 0x034), *( int* )(SCRPAD_ADDR + 0x004));
    gte_nclip();
    gte_stopz( (SCRPAD_ADDR + 0x008) );

    x = *(int * )(SCRPAD_ADDR + 0x008);
    if (floor->p3.h == 0) return floor->p1.y;
    return floor->p1.y - x / floor->p3.h;
}

STATIC void HZD_LevelTest(HZD_FLR *floor)
{
    int          y, h;
    SCRPAD_DATA *scrpad;

    h = floor->b1.h; // TODO: What's "h"?
    if ((h & 1) || HZD_LevelCheckInPoint(floor))
    {
        if (h & 2)
        {
            y = floor->b1.y;
        }
        else
        {
            y = SlopeFloorLevel(floor);
        }

        scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;
        if (POINT.y >= y)
        {
            if (y > scrpad->max_level)
            {
                scrpad->max_level = y;
                port_max_floor = floor;
            }
        }
        else
        {
            if (y < scrpad->min_level)
            {
                scrpad->min_level = y;
                port_min_floor = floor;
            }
        }
    }
}

static inline int HZD_PointInBounds(HZD_FLR *floor, HZD_VEC *point)
{
    if (floor->b1.z > point->z || floor->b2.z < point->z ||
        floor->b1.x > point->x || floor->b2.x < point->x)
    {
        return 0;
    }

    return 1;
}

int HZD_LevelHazardCheck( HZD_HDL *hzd, SVECTOR *from, int chk_flag )
{
    HZD_GRP *pArea;
    int      *pScr;
    HZD_FLR  *floor;
    int       count;
    HZD_FLR **ppFloors;

    pArea = hzd->grp;

    CopyVector(from, &POINT);

    pScr = (int *)getScratchAddr(0);
    pScr[16] = 0;
    pScr[15] = 0;
    pScr[17] = -1000000;
    pScr[18] = 1000000;
    port_max_floor = NULL;
    port_min_floor = NULL;

    if ( chk_flag & HZD_CHK_F_FLOOR )
    {
        floor = pArea->floors;

        for (count = pArea->n_floors; count > 0; count--, floor++)
        {
            if (HZD_PointInBounds(floor, (HZD_VEC *)getScratchAddr(3)))
            {
                HZD_LevelTest(floor);
            }
        }
    }

    if ( chk_flag & HZD_CHK_D_FLOOR )
    {
        ppFloors = hzd->dynamic_floors;

        for (count = hzd->dynamic_floor_index; count > 0; count--, ppFloors++)
        {
            if (HZD_PointInBounds(*ppFloors, (HZD_VEC *)getScratchAddr(3)))
            {
                HZD_LevelTest(*ppFloors);
            }
        }
    }

    /* On PSX, pScr[15] and pScr[16] held max_floor/min_floor pointers
       (4 bytes each at offsets 0x3C/0x40). On the port, floor pointers
       live in the port_max_floor / port_min_floor statics. */
    if (port_min_floor == NULL)
    {
        return port_max_floor != NULL;
    }
    return (port_max_floor == NULL) ? 2 : 3;
}

void HZD_GetLevelHazard( HZD_FLR **flr )
{
    flr[0] = port_max_floor;
    flr[1] = port_min_floor;
}

void HZD_GetLevelHeight( int *lvl_ptr )
{
    SCRPAD_DATA *scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;

    lvl_ptr[0] = scrpad->max_level;
    lvl_ptr[1] = scrpad->min_level;
}

int HZD_SlopeFloorLevel( SVECTOR *mov, HZD_FLR *flr )
{
    CopyVector(mov, &POINT);
    return SlopeFloorLevel(flr);
}

int HZD_GetFloorHit( HZD_FLR *flr, SVECTOR *mov )
{
    SCRPAD_DATA *scrpad;

    CopyVector(mov, &POINT);

    scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;
    port_min_floor = NULL;
    port_max_floor = NULL;
    scrpad->max_level = -1000000;
    scrpad->min_level = 1000000;

    if (HZD_PointInBounds(flr, &POINT))
    {
        HZD_LevelTest(flr);
    }

    if (!port_min_floor)
    {
        return port_max_floor != NULL;
    }
    return (!port_max_floor) ? 2 : 3;
}

int HZD_GetFloorLevel( void )
{
    if (!MAX_FLOOR)
    {
        return 0;
    }

    return MAX_FLOOR->b1.h >> 8;
}
