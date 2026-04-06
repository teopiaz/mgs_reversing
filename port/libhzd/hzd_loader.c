/**
 * 64-bit safe HZD (collision/hazard) loader.
 * The HZD binary uses OFFSET_TO_PTR which stores base+offset in 32-bit pointer fields.
 * On 64-bit, pointer fields are 8 bytes but file data is 4 bytes packed.
 * We parse the 32-bit raw format and build proper 64-bit structs.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "libgte.h"
#include "libgv/libgv.h"
#include "libhzd/libhzd.h"

#pragma pack(push, 1)

typedef struct {
    int16_t  n_triggers;
    int16_t  n_walls;
    int16_t  n_floors;
    int16_t  n_flat_walls;
    uint32_t walls_off;
    uint32_t floors_off;
    uint32_t triggers_off;
    uint32_t wallsFlags_off;
} HZD_GRP_RAW;  /* 24 bytes on PSX */

typedef struct {
    int16_t  version;
    int16_t  min_x, min_y;
    int16_t  max_x, max_y;
    int16_t  n_groups;
    int16_t  n_zones;
    int16_t  n_routes;
    uint32_t groups_off;
    uint32_t zones_off;
    uint32_t routes_off;
} HZD_MAP_RAW;  /* 24 bytes on PSX (no ptr_access at start in file) */

#pragma pack(pop)

int HZD_LoadInitHzd(void *buf, int id)
{
    unsigned char *base = (unsigned char *)buf;
    HZD_MAP_RAW *raw = (HZD_MAP_RAW *)buf;

    if (raw->version < 2)
        printf("Warning:old version hzm\n");

    /* Allocate proper 64-bit HZD_MAP */
    int n_groups = raw->n_groups;
    int alloc_size = sizeof(HZD_MAP) + sizeof(HZD_GRP) * n_groups;
    HZD_MAP *hzm = (HZD_MAP *)GV_AllocMemory(GV_NORMAL_MEMORY, alloc_size);
    if (!hzm) {
        printf("    [hzd] Failed to allocate %d bytes\n", alloc_size);
        return 0;
    }
    memset(hzm, 0, alloc_size);

    /* Copy header fields */
    hzm->version = raw->version;
    hzm->min_x = raw->min_x;
    hzm->min_y = raw->min_y;
    hzm->max_x = raw->max_x;
    hzm->max_y = raw->max_y;
    hzm->n_groups = raw->n_groups;
    hzm->n_zones = raw->n_zones;
    hzm->n_routes = raw->n_routes;

    /* Convert offsets to pointers (relative to buf) */
    hzm->zones = raw->zones_off ? (HZD_ZON *)(base + raw->zones_off) : NULL;
    /* Convert routes — raw format has 4-byte offset for points, need 8-byte pointer */
    if (raw->routes_off && hzm->n_routes > 0) {
        typedef struct { int16_t n_points; int16_t init_point; uint32_t points_off; } HZD_PAT_RAW;
        HZD_PAT_RAW *raw_routes = (HZD_PAT_RAW *)(base + raw->routes_off);
        extern void *port_malloc(size_t size);
        HZD_PAT *routes = (HZD_PAT *)port_malloc(hzm->n_routes * sizeof(HZD_PAT));
        for (int i = 0; i < hzm->n_routes; i++) {
            routes[i].n_points = raw_routes[i].n_points;
            routes[i].init_point = raw_routes[i].init_point;
            routes[i].points = raw_routes[i].points_off ? (HZD_PTP *)(base + raw_routes[i].points_off) : NULL;
        }
        hzm->routes = routes;
    } else {
        hzm->routes = NULL;
    }

    /* Set up groups — they come right after the map header in the allocated block */
    HZD_GRP *groups = (HZD_GRP *)(hzm + 1);
    hzm->groups = groups;

    /* Parse raw groups */
    HZD_GRP_RAW *raw_groups = (HZD_GRP_RAW *)(base + raw->groups_off);
    for (int i = 0; i < n_groups; i++) {
        groups[i].n_triggers = raw_groups[i].n_triggers;
        groups[i].n_walls = raw_groups[i].n_walls;
        groups[i].n_floors = raw_groups[i].n_floors;
        groups[i].n_flat_walls = raw_groups[i].n_flat_walls;
        groups[i].walls = raw_groups[i].walls_off ? (HZD_SEG *)(base + raw_groups[i].walls_off) : NULL;
        groups[i].floors = raw_groups[i].floors_off ? (HZD_FLR *)(base + raw_groups[i].floors_off) : NULL;
        groups[i].triggers = raw_groups[i].triggers_off ? (HZD_TRG *)(base + raw_groups[i].triggers_off) : NULL;
        groups[i].wallsFlags = raw_groups[i].wallsFlags_off ? (char *)(base + raw_groups[i].wallsFlags_off) : NULL;
    }

    /* Compute name_id hashes for all non-camera traps in each group.
     * The PSX HZD_LoadInitHzd called HZD_ProcessTraps() here; since that function
     * is static in hzdd.c we inline the same logic. Without this, all trigger
     * name_id fields hold raw binary garbage and duct/trap zone detection fails. */
    for (int gi = 0; gi < n_groups; gi++) {
        HZD_TRG *trap = groups[gi].triggers;
        int n = groups[gi].n_triggers;
        for (int ti = n - 1; ti >= 0; ti--, trap++) {
            /* Camera traps have id2 == 0xFF; stop when we hit one */
            if ((signed char)trap->trap.id2 == -1)
                break;
            /* Trim name at first space (same as HZD_ProcessTraps) */
            char *s = trap->trap.name;
            for (int j = (int)sizeof(trap->trap.name) + 1; j > 0 && *s != ' '; j--)
                s++;
            *s = '\0';
            trap->trap.name_id = (u_short)GV_StrCode(trap->trap.name);
        }
    }

    /* Store in cache */
    GV_SetCache(id, hzm);

    return 1;
}
