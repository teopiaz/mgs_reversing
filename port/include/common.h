/**
 * Port wrapper for common.h.
 * Includes the original then fixes SECTION for Mach-O.
 */
#ifndef __PORT_COMMON_H__
#define __PORT_COMMON_H__

/* Include the original common.h from source/include/ */
#include "../../source/include/common.h"

/* Override SECTION — Mach-O doesn't support PSX ELF section names.
   The original defines: #define SECTION(x) __attribute__((section(x)))
   which fails on macOS for .bss / .sbss sections. */
#undef SECTION
#define SECTION(x) /* nothing */

/* Override STATIC_ASSERT — struct sizes differ on 64-bit due to pointer widths.
   The original defines: typedef char name[(cond)?1:-1] */
#undef STATIC_ASSERT
#define STATIC_ASSERT(cond, msg) /* disabled for port */

/* Override scratchpad macros from psxdefs.h — the original uses MIPS asm
   that can't compile on ARM64/x86_64. These are included by the original
   common.h -> psxdefs.h chain and need to be overridden here. */
extern char port_scratchpad[1024];

#undef SCRPAD_ADDR
#define SCRPAD_ADDR     ((unsigned long)(port_scratchpad))

#undef SCRPAD_SIZE

#undef getScratchAddr2
#define getScratchAddr2(type, offset) ((type *)(port_scratchpad + (offset)))

#undef SPAD_STACK_ADDR
#define SPAD_STACK_ADDR 0

#undef SetSpadStack
#define SetSpadStack(addr)   do {} while(0)

#undef ResetSpadStack
#define ResetSpadStack()     do {} while(0)

#undef GetStackAddr
#define GetStackAddr(addr)   do {} while(0)

/* getScratchAddr (word-aligned, 4-byte offset units) */
#ifndef getScratchAddr
#define getScratchAddr(offset)  ((u_long *)(port_scratchpad + (offset) * 4))
#endif

#endif /* __PORT_COMMON_H__ */
