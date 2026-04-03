#define __LIBHZD_HZDD_C__
#include "libhzd.h"

#include <stdio.h>
#include "libgv/libgv.h"

int SECTION(".sbss") dword_800AB9A4; // unused
int SECTION(".sbss") HZD_CurrentGroup;
int SECTION(".sbss") dword_800AB9AC; // unused

//------------------------------------------------------------------------------

#ifdef PORT_BUILD
void HZD_StartDaemon(void)
{
    extern int HZD_LoadInitHzd(void *, int);
    GV_SetLoader('h', (GV_LOADFUNC)&HZD_LoadInitHzd);
}
#else
void HZD_StartDaemon(void)
{
    GV_SetLoader('h', HZD_LoadInitHzd);
}
#endif

STATIC void HZD_ProcessTraps(HZD_TRG *trap, int n_traps)
{
    int i;
    char *new_var;
    int j;
    char *s;

    for (i = n_traps - 1; i > (-1); i--)
    {
        s = trap->trap.name;
        j = (sizeof(trap->trap.name)) + 1;

        if (s[0xd] == ((char) (-1)))
        {
            break;
        }
        for (; (j > 0) && ((*s) != ' '); j--)
        {
            s++;
        }

        *s = '\0';
        trap->trap.name_id = GV_StrCode(trap->trap.name);
        trap++;
        new_var = trap->trap.name;
        s = new_var;
    }
}

STATIC void HZD_ProcessRoutes(HZD_PAT *routes, int n_routes, HZD_DEF *hzm)
{
    HZD_PTP *points;
    int      i;

    points = (HZD_PTP *)&routes->points;
    for (i = n_routes - 1; i > -1; i--)
    {
        OFFSET_TO_PTR(hzm, points);
        points++;
    }
}

#ifndef PORT_BUILD
int HZD_LoadInitHzd(void *buf, int id)
{
    HZD_DEF *hzm;
    HZD_GRP *area;
    int      i;

    hzm = (HZD_DEF *)buf;
    if (hzm->version < 2)
    {
        printf("Warning:old version hzm\n");
    }

    hzm->ptr_access[0] = 0;

    OFFSET_TO_PTR(hzm, &hzm->groups);
    OFFSET_TO_PTR(hzm, &hzm->zones);
    OFFSET_TO_PTR(hzm, &hzm->routes);

    HZD_ProcessRoutes(hzm->routes, hzm->n_routes, hzm);

    area = hzm->groups;
    for (i = hzm->n_groups; i > 0; i--)
    {
        OFFSET_TO_PTR(hzm, &area->walls);
        OFFSET_TO_PTR(hzm, &area->floors);
        OFFSET_TO_PTR(hzm, &area->triggers);
        OFFSET_TO_PTR(hzm, &area->wallsFlags);

        HZD_ProcessTraps((HZD_TRG *)area->triggers, area->n_triggers);
        area++;
    }

    return 1;
}
#endif /* PORT_BUILD */

HZD_HDL *HZD_MakeHandler(HZD_DEF *hzd, int areaIndex, int dynamic_segments, int dynamic_floors)
{
    short    n_zones;
    void    *zones;
    HZD_HDL *hzdMap;
    int      i;
    HZD_TRG *trig;

    /* Port: use a static to store the route pointer instead of cramming it
       into the first 4 bytes of HZD_MAP (which truncates on 64-bit) */
    {
        static void *cached_route = NULL;
        static HZD_MAP *cached_hzd = NULL;
        if (cached_hzd != hzd) {
            cached_route = NULL;
            cached_hzd = hzd;
        }
        if (!cached_route) {
            n_zones = hzd->n_zones;
            if (n_zones > 1) {
                zones = GV_Malloc((n_zones - 1) * (n_zones - 2) / 2 + (n_zones - 1));
                HZD_MakeRoute(hzd, zones);
                cached_route = zones;
            }
        }
        zones = cached_route;
    }

    hzdMap = (HZD_HDL *)GV_Malloc(sizeof(HZD_HDL) +
        (sizeof(HZD_FLR *) * dynamic_floors) +
        (sizeof(HZD_SEG *) * dynamic_segments) +
        (sizeof(char) * dynamic_segments));
    if (hzdMap)
    {
        hzdMap->dynamic_floors = (void *)&hzdMap[1];
        hzdMap->dynamic_segments = (void *)&hzdMap->dynamic_floors[dynamic_floors];
        hzdMap->dynamic_flags = (char*)&hzdMap->dynamic_segments[dynamic_segments];

        hzdMap->max_dynamic_segments = dynamic_segments;
        hzdMap->max_dynamic_floors = dynamic_floors;
        hzdMap->def = hzd;
        hzdMap->grp = &hzd->groups[areaIndex];
        hzdMap->dynamic_queue_index = 0;
        hzdMap->dynamic_floor_index = 0;
        hzdMap->route = (u_char *)zones;

        trig = hzdMap->grp->triggers;
        for (i = hzdMap->grp->n_triggers; i > 0; i--)
        {
            // stop when we find a camera (traps are stored after cameras)
            if (trig->trap.id2 == (char)-1)
            {
                break;
            }
            trig++;
        }
        hzdMap->n_cameras = i;
        hzdMap->traps = (HZD_TRP *)trig;
    }

    return hzdMap;
}

void HZD_FreeHandler(HZD_HDL *hdl)
{
    if (hdl != NULL)
    {
        GV_Free(hdl);
    }
    return;
}
