#include "common.h"
#include "libdg/libdg.h"
#include "libgv/libgv.h"
#include "libhzd/libhzd.h"
#include "inline_n.h"

/*possible funcs
 HZD_GetLevelHazard
 HZD_GetLevelHeight
 HZD_LevelHazardCheckOne
 HZD_GetLevelAtr
*/

/**
 * Used in collision detection, called every frame for Snake and each enemy on the map.
 *
 * In Snake's case, the first argument is snake->control->field_0_vec.
 * Whether for Snake or not, the second argument is always the scratchpad
 * address 0x1f80000c.
 *
 * Disabling this function badly messes up collision detection for both Snake
 * and his enemies, ie Snake will constantly fall into and come back out from
 * the floor as he runs around.
 */
STATIC void HZD_CopyVector2(SVECTOR *src, HZD_VEC *dst)
{
    dst->x = src->vx;
    dst->y = src->vy;
    dst->z = src->vz;
}

#ifdef PORT_BUILD
/* On 64-bit, pointer size changes struct layout vs PSX hardcoded offsets.
   Use static side-channel for floor pointers to avoid mismatch. */
static HZD_FLR *port_max_floor = NULL;
static HZD_FLR *port_min_floor = NULL;

typedef struct {
    char     unused1[0x8];  // 00
    int      side;          // 08
    HZD_VEC  point;         // 0C
    char     unused2[0x20]; // 14
    HZD_VEC  f34;           // 34
    char     pad_3a[2];     // 3A - padding (was pointer on PSX)
    int      max_floor_stub; // 3C - 4 bytes (was HZD_FLR* on PSX)
    int      min_floor_stub; // 40 - 4 bytes (was HZD_FLR* on PSX)
    int      max_level;     // 44
    int      min_level;     // 48
} SCRPAD_DATA;
#else
typedef struct {
    char     unused1[0x8];  // 00
    int      side;          // 08
    HZD_VEC  point;         // 0C
    char     unused2[0x20]; // 14
    HZD_VEC  f34;           // 34
    HZD_FLR *max_floor;     // 3C
    HZD_FLR *min_floor;     // 40
    int      max_level;     // 44
    int      min_level;     // 48
} SCRPAD_DATA;
#endif

#define SCRPAD ((SCRPAD_DATA *)SCRPAD_ADDR)

#define SIDE      (*(int *)(SCRPAD_ADDR + 0x8))
#define POINT     (*(HZD_VEC *)(SCRPAD_ADDR + 0xC))
#ifdef PORT_BUILD
#define MAX_FLOOR (port_max_floor)
#define MIN_FLOOR (port_min_floor)
#else
#define MAX_FLOOR (*(HZD_FLR **)(SCRPAD_ADDR + 0x3C))
#define MIN_FLOOR (*(HZD_FLR **)(SCRPAD_ADDR + 0x40))
#endif

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

STATIC int HZD_LevelHeight(HZD_FLR *floor)
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
            y = HZD_LevelHeight(floor);
        }

        scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;
        if (POINT.y >= y)
        {
            if (y > scrpad->max_level)
            {
                scrpad->max_level = y;
#ifdef PORT_BUILD
                port_max_floor = floor;
#else
                scrpad->max_floor = floor;
#endif
            }
        }
        else
        {
            if (y < scrpad->min_level)
            {
                scrpad->min_level = y;
#ifdef PORT_BUILD
                port_min_floor = floor;
#else
                scrpad->min_floor = floor;
#endif
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

int HZD_LevelTestHazard(HZD_HDL *hdl, SVECTOR *point, int flags)
{
    HZD_GRP *pArea;
    int      *pScr;
    HZD_FLR  *floor;
    int       count;
    HZD_FLR **ppFloors;
    int      *pScr2;

    pArea = hdl->group;

    HZD_CopyVector2(point, &POINT);

    pScr = (int *)getScratchAddr(0);
    pScr[16] = 0;
    pScr[15] = 0;
    pScr[17] = -1000000;
    pScr[18] = 1000000;
#ifdef PORT_BUILD
    port_max_floor = NULL;
    port_min_floor = NULL;
#endif

    if (flags & 1)
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

    if (flags & 2)
    {
        ppFloors = hdl->dynamic_floors;

        for (count = hdl->dynamic_floor_index; count > 0; count--, ppFloors++)
        {
            if (HZD_PointInBounds(*ppFloors, (HZD_VEC *)getScratchAddr(3)))
            {
                HZD_LevelTest(*ppFloors);
            }
        }
    }

    pScr2 = (int *)getScratchAddr(0);
    if (pScr2[16] == 0)
    {
        return pScr2[15] != 0;
    }

    return (pScr2[15] == 0) ? 2 : 3;
}

void HZD_LevelMinMaxFloors(HZD_FLR **floors)
{
#ifdef PORT_BUILD
    floors[0] = port_max_floor;
    floors[1] = port_min_floor;
#else
    SCRPAD_DATA *scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;
    floors[0] = scrpad->max_floor;
    floors[1] = scrpad->min_floor;
#endif
}

void HZD_LevelMinMaxHeights(int *levels)
{
    SCRPAD_DATA *scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;

    levels[0] = scrpad->max_level;
    levels[1] = scrpad->min_level;
}

int HZD_SlopeFloorLevel(SVECTOR *point, HZD_FLR *floor)
{
    HZD_CopyVector2(point, &POINT);
    return HZD_LevelHeight(floor);
}

int HZD_LevelTestFloor(HZD_FLR *floor, SVECTOR *point)
{
    SCRPAD_DATA *scrpad;
    SCRPAD_DATA *scrpad2;

    HZD_CopyVector2(point, &POINT);

    scrpad = (SCRPAD_DATA *)SCRPAD_ADDR;
#ifdef PORT_BUILD
    port_min_floor = NULL;
    port_max_floor = NULL;
#else
    scrpad->min_floor = NULL;
    scrpad->max_floor = NULL;
#endif
    scrpad->max_level = -1000000;
    scrpad->min_level = 1000000;

    if (HZD_PointInBounds(floor, &POINT))
    {
        HZD_LevelTest(floor);
    }

#ifdef PORT_BUILD
    if (!port_min_floor)
    {
        return port_max_floor != NULL;
    }
    return (!port_max_floor) ? 2 : 3;
#else
    scrpad2 = (SCRPAD_DATA *)SCRPAD_ADDR;

    if (!scrpad2->min_floor)
    {
        return scrpad2->max_floor != NULL;
    }

    return (!scrpad2->max_floor) ? 2 : 3;
#endif
}

int HZD_LevelMaxHeight(void)
{
    if (!MAX_FLOOR)
    {
        return 0;
    }

    return MAX_FLOOR->b1.h >> 8;
}
