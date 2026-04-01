#ifndef __PSX_KERNEL_H__
#define __PSX_KERNEL_H__

#include <sys/types.h>

struct ToT {
    u_long *head;
    long    size;
};

struct TCBH {
    struct TCB *entry;
    long        flag;
};

struct TCB {
    long   status;
    long   mode;
    u_long reg[32];    /* general purpose registers */
    u_long epc;
    u_long hi, lo;
    u_long sr, cause;
    u_long unused[9];
    long   system[6];
};

/* Kernel thread status */
#define TcbMOD  0x4000
#define TcbACT  0x4001

#endif /* __PSX_KERNEL_H__ */
