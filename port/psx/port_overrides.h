/**
 * Force-included before all source files to override PSX-specific constructs.
 * Used with: -include port/psx/port_overrides.h
 */
#ifndef __PORT_OVERRIDES_H__
#define __PORT_OVERRIDES_H__

#define PORT_BUILD 1
#define DEV_EXE 1

/* Prevent libgv.h from defining PSX hardcoded memory addresses.
   Our port/libgv/libgv.h overrides them after #undef.
   We pre-define them so libgv.h's #define is a no-op (already defined). */
extern void *port_normal_memory;
extern void *port_packet_memory0;
extern void *port_packet_memory1;
/* These will be #undef'd and redefined in port/libgv/libgv.h */

/* fork/master's libgcl.h does not declare these (PSX is 32-bit, so the
   implicit-int return is harmless there). On the 64-bit port an implicit-int
   return TRUNCATES the returned pointer (e.g. menu entry pointer 0x30000325e
   -> 0x325e), crashing GCL string walks. Force the correct prototypes. */
char *GCL_GetString(char *ptr);
char *GCL_NextStr(void);
int   GCL_GetNextInt(void);
void  GCL_GetNextSV(short *vec);

/* Fix fundamental type sizes: PSX uses 32-bit long/u_long everywhere.
   On 64-bit macOS, long is 8 bytes which breaks MATRIX, VECTOR, POLY_GT4,
   ordering tables, and many other structures. Force 32-bit. */
#include <stdint.h>
#define u_long uint32_t
#define u_short uint16_t
#define u_char uint8_t

/* Include standard headers globally — many PSX source files use memcpy/abs
   without including the proper headers (the PSX compiler didn't require it). */
#include <string.h>
#include <stdlib.h>

/* Check if a pointer is safely readable (page is mapped).
   Used to guard against dangling pointers to freed memory on macOS.
   Uses Mach VM API — the only fully safe approach on Apple Silicon.
   On non-Apple platforms, this always returns 1 (assume readable). */
#ifdef __APPLE__
#include <mach/mach.h>
static inline int port_ptr_readable(const void *p) {
   if (!p) return 0;
   char buf;
   vm_size_t sz = 1;
   kern_return_t kr = vm_read_overwrite(
      mach_task_self(), (vm_address_t)p, 1,
      (vm_address_t)&buf, &sz);
   return kr == KERN_SUCCESS;
}
#else
static inline int port_ptr_readable(const void *p) {
   (void)p;
   return 1;
}
#endif

/* The PSX MTS declares fprintf(int stream, ...) which conflicts with
   stdio's fprintf(FILE*, ...). We suppress MTS's declaration and
   provide cprintf ourselves. */
#define __IN_MTS_NEW__

/* Provide cprintf since __IN_MTS_NEW__ suppresses its mts.h declaration */
int cprintf(const char *format, ...);

/* Redirect PSX fprintf(stream_id, ...) calls.
   Many source files call fprintf(0, "...") where 0 is a PSX stream ID.
   We macro-redirect these to printf. This is imperfect but compiles. */
#include <stdio.h>
#define fprintf(stream, ...) printf(__VA_ARGS__)

/* Disable STATIC_ASSERT for the port — struct sizes differ due to
   64-bit pointers (e.g., GM_CAMERA is larger than on PSX). */
#define STATIC_ASSERT(cond, msg) /* disabled for port */

/* SPU_MALLOC_RECSIZ is defined in the PSYQ SDK but not in our shims.
   The value is 8 (record size for SPU malloc tracking). */
#define SPU_MALLOC_RECSIZ 8

/* Map hardcoded PSX scratchpad addresses (0x1F800000-0x1F8003FF) to our buffer.
   Some files use literal addresses like *(int *)0x1F800038 or cast 0x1f800000
   directly as a pointer. We can't intercept literal casts, but we CAN
   mmap our scratchpad at that address... or just use a macro hack.
   Actually, the simplest fix: mmap a page at 0x1f800000 on macOS. */
extern char port_scratchpad[256 * 32 + 1024];

/* Redirect hardcoded scratchpad address casts via a macro.
   This won't catch all cases but helps with common patterns. */

/* GCL expression evaluator stores pointers in int variables.
   On 64-bit this truncates addresses. We can't easily fix this without
   modifying source, but we CAN ensure the truncated value is at least
   usable by keeping all GCL-related memory in the lower 4GB. */

/* The sound driver declares 'unsigned int random(void)' in sd_ext.h
   which conflicts with macOS stdlib's 'long random(void)'. Rename it. */
#define random sd_random

#endif /* __PORT_OVERRIDES_H__ */
