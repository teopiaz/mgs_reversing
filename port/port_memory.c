/**
 * Port memory — 4GB-aligned mmap region for PSX pointer compatibility.
 *
 * PSX code stores pointers as 32-bit int values. By mapping the game memory
 * at a 4GB-aligned address, the low 32 bits of any pointer equal the offset
 * from the base. Truncation to int preserves the offset, and reconstructing
 * the pointer is just (base | (unsigned)offset).
 *
 * Layout:
 *   [0, 2MB)    — GV normal memory
 *   [2MB, 3MB)  — GV packet memory 0
 *   [3MB, 4MB)  — GV packet memory 1
 *   [4MB, 8MB)  — port_malloc pool (resident data, misc)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>

#define GV_NORMAL_MEMORY_SIZE   0x200000   /* 2 MiB */
#define GV_PACKET_MEMORY_SIZE   0x200000   /* 2 MiB each (PSX was 188KB; 64-bit structs ~2x larger) */
#define PORT_MALLOC_POOL_SIZE   0x400000   /* 4 MiB */
#define TOTAL_POOL_SIZE (GV_NORMAL_MEMORY_SIZE + GV_PACKET_MEMORY_SIZE * 2 + PORT_MALLOC_POOL_SIZE)

void *port_normal_memory;
void *port_packet_memory0;
void *port_packet_memory1;
char *port_gpu_base = NULL;
uintptr_t port_mem_base = 0;

static char *pm_pool_start;
static char *pm_pool_end;
static char *pm_pool_cursor;

extern char port_scratchpad[];

int port_init_memory(void)
{
    const size_t alignment = 0x100000000ULL; /* 4 GB */
    size_t reserved_size = TOTAL_POOL_SIZE + alignment;

    /* 1. Reserve a large range to guarantee a 4GB boundary exists within it */
    void *ptr = mmap(NULL, reserved_size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("port: initial reservation failed");
        return -1;
    }

    /* 2. Find the 4GB-aligned address within the reservation */
    uintptr_t raw_addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = (raw_addr + (alignment - 1)) & ~(alignment - 1);

    /* 3. Set permissions on the aligned block within the reservation.
       Don't trim leading/trailing — just mprotect the portion we need.
       The unused portions remain PROT_NONE (harmless virtual address waste). */
    if (mprotect((void *)aligned_addr, TOTAL_POOL_SIZE, PROT_READ | PROT_WRITE) != 0) {
        perror("port: mprotect failed");
        return -1;
    }

    void *block = (void *)aligned_addr;

    /* Verify: low 32 bits must be zero */
    if (((uintptr_t)block & 0xFFFFFFFF) != 0) {
        fprintf(stderr, "port: 4GB alignment failed! addr=%p\n", block);
        return -1;
    }

    port_mem_base = (uintptr_t)block;

    port_normal_memory  = block;
    port_packet_memory0 = (char *)block + GV_NORMAL_MEMORY_SIZE;
    port_packet_memory1 = (char *)block + GV_NORMAL_MEMORY_SIZE + GV_PACKET_MEMORY_SIZE;
    port_gpu_base       = (char *)block;

    pm_pool_start  = (char *)block + GV_NORMAL_MEMORY_SIZE + GV_PACKET_MEMORY_SIZE * 2;
    pm_pool_end    = pm_pool_start + PORT_MALLOC_POOL_SIZE;
    pm_pool_cursor = pm_pool_start;

    /* Scratchpad */
    {
        void *spad = mmap((void *)0x1f800000, 4096,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (spad == (void *)0x1f800000)
            memcpy(spad, port_scratchpad, 1024);
        else
            printf("port: WARNING: scratchpad mmap failed\n");
    }

    memset(block, 0, TOTAL_POOL_SIZE);

    printf("port: memory at %p (4GB-aligned, base=0x%lX)\n",
           block, (unsigned long)port_mem_base);
    printf("port: normal=%p pkt0=%p pkt1=%p malloc=%p\n",
           port_normal_memory, port_packet_memory0,
           port_packet_memory1, pm_pool_start);

    return 0;
}

/* Bump allocator within the mmap'd region */
void *port_malloc(size_t size)
{
    size = (size + 15) & ~15;
    if (pm_pool_cursor + size > pm_pool_end) {
        printf("[port_malloc] pool exhausted! (used=%ld req=%zu)\n",
               (long)(pm_pool_cursor - pm_pool_start), size);
        return malloc(size); /* fallback */
    }
    void *p = pm_pool_cursor;
    pm_pool_cursor += size;
    return p;
}

/* Pointer ↔ int: with 4GB alignment, low 32 bits == offset from base */
int port_ptr_to_int(const void *p)
{
    if (!p) return 0;
    return (int)((uintptr_t)p - port_mem_base);
}

void *port_int_to_ptr(int offset)
{
    if (!offset) return NULL;
    return (void *)(port_mem_base + (unsigned int)offset);
}

void port_shutdown_memory(void)
{
    if (port_normal_memory)
        munmap(port_normal_memory, TOTAL_POOL_SIZE);
    port_normal_memory = NULL;
    port_packet_memory0 = NULL;
    port_packet_memory1 = NULL;
}
