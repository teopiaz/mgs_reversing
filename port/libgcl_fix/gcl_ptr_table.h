/* GCL 64-bit pointer table — stores pointer values that don't fit in int.
   parse.c puts an index into *value_p, callers use GCL_PTR(value) to recover. */
#include <stdint.h>
#define GCL_PTR_TABLE_SIZE 256
extern void *gcl_ptr_table[GCL_PTR_TABLE_SIZE];
extern int gcl_ptr_next;

static inline int gcl_store_ptr(void *p) {
    int idx = gcl_ptr_next++ & (GCL_PTR_TABLE_SIZE - 1);
    gcl_ptr_table[idx] = p;
    /* Return a value that: 1) fits in int, 2) is recognizable as a table index,
       3) when cast to (char*) on 64-bit won't crash immediately.
       Use a high value that's clearly not a normal integer. */
    return 0x7F000000 | idx;
}

static inline void *gcl_resolve_ptr(int value) {
    if ((value & 0x7F000000) == 0x7F000000) {
        return gcl_ptr_table[value & (GCL_PTR_TABLE_SIZE - 1)];
    }
    return (void *)(intptr_t)value;
}
