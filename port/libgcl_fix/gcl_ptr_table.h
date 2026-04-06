/* GCL 64-bit pointer table — stores pointer values that don't fit in int.
   parse.c puts an index into *value_p, callers use gcl_resolve_ptr to recover.

   This is a circular buffer for frame-local usage (if/else/while blocks).
   For persistent pointers (HZD bind blocks), use bind_ptr_table instead. */
#include <stdint.h>
#define GCL_PTR_TABLE_SIZE 4096
extern void *gcl_ptr_table[GCL_PTR_TABLE_SIZE];
extern int gcl_ptr_next;

static inline int gcl_store_ptr(void *p) {
    int idx = gcl_ptr_next++ & (GCL_PTR_TABLE_SIZE - 1);
    gcl_ptr_table[idx] = p;
    return 0x7F000000 | idx;
}

static inline void *gcl_resolve_ptr(int value) {
    if ((value & 0x7F000000) == 0x7F000000) {
        return gcl_ptr_table[value & (GCL_PTR_TABLE_SIZE - 1)];
    }
    return (void *)(intptr_t)value;
}
