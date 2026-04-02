/**
 * Port replacement for source/include/psxdefs.h
 * Replaces PSX scratchpad memory and MIPS assembly with portable equivalents.
 */
#ifndef __PORT_PSXDEFS_H__
#define __PORT_PSXDEFS_H__

typedef long (*openevent_cb_t)();

/*---------------------------------------------------------------------------*/
/* Scratchpad replacement - 1KB static buffer instead of 0x1f800000          */
/*---------------------------------------------------------------------------*/

extern char port_scratchpad[1024];

#define SCRPAD_ADDR     ((unsigned long long)(port_scratchpad))
#define SCRPAD_SIZE     0x400

#define getScratchAddr2(type, offset) ((type *)(port_scratchpad + (offset)))

/* Stack switching macros - no-ops on port (no scratchpad stack) */
#define SPAD_STACK_ADDR 0
#define SetSpadStack(addr)   do {} while(0)
#define ResetSpadStack()     do {} while(0)
#define GetStackAddr(addr)   do {} while(0)

/* getScratchAddr (original PSX macro from libapi.h) — returns word-aligned
   address into scratchpad. offset is in 4-byte (long) units. */
#define getScratchAddr(offset)  ((u_long *)(port_scratchpad + (offset) * 4))

/*---------------------------------------------------------------------------*/
/* GPU primitive codes (portable - just constants)                            */
/*---------------------------------------------------------------------------*/

#define GPU_CODE_POLY_F3        0x20
#define GPU_CODE_POLY_FT3       0x24
#define GPU_CODE_POLY_F4        0x28
#define GPU_CODE_POLY_FT4       0x2C
#define GPU_CODE_POLY_G3        0x30
#define GPU_CODE_POLY_GT3       0x34
#define GPU_CODE_POLY_G4        0x38
#define GPU_CODE_POLY_GT4       0x3C
#define GPU_CODE_LINE_F2        0x40
#define GPU_CODE_LINE_F3        0x48
#define GPU_CODE_LINE_F4        0x4C
#define GPU_CODE_LINE_G2        0x50
#define GPU_CODE_LINE_G3        0x58
#define GPU_CODE_LINE_G4        0x5C
#define GPU_CODE_TILE           0x60
#define GPU_CODE_SPRT           0x64
#define GPU_CODE_TILE_1         0x68
#define GPU_CODE_TILE_8         0x70
#define GPU_CODE_SPRT_8         0x74
#define GPU_CODE_TILE_16        0x78
#define GPU_CODE_SPRT_16        0x7C

#endif /* __PORT_PSXDEFS_H__ */
