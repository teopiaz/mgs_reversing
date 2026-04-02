/* clang-format off */

/*
 * Port replacement for source/include/inline_x.h
 * Replaces MIPS GTE assembly with C equivalents using gte_state.
 */

#ifndef __PORT_INLINE_X_H__
#define __PORT_INLINE_X_H__

#include "inline_n.h"

/* gte_read_opz — reads MAC0 (COP2 $24) into a variable.
   PSX: mfc2 r0, $24 */
#define gte_read_opz(r0) do { (r0) = (int)gte_state.MAC0; } while(0)

/* gte_ldVXY0/VZ0 etc. — load individual vector components as values */
#define gte_ldVXY0(r0) do { \
    long _v = (long)(r0); memcpy(&gte_state.V0, &_v, sizeof(long)); \
} while(0)

#define gte_ldVZ0(r0) do { \
    gte_state.V0.vz = (short)(long)(r0); \
} while(0)

#define gte_ldVXY1(r0) do { \
    long _v = (long)(r0); memcpy(&gte_state.V1, &_v, sizeof(long)); \
} while(0)

#define gte_ldVZ1(r0) do { \
    gte_state.V1.vz = (short)(long)(r0); \
} while(0)

#define gte_ldVXY2(r0) do { \
    long _v = (long)(r0); memcpy(&gte_state.V2, &_v, sizeof(long)); \
} while(0)

#define gte_ldVZ2(r0) do { \
    gte_state.V2.vz = (short)(long)(r0); \
} while(0)

#define gte_ldVXYZ0(r0, r1) do { \
    long _v = (long)(r0); memcpy(&gte_state.V0, &_v, sizeof(long)); \
    gte_state.V0.vz = (short)(long)(r1); \
} while(0)

#define gte_ldVXYZ1(r0, r1) do { \
    long _v = (long)(r0); memcpy(&gte_state.V1, &_v, sizeof(long)); \
    gte_state.V1.vz = (short)(long)(r1); \
} while(0)

#define gte_ldVXYZ2(r0, r1) do { \
    long _v = (long)(r0); memcpy(&gte_state.V2, &_v, sizeof(long)); \
    gte_state.V2.vz = (short)(long)(r1); \
} while(0)

#endif /* __PORT_INLINE_X_H__ */

/* clang-format on */
