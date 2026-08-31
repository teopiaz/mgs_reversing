/**
 * Port wrapper for libgv.h.
 * Includes the original header then overrides hardcoded PSX memory addresses.
 */
#ifndef __PORT_LIBGV_H__
#define __PORT_LIBGV_H__

/* Include the original */
#include "../../source/libgv/libgv.h"

/* Override PSX RAM addresses with port memory pools.
 *
 * Upstream reworked these constants (they used to be GV_NORMAL_MEMORY_TOP /
 * GV_PACKET_MEMORY0_TOP / ...); they are now derived from MEM_BOTTOM in
 * source/libgv/libgv.h and consumed directly by source/libgv/gvd.c and
 * source/libfs/fscd.c. Left un-overridden, GV_Malloc hands out PSX addresses
 * (MEM_ADDR == 0x80117000) and the first GV_NewActor segfaults.
 *
 * Sizes must stay in sync with port/port_memory.c.
 */
#undef MEM_BOTTOM
#undef PACK_SIZE
#undef MEM_SIZE
#undef PACK_ADDR0
#undef PACK_ADDR1
#undef MEM_ADDR
#undef RESIDENT_BOTTOM

extern void *port_normal_memory;
extern void *port_packet_memory0;
extern void *port_packet_memory1;

#define MEM_SIZE        0x2000000   /* 32 MiB */
#define PACK_SIZE       0x200000    /* 2 MiB each */

#define MEM_ADDR        (port_normal_memory)
#define PACK_ADDR0      (port_packet_memory0)
#define PACK_ADDR1      (port_packet_memory1)
#define MEM_BOTTOM      ((void *)((char *)port_packet_memory1 + PACK_SIZE))
#define RESIDENT_BOTTOM (MEM_ADDR)

/* Kept for the port's own call sites. */
#undef GV_NORMAL_MEMORY_TOP
#undef GV_NORMAL_MEMORY_SIZE
#undef GV_PACKET_MEMORY0_TOP
#undef GV_PACKET_MEMORY1_TOP
#undef GV_PACKET_MEMORY_SIZE

#define GV_NORMAL_MEMORY_TOP    MEM_ADDR
#define GV_NORMAL_MEMORY_SIZE   MEM_SIZE
#define GV_PACKET_MEMORY0_TOP   PACK_ADDR0
#define GV_PACKET_MEMORY1_TOP   PACK_ADDR1
#define GV_PACKET_MEMORY_SIZE   PACK_SIZE

#endif /* __PORT_LIBGV_H__ */
