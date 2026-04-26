/**
 * Port wrapper for libgv.h.
 * Includes the original header then overrides hardcoded PSX memory addresses.
 */
#ifndef __PORT_LIBGV_H__
#define __PORT_LIBGV_H__

/* Include the original */
#include "../../source/libgv/libgv.h"

/* Override PSX RAM addresses with port memory pools */
#undef GV_NORMAL_MEMORY_TOP
#undef GV_NORMAL_MEMORY_SIZE
#undef GV_PACKET_MEMORY0_TOP
#undef GV_PACKET_MEMORY1_TOP
#undef GV_PACKET_MEMORY_SIZE

extern void *port_normal_memory;
extern void *port_packet_memory0;
extern void *port_packet_memory1;

#define GV_NORMAL_MEMORY_TOP    port_normal_memory
#define GV_NORMAL_MEMORY_SIZE   0x2000000 /* 32 MiB — fits heavy custom
                                            stages from import_stage.py.
                                            Must match port_memory.c. */
#define GV_PACKET_MEMORY0_TOP   port_packet_memory0
#define GV_PACKET_MEMORY1_TOP   port_packet_memory1
#define GV_PACKET_MEMORY_SIZE   0x80000   /* 512 KiB (port enlarged) */

#endif /* __PORT_LIBGV_H__ */
