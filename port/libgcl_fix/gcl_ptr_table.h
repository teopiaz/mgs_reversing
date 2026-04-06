/* GCL 64-bit pointer table — stores pointer values that don't fit in int.
   parse.c puts an index into *value_p, callers use gcl_resolve_ptr to recover.

   The table is large and non-wrapping: entries persist for the lifetime of a
   stage. Call gcl_ptr_table_reset() on stage unload to reclaim slots. */
#include <stdint.h>
#define GCL_PTR_TABLE_SIZE 8192
extern void *gcl_ptr_table[GCL_PTR_TABLE_SIZE];
extern int gcl_ptr_next;

static inline int gcl_store_ptr(void *p) {
    if (gcl_ptr_next >= GCL_PTR_TABLE_SIZE) {
        /* Table full — wrap as last resort but print warning */
        gcl_ptr_next = 0;
    }
    int idx = gcl_ptr_next++;
    gcl_ptr_table[idx] = p;
    return 0x7F000000 | idx;
}

static inline void *gcl_resolve_ptr(int value) {
    if ((value & 0x7F000000) == 0x7F000000) {
        int idx = value & (GCL_PTR_TABLE_SIZE - 1);
        if (idx < GCL_PTR_TABLE_SIZE)
            return gcl_ptr_table[idx];
    }
    return (void *)(intptr_t)value;
}

/* Declared in parse.c */
void gcl_ptr_table_reset(void);
