/**
 * Port memory pool allocation.
 * Replaces hardcoded PSX RAM addresses with malloc'd buffers.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Larger than original PSX sizes to accommodate 64-bit pointers and
   the stage loading pipeline which needs contiguous buffers.
   Original: normal=428KB, packet=188KB each.
   Port: 2MB normal, 512KB packet — plenty of room. */
#define GV_NORMAL_MEMORY_SIZE   0x200000  /* 2 MiB */
#define GV_PACKET_MEMORY_SIZE   0x80000   /* 512 KiB */

void *port_normal_memory;
void *port_packet_memory0;
void *port_packet_memory1;

/* GPU base pointer for offset-based OT system.
   All OT and primitive memory must be within 16MB of this base.
   We use packet_memory0 as the base since OTs are allocated there. */
char *port_gpu_base = NULL;

/* Single contiguous block for all memory — needed so OT offset-based
   addressing works (all pointers must be within 16MB of gpu_base). */
static void *port_memory_block = NULL;

int port_init_memory(void)
{
    size_t total = GV_NORMAL_MEMORY_SIZE + GV_PACKET_MEMORY_SIZE * 2;
    port_memory_block = malloc(total);
    if (!port_memory_block)
    {
        fprintf(stderr, "port: failed to allocate %zu bytes\n", total);
        return -1;
    }

    /* Lay out memory regions within the single block */
    port_normal_memory  = port_memory_block;
    port_packet_memory0 = (char *)port_memory_block + GV_NORMAL_MEMORY_SIZE;
    port_packet_memory1 = (char *)port_memory_block + GV_NORMAL_MEMORY_SIZE + GV_PACKET_MEMORY_SIZE;

    /* GPU base = start of the contiguous block */
    port_gpu_base = (char *)port_memory_block;

    /* Map the PSX scratchpad at its original address (0x1f800000).
       Some decompiled code uses literal casts like *(int *)0x1f800000.
       If mmap fails, the scratchpad stays as a regular buffer and those
       literal casts will crash — but most code uses getScratchAddr2 which works. */
    {
        #include <sys/mman.h>
        void *spad = mmap((void *)0x1f800000, 4096,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          -1, 0);
        if (spad == (void *)0x1f800000)
        {
            printf("port: scratchpad mapped at 0x1f800000\n");
            /* Copy any existing scratchpad data */
            memcpy(spad, port_scratchpad, 1024);
        }
        else
        {
            printf("port: WARNING: could not map scratchpad at 0x1f800000 (%p)\n", spad);
        }
    }

    memset(port_memory_block, 0, total);

    printf("port: memory pools allocated (normal=%p, pkt0=%p, pkt1=%p)\n",
           port_normal_memory, port_packet_memory0, port_packet_memory1);
    return 0;
}

void port_shutdown_memory(void)
{
    free(port_normal_memory);
    free(port_packet_memory0);
    free(port_packet_memory1);
    port_normal_memory = NULL;
    port_packet_memory0 = NULL;
    port_packet_memory1 = NULL;
}
