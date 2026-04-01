#ifndef __PSX_SYS_TYPES_H__
#define __PSX_SYS_TYPES_H__

#include <stdint.h>

/* u_long, u_short, u_char are defined in port_overrides.h as
   uint32_t, uint16_t, uint8_t to match PSX 32-bit sizes. */
#ifndef u_long
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef uint32_t       u_long;
#else
/* Already defined by port_overrides.h */
typedef unsigned int   u_int;
#endif

#endif /* __PSX_SYS_TYPES_H__ */
