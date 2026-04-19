/**
 * Pointer ↔ int conversion for the port.
 *
 * PSX code stores pointers as 32-bit int values (GCL scripts, HZD binds,
 * delay exec, etc.). On 64-bit, raw pointer→int truncates the high bits.
 *
 * Solution: all game memory is at a known base address (port_mem_base).
 * Storing: offset = (int)(ptr - base)     [fits in 32 bits]
 * Loading: ptr    = (void*)(base + (unsigned int)offset)
 */
#ifndef PORT_PTR_H
#define PORT_PTR_H

#ifdef PORT_BUILD

#include <stdint.h>

extern uintptr_t port_mem_base;
extern size_t    port_mem_size;

/* Returns 1 iff p points inside the mmap'd pool. NULL returns 0. */
static inline int port_ptr_in_pool(const void *p) {
    uintptr_t v = (uintptr_t)p;
    return (v >= port_mem_base) && (v < port_mem_base + port_mem_size);
}

/* Convert a native pointer to a 32-bit offset from the memory base.
   Returns 0 for NULL. Asserts the pointer is within the pool. */
int   port_ptr_to_int(const void *p);
void *port_int_to_ptr(int offset);

#endif /* PORT_BUILD */
#endif /* PORT_PTR_H */
