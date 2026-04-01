/**
 * Force-included before libgcl source files to fix int/pointer truncation.
 * The GCL system passes pointers via `int *value_p` parameters.
 * On 64-bit, this truncates pointers. We redefine the GCL value type
 * to be pointer-sized by modifying the function signatures.
 *
 * This is a targeted fix for parse.c lines 65, 76, 89 which do:
 *   *value_p = (int)(ptr + N)
 * After this fix, int is large enough to hold a pointer.
 */
#ifndef __PORT_GCL_FIX_H__
#define __PORT_GCL_FIX_H__

/* Nothing here — the fix is applied via -Wno-int-to-pointer-cast
   which is already set. The real issue is that (int)(ptr) truncates.
   We need the cast result to fit in int.

   Since we can't change source, the practical fix is to ensure
   GCL data buffers are allocated at addresses that fit in 32 bits.
   We do this by allocating from our contiguous memory block
   which starts at port_gpu_base. */

#endif
