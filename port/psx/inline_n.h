/* clang-format off */

/*
 * Port replacement for PSX GTE inline assembly macros.
 * All operations use a software GTE state struct instead of COP2 registers.
 */

#ifndef __PORT_INLINE_N_H__
#define __PORT_INLINE_N_H__

#include <libgte.h>

/*---------------------------------------------------------------------------*/
/* Software GTE state                                                        */
/*---------------------------------------------------------------------------*/

typedef struct {
    /* Data registers */
    SVECTOR V0, V1, V2;         /* Input vectors (COP2 r0-5) */
    CVECTOR RGBC;               /* Input color (COP2 r6) */
    long    OTZ;                /* Average Z (COP2 r7) */
    long    IR0, IR1, IR2, IR3; /* Intermediate results (COP2 r8-11) */
    DVECTOR SXY0, SXY1, SXY2;  /* Screen XY FIFO (COP2 r12-14) */
    long    SZ0, SZ1, SZ2, SZ3;/* Screen Z FIFO (COP2 r16-19) */
    CVECTOR RGB0, RGB1, RGB2;   /* Color FIFO (COP2 r20-22) */
    long    RES1;               /* Prohibited (COP2 r23) */
    long    MAC0;               /* Sum of products (COP2 r24) */
    long    MAC1, MAC2, MAC3;   /* Sum of products (COP2 r25-27) */
    long    IRGB, ORGB;        /* Color conversion (COP2 r28-29) */
    long    LZCS, LZCR;        /* Leading zero count (COP2 r30-31) */
    long    FLAG;               /* Overflow flags (COP2 r63) */

    /* Control registers */
    MATRIX  R;                  /* Rotation matrix (COP2 c0-4) */
    long    TRX, TRY, TRZ;     /* Translation vector (COP2 c5-7) */
    MATRIX  L;                  /* Light matrix (COP2 c8-12) */
    long    RBK, GBK, BBK;     /* Background color (COP2 c13-15) */
    MATRIX  LR;                 /* Light color matrix (COP2 c16-20) */
    long    RFC, GFC, BFC;     /* Far color (COP2 c21-23) */
    long    OFX, OFY;          /* Screen offset (COP2 c24-25) */
    long    H;                  /* Projection plane distance (COP2 c26) */
    long    DQA;                /* Depth que parameter A (COP2 c27) */
    long    DQB;                /* Depth que parameter B (COP2 c28) */
    long    ZSF3, ZSF4;        /* Average Z scale factors (COP2 c29-30) */
} GTE_State;

extern GTE_State gte_state;

/*---------------------------------------------------------------------------*/
/* GTE core operations (implemented in gte_math.c)                           */
/*---------------------------------------------------------------------------*/

void gte_op_rtps(void);     /* Rotate-translate-perspective single */
void gte_op_rtpt(void);     /* Rotate-translate-perspective triple */
void gte_op_rt(void);       /* Rotate-translate (no perspective) */
void gte_op_rtv0(void);
void gte_op_rtv1(void);
void gte_op_rtv2(void);
void gte_op_rtir(void);
void gte_op_rtir_sf0(void);
void gte_op_nclip(void);    /* Normal clipping */
void gte_op_avsz3(void);    /* Average Z for 3 values */
void gte_op_avsz4(void);    /* Average Z for 4 values */
void gte_op_ncs(void);      /* Normal color single */
void gte_op_nct(void);      /* Normal color triple */
void gte_op_ncds(void);     /* Normal color depth single */
void gte_op_ncdt(void);     /* Normal color depth triple */
void gte_op_nccs(void);
void gte_op_ncct(void);
void gte_op_cc(void);
void gte_op_cdp(void);
void gte_op_dpcs(void);
void gte_op_dpct(void);
void gte_op_dpcl(void);
void gte_op_intpl(void);
void gte_op_sqr12(void);
void gte_op_sqr0(void);
void gte_op_op12(void);
void gte_op_op0(void);
void gte_op_gpf12(void);
void gte_op_gpf0(void);
void gte_op_gpl12(void);
void gte_op_gpl0(void);
void gte_op_ll(void);
void gte_op_mvmva(int sf, int mx, int v, int cv, int lm);

/* Rotation variants with different vector/matrix combos */
void gte_op_rtv0tr(void);
void gte_op_rtv1tr(void);
void gte_op_rtv2tr(void);
void gte_op_rtirtr(void);
void gte_op_rtv0bk(void);
void gte_op_rtv1bk(void);
void gte_op_rtv2bk(void);
void gte_op_rtirbk(void);

/* Light matrix variants */
void gte_op_llv0(void);
void gte_op_llv1(void);
void gte_op_llv2(void);
void gte_op_llir(void);
void gte_op_llv0tr(void);
void gte_op_llv1tr(void);
void gte_op_llv2tr(void);
void gte_op_llirtr(void);
void gte_op_llv0bk(void);
void gte_op_llv1bk(void);
void gte_op_llv2bk(void);
void gte_op_llirbk(void);

/* Color matrix variants */
void gte_op_lcv0(void);
void gte_op_lcv1(void);
void gte_op_lcv2(void);
void gte_op_lcir(void);
void gte_op_lcv0tr(void);
void gte_op_lcv1tr(void);
void gte_op_lcv2tr(void);
void gte_op_lcirtr(void);
void gte_op_lcv0bk(void);
void gte_op_lcv1bk(void);
void gte_op_lcv2bk(void);
void gte_op_lcirbk(void);
void gte_op_lc(void);

/*---------------------------------------------------------------------------*/
/* Vector load macros                                                        */
/*---------------------------------------------------------------------------*/

#define gte_ldv0(r0)    do { const SVECTOR *_v = (const SVECTOR *)(r0); gte_state.V0 = *_v; } while(0)
#define gte_ldv1(r0)    do { const SVECTOR *_v = (const SVECTOR *)(r0); gte_state.V1 = *_v; } while(0)
#define gte_ldv2(r0)    do { const SVECTOR *_v = (const SVECTOR *)(r0); gte_state.V2 = *_v; } while(0)

#define gte_ldv3(r0, r1, r2)  do { \
    gte_state.V0 = *(const SVECTOR *)(r0); \
    gte_state.V1 = *(const SVECTOR *)(r1); \
    gte_state.V2 = *(const SVECTOR *)(r2); \
} while(0)

#define gte_ldv3c(r0)  do { \
    const SVECTOR *_v = (const SVECTOR *)(r0); \
    gte_state.V0 = _v[0]; \
    gte_state.V1 = _v[1]; \
    gte_state.V2 = _v[2]; \
} while(0)

#define gte_ldv3c_vertc(r0)  do { \
    /* Loads from struct with 12-byte stride (SVECTOR + 4 pad bytes) */ \
    const char *_p = (const char *)(r0); \
    gte_state.V0 = *(const SVECTOR *)(_p + 0); \
    gte_state.V1 = *(const SVECTOR *)(_p + 12); \
    gte_state.V2 = *(const SVECTOR *)(_p + 24); \
} while(0)

#define gte_ldv01(r0, r1)  do { \
    gte_state.V0 = *(const SVECTOR *)(r0); \
    gte_state.V1 = *(const SVECTOR *)(r1); \
} while(0)

#define gte_ldv01c(r0)  do { \
    const SVECTOR *_v = (const SVECTOR *)(r0); \
    gte_state.V0 = _v[0]; \
    gte_state.V1 = _v[1]; \
} while(0)

/*---------------------------------------------------------------------------*/
/* Color load macros                                                         */
/*---------------------------------------------------------------------------*/

#define gte_ldrgb(r0)   do { gte_state.RGBC = *(const CVECTOR *)(r0); } while(0)

#define gte_ldrgb3(r0, r1, r2)  do { \
    gte_state.RGB0 = *(const CVECTOR *)(r0); \
    gte_state.RGB1 = *(const CVECTOR *)(r1); \
    gte_state.RGB2 = *(const CVECTOR *)(r2); \
} while(0)

#define gte_ldrgb3c(r0)  do { \
    const CVECTOR *_c = (const CVECTOR *)(r0); \
    gte_state.RGB0 = _c[0]; \
    gte_state.RGB1 = _c[1]; \
    gte_state.RGB2 = _c[2]; \
} while(0)

#define gte_SetRGBcd(r0)  gte_ldrgb(r0)

/*---------------------------------------------------------------------------*/
/* Long vector / misc load macros                                            */
/*---------------------------------------------------------------------------*/

#define gte_ldlv0(r0)   do { \
    const VECTOR *_v = (const VECTOR *)(r0); \
    gte_state.IR1 = _v->vx; gte_state.IR2 = _v->vy; gte_state.IR3 = _v->vz; \
} while(0)

#define gte_ldlvl(r0)   do { \
    const VECTOR *_v = (const VECTOR *)(r0); \
    gte_state.MAC1 = _v->vx; gte_state.MAC2 = _v->vy; gte_state.MAC3 = _v->vz; \
} while(0)

#define gte_ldsv(r0)    gte_ldlv0(r0)  /* alias */

#define gte_ldbv(r0)    do { \
    const CVECTOR *_c = (const CVECTOR *)(r0); \
    gte_state.IR1 = _c->r; gte_state.IR2 = _c->g; gte_state.IR3 = _c->b; \
} while(0)

#define gte_ldcv(r0)    gte_ldbv(r0)

#define gte_ldclmv(r0)  do { \
    const short *_s = (const short *)(r0); \
    gte_state.IR1 = _s[0]; gte_state.IR2 = _s[3]; gte_state.IR3 = _s[6]; \
} while(0)

#define gte_lddp(r0)    do { gte_state.IR0 = (long)(r0); } while(0)

/*---------------------------------------------------------------------------*/
/* Screen coordinate load macros                                             */
/*---------------------------------------------------------------------------*/

#define gte_ldsxy0(r0)  do { gte_state.SXY0 = *(const DVECTOR *)(r0); } while(0)
#define gte_ldsxy1(r0)  do { gte_state.SXY1 = *(const DVECTOR *)(r0); } while(0)
#define gte_ldsxy2(r0)  do { gte_state.SXY2 = *(const DVECTOR *)(r0); } while(0)

/* gte_ldsxy3 — loads three packed (x,y) short pairs as VALUES into SXY FIFO.
   PSX: mtc2 r0,$12; mtc2 r1,$13; mtc2 r2,$14 — these are values, not pointers. */
#define gte_ldsxy3(r0, r1, r2) do { \
    long _v0 = (long)(r0), _v1 = (long)(r1), _v2 = (long)(r2); \
    memcpy(&gte_state.SXY0, &_v0, sizeof(DVECTOR)); \
    memcpy(&gte_state.SXY1, &_v1, sizeof(DVECTOR)); \
    memcpy(&gte_state.SXY2, &_v2, sizeof(DVECTOR)); \
} while(0)

#define gte_ldsxy3c(r0) do { \
    const DVECTOR *_d = (const DVECTOR *)(r0); \
    gte_state.SXY0 = _d[0]; gte_state.SXY1 = _d[1]; gte_state.SXY2 = _d[2]; \
} while(0)

#define gte_ldsz3(r0, r1, r2) do { \
    gte_state.SZ1 = *(const long *)(r0); \
    gte_state.SZ2 = *(const long *)(r1); \
    gte_state.SZ3 = *(const long *)(r2); \
} while(0)

#define gte_ldsz4(r0, r1, r2, r3) do { \
    gte_state.SZ0 = *(const long *)(r0); \
    gte_state.SZ1 = *(const long *)(r1); \
    gte_state.SZ2 = *(const long *)(r2); \
    gte_state.SZ3 = *(const long *)(r3); \
} while(0)

/*---------------------------------------------------------------------------*/
/* Matrix / control register load macros                                     */
/*---------------------------------------------------------------------------*/

#define gte_SetRotMatrix(r0)   do { \
    const MATRIX *_m = (const MATRIX *)(r0); \
    int _i, _j; \
    for (_i = 0; _i < 3; _i++) for (_j = 0; _j < 3; _j++) \
        gte_state.R.m[_i][_j] = _m->m[_i][_j]; \
} while(0)

#define gte_SetTransMatrix(r0) do { \
    const MATRIX *_m = (const MATRIX *)(r0); \
    gte_state.TRX = _m->t[0]; gte_state.TRY = _m->t[1]; gte_state.TRZ = _m->t[2]; \
} while(0)

#define gte_SetLightMatrix(r0) do { \
    const MATRIX *_m = (const MATRIX *)(r0); \
    int _i, _j; \
    for (_i = 0; _i < 3; _i++) for (_j = 0; _j < 3; _j++) \
        gte_state.L.m[_i][_j] = _m->m[_i][_j]; \
} while(0)

#define gte_SetColorMatrix(r0) do { \
    const MATRIX *_m = (const MATRIX *)(r0); \
    int _i, _j; \
    for (_i = 0; _i < 3; _i++) for (_j = 0; _j < 3; _j++) \
        gte_state.LR.m[_i][_j] = _m->m[_i][_j]; \
} while(0)

#define gte_SetTransVector(r0) do { \
    const VECTOR *_v = (const VECTOR *)(r0); \
    gte_state.TRX = _v->vx; gte_state.TRY = _v->vy; gte_state.TRZ = _v->vz; \
} while(0)

#define gte_ldtr(r0, r1, r2) do { \
    gte_state.TRX = (long)(r0); gte_state.TRY = (long)(r1); gte_state.TRZ = (long)(r2); \
} while(0)

#define gte_SetBackColor(r0, r1, r2) do { \
    gte_state.RBK = (long)(r0) << 4; gte_state.GBK = (long)(r1) << 4; gte_state.BBK = (long)(r2) << 4; \
} while(0)

#define gte_ldbkdir(r0, r1, r2) gte_SetBackColor(r0, r1, r2)

#define gte_SetFarColor(r0, r1, r2) do { \
    gte_state.RFC = (long)(r0) << 4; gte_state.GFC = (long)(r1) << 4; gte_state.BFC = (long)(r2) << 4; \
} while(0)

#define gte_ldfcdir(r0, r1, r2) gte_SetFarColor(r0, r1, r2)

#define gte_SetGeomOffset(r0, r1) do { \
    gte_state.OFX = (long)(r0) << 16; gte_state.OFY = (long)(r1) << 16; \
} while(0)

#define gte_SetGeomScreen(r0) do { \
    gte_state.H = (long)(r0); \
} while(0)

#define gte_ldsvrtrow0(r0)  gte_SetRotMatrix(r0)
#define gte_ldsvllrow0(r0)  gte_SetLightMatrix(r0)
#define gte_ldsvlcrow0(r0)  gte_SetColorMatrix(r0)

/*---------------------------------------------------------------------------*/
/* Interpolation UV / byte / short vector load macros                        */
/*---------------------------------------------------------------------------*/

#define gte_ld_intpol_uv0(r0) do { \
    const u_char *_p = (const u_char *)(r0); \
    gte_state.IR1 = _p[0]; gte_state.IR2 = _p[1]; gte_state.IR3 = 0; \
} while(0)
#define gte_ld_intpol_uv1(r0) gte_ld_intpol_uv0(r0)
#define gte_ld_intpol_bv0(r0) gte_ldbv(r0)
#define gte_ld_intpol_bv1(r0) gte_ldbv(r0)
#define gte_ld_intpol_sv0(r0) do { \
    const SVECTOR *_v = (const SVECTOR *)(r0); \
    gte_state.IR1 = _v->vx; gte_state.IR2 = _v->vy; gte_state.IR3 = _v->vz; \
} while(0)
#define gte_ld_intpol_sv1(r0) gte_ld_intpol_sv0(r0)

#define gte_ldfc(r0) do { \
    const VECTOR *_v = (const VECTOR *)(r0); \
    gte_state.RFC = _v->vx; gte_state.GFC = _v->vy; gte_state.BFC = _v->vz; \
} while(0)

#define gte_ldopv1(r0)  gte_ldlv0(r0)
#define gte_ldopv2(r0)  do { \
    const VECTOR *_v = (const VECTOR *)(r0); \
    gte_state.IR1 = _v->vx; gte_state.IR2 = _v->vy; gte_state.IR3 = _v->vz; \
} while(0)
#define gte_ldopv1SV(r0) gte_ld_intpol_sv0(r0)
#define gte_ldopv2SV(r0) gte_ld_intpol_sv0(r0)

/* gte_ldlzc — loads a VALUE into LZCS (COP2 r30). PSX: mtc2 r0, $30 */
#define gte_ldlzc(r0)   do { \
    long _lzcs_val = (long)(r0); \
    gte_state.LZCS = _lzcs_val; \
    /* Compute LZCR: count leading zeros/ones */ \
    if (_lzcs_val > 0) { \
        int _n = 0; long _v = _lzcs_val; \
        while (_n < 32 && !(_v & 0x80000000L)) { _n++; _v <<= 1; } \
        gte_state.LZCR = _n; \
    } else if (_lzcs_val < 0) { \
        int _n = 0; long _v = _lzcs_val; \
        while (_n < 32 && (_v & 0x80000000L)) { _n++; _v <<= 1; } \
        gte_state.LZCR = _n; \
    } else { \
        gte_state.LZCR = 32; \
    } \
} while(0)

/*---------------------------------------------------------------------------*/
/* GTE compute operations                                                    */
/*---------------------------------------------------------------------------*/

#define gte_rtps()      gte_op_rtps()
#define gte_rtpt()      gte_op_rtpt()
#define gte_rt()        gte_op_rt()
#define gte_rtv0()      gte_op_rtv0()
#define gte_rtv1()      gte_op_rtv1()
#define gte_rtv2()      gte_op_rtv2()
#define gte_rtir()      gte_op_rtir()
#define gte_rtir_sf0()  gte_op_rtir_sf0()
#define gte_rtv0tr()    gte_op_rtv0tr()
#define gte_rtv1tr()    gte_op_rtv1tr()
#define gte_rtv2tr()    gte_op_rtv2tr()
#define gte_rtirtr()    gte_op_rtirtr()
#define gte_rtv0bk()    gte_op_rtv0bk()
#define gte_rtv1bk()    gte_op_rtv1bk()
#define gte_rtv2bk()    gte_op_rtv2bk()
#define gte_rtirbk()    gte_op_rtirbk()

#define gte_ll()        gte_op_ll()
#define gte_llv0()      gte_op_llv0()
#define gte_llv1()      gte_op_llv1()
#define gte_llv2()      gte_op_llv2()
#define gte_llir()      gte_op_llir()
#define gte_llv0tr()    gte_op_llv0tr()
#define gte_llv1tr()    gte_op_llv1tr()
#define gte_llv2tr()    gte_op_llv2tr()
#define gte_llirtr()    gte_op_llirtr()
#define gte_llv0bk()    gte_op_llv0bk()
#define gte_llv1bk()    gte_op_llv1bk()
#define gte_llv2bk()    gte_op_llv2bk()
#define gte_llirbk()    gte_op_llirbk()

#define gte_lc()        gte_op_lc()
#define gte_lcv0()      gte_op_lcv0()
#define gte_lcv1()      gte_op_lcv1()
#define gte_lcv2()      gte_op_lcv2()
#define gte_lcir()      gte_op_lcir()
#define gte_lcv0tr()    gte_op_lcv0tr()
#define gte_lcv1tr()    gte_op_lcv1tr()
#define gte_lcv2tr()    gte_op_lcv2tr()
#define gte_lcirtr()    gte_op_lcirtr()
#define gte_lcv0bk()    gte_op_lcv0bk()
#define gte_lcv1bk()    gte_op_lcv1bk()
#define gte_lcv2bk()    gte_op_lcv2bk()
#define gte_lcirbk()    gte_op_lcirbk()

#define gte_dpcl()      gte_op_dpcl()
#define gte_dpcs()      gte_op_dpcs()
#define gte_dpct()      gte_op_dpct()
#define gte_intpl()     gte_op_intpl()
#define gte_sqr12()     gte_op_sqr12()
#define gte_sqr0()      gte_op_sqr0()

#define gte_ncs()       gte_op_ncs()
#define gte_nct()       gte_op_nct()
#define gte_ncds()      gte_op_ncds()
#define gte_ncdt()      gte_op_ncdt()
#define gte_nccs()      gte_op_nccs()
#define gte_ncct()      gte_op_ncct()
#define gte_cdp()       gte_op_cdp()
#define gte_cc()        gte_op_cc()

#define gte_nclip()     gte_op_nclip()
#define gte_avsz3()     gte_op_avsz3()
#define gte_avsz4()     gte_op_avsz4()

#define gte_op12()      gte_op_op12()
#define gte_op0()       gte_op_op0()
#define gte_gpf12()     gte_op_gpf12()
#define gte_gpf0()      gte_op_gpf0()
#define gte_gpl12()     gte_op_gpl12()
#define gte_gpl0()      gte_op_gpl0()

#define gte_mvmva_core(r0)    /* TODO: decode opcode and call gte_op_mvmva */
#define gte_mvmva(sf,mx,v,cv,lm)  gte_op_mvmva(sf,mx,v,cv,lm)

/* _b variants are identical (just different instruction encoding) */
#define gte_rtps_b()    gte_op_rtps()
#define gte_rtpt_b()    gte_op_rtpt()
#define gte_rt_b()      gte_op_rt()
#define gte_rtv0_b()    gte_op_rtv0()
#define gte_rtv1_b()    gte_op_rtv1()
#define gte_rtv2_b()    gte_op_rtv2()
#define gte_rtir_b()    gte_op_rtir()
#define gte_rtir_sf0_b() gte_op_rtir_sf0()
#define gte_rtv0tr_b()  gte_op_rtv0tr()
#define gte_rtv1tr_b()  gte_op_rtv1tr()
#define gte_rtv2tr_b()  gte_op_rtv2tr()
#define gte_rtirtr_b()  gte_op_rtirtr()
#define gte_rtv0bk_b()  gte_op_rtv0bk()
#define gte_rtv1bk_b()  gte_op_rtv1bk()
#define gte_rtv2bk_b()  gte_op_rtv2bk()
#define gte_rtirbk_b()  gte_op_rtirbk()
#define gte_ll_b()      gte_op_ll()
#define gte_llv0_b()    gte_op_llv0()
#define gte_llv1_b()    gte_op_llv1()
#define gte_llv2_b()    gte_op_llv2()
#define gte_llir_b()    gte_op_llir()
#define gte_llv0tr_b()  gte_op_llv0tr()
#define gte_llv1tr_b()  gte_op_llv1tr()
#define gte_llv2tr_b()  gte_op_llv2tr()
#define gte_llirtr_b()  gte_op_llirtr()
#define gte_llv0bk_b()  gte_op_llv0bk()
#define gte_llv1bk_b()  gte_op_llv1bk()
#define gte_llv2bk_b()  gte_op_llv2bk()
#define gte_llirbk_b()  gte_op_llirbk()
#define gte_lc_b()      gte_op_lc()
#define gte_lcv0_b()    gte_op_lcv0()
#define gte_lcv1_b()    gte_op_lcv1()
#define gte_lcv2_b()    gte_op_lcv2()
#define gte_lcir_b()    gte_op_lcir()
#define gte_lcv0tr_b()  gte_op_lcv0tr()
#define gte_lcv1tr_b()  gte_op_lcv1tr()
#define gte_lcv2tr_b()  gte_op_lcv2tr()
#define gte_lcirtr_b()  gte_op_lcirtr()
#define gte_lcv0bk_b()  gte_op_lcv0bk()
#define gte_lcv1bk_b()  gte_op_lcv1bk()
#define gte_lcv2bk_b()  gte_op_lcv2bk()
#define gte_lcirbk_b()  gte_op_lcirbk()
#define gte_dpcl_b()    gte_op_dpcl()
#define gte_dpcs_b()    gte_op_dpcs()
#define gte_dpct_b()    gte_op_dpct()
#define gte_intpl_b()   gte_op_intpl()
#define gte_sqr12_b()   gte_op_sqr12()
#define gte_sqr0_b()    gte_op_sqr0()
#define gte_ncs_b()     gte_op_ncs()
#define gte_nct_b()     gte_op_nct()
#define gte_ncds_b()    gte_op_ncds()
#define gte_ncdt_b()    gte_op_ncdt()
#define gte_nccs_b()    gte_op_nccs()
#define gte_ncct_b()    gte_op_ncct()
#define gte_cdp_b()     gte_op_cdp()
#define gte_cc_b()      gte_op_cc()
#define gte_nclip_b()   gte_op_nclip()
#define gte_avsz3_b()   gte_op_avsz3()
#define gte_avsz4_b()   gte_op_avsz4()
#define gte_op12_b()    gte_op_op12()
#define gte_op0_b()     gte_op_op0()
#define gte_gpf12_b()   gte_op_gpf12()
#define gte_gpf0_b()    gte_op_gpf0()
#define gte_gpl12_b()   gte_op_gpl12()
#define gte_gpl0_b()    gte_op_gpl0()
#define gte_mvmva_core_b(r0)   /* TODO */
#define gte_mvmva_b(sf,mx,v,cv,lm)  gte_op_mvmva(sf,mx,v,cv,lm)

/*---------------------------------------------------------------------------*/
/* Store macros                                                              */
/*---------------------------------------------------------------------------*/

#define gte_stsxy(r0)   do { *(DVECTOR *)(r0) = gte_state.SXY2; } while(0)
#define gte_stsxy0(r0)  do { *(DVECTOR *)(r0) = gte_state.SXY0; } while(0)
#define gte_stsxy1(r0)  do { *(DVECTOR *)(r0) = gte_state.SXY1; } while(0)
#define gte_stsxy2(r0)  do { *(DVECTOR *)(r0) = gte_state.SXY2; } while(0)

#define gte_stsxy3(r0, r1, r2) do { \
    *(DVECTOR *)(r0) = gte_state.SXY0; \
    *(DVECTOR *)(r1) = gte_state.SXY1; \
    *(DVECTOR *)(r2) = gte_state.SXY2; \
} while(0)

#define gte_stsxy3c(r0) do { \
    DVECTOR *_d = (DVECTOR *)(r0); \
    _d[0] = gte_state.SXY0; _d[1] = gte_state.SXY1; _d[2] = gte_state.SXY2; \
} while(0)

#define gte_stsxy01(r0, r1) do { \
    *(DVECTOR *)(r0) = gte_state.SXY0; \
    *(DVECTOR *)(r1) = gte_state.SXY1; \
} while(0)

#define gte_stsxy01c(r0) do { \
    DVECTOR *_d = (DVECTOR *)(r0); \
    _d[0] = gte_state.SXY0; _d[1] = gte_state.SXY1; \
} while(0)

/* Store SXY3 into specific primitive types (write at known offsets) */
#define gte_stsxy3_f3(r0) do { \
    short *_p = (short *)((char *)(r0) + 8); \
    _p[0] = gte_state.SXY0.vx; _p[1] = gte_state.SXY0.vy; \
    _p[2] = gte_state.SXY1.vx; _p[3] = gte_state.SXY1.vy; \
    _p[4] = gte_state.SXY2.vx; _p[5] = gte_state.SXY2.vy; \
} while(0)
#define gte_stsxy3_g3(r0)   gte_stsxy3_f3(r0)
#define gte_stsxy3_ft3(r0)  gte_stsxy3_f3(r0)
#define gte_stsxy3_gt3(r0)  gte_stsxy3_f3(r0)
#define gte_stsxy3_f4(r0)   gte_stsxy3_f3(r0)
#define gte_stsxy3_g4(r0)   gte_stsxy3_f3(r0)
#define gte_stsxy3_ft4(r0)  gte_stsxy3_f3(r0)
#define gte_stsxy3_gt4(r0)  gte_stsxy3_f3(r0)

/* Store depth / Z values */
#define gte_stdp(r0)    do { *(long *)(r0) = gte_state.IR0; } while(0)
#define gte_stsz(r0)    do { *(long *)(r0) = gte_state.SZ3; } while(0)

#define gte_stsz3(r0, r1, r2) do { \
    *(long *)(r0) = gte_state.SZ1; \
    *(long *)(r1) = gte_state.SZ2; \
    *(long *)(r2) = gte_state.SZ3; \
} while(0)

#define gte_stsz4(r0, r1, r2, r3) do { \
    *(long *)(r0) = gte_state.SZ0; \
    *(long *)(r1) = gte_state.SZ1; \
    *(long *)(r2) = gte_state.SZ2; \
    *(long *)(r3) = gte_state.SZ3; \
} while(0)

#define gte_stsz3c(r0) do { \
    long *_p = (long *)(r0); \
    _p[0] = gte_state.SZ1; _p[1] = gte_state.SZ2; _p[2] = gte_state.SZ3; \
} while(0)

#define gte_stsz4c(r0) do { \
    long *_p = (long *)(r0); \
    _p[0] = gte_state.SZ0; _p[1] = gte_state.SZ1; _p[2] = gte_state.SZ2; _p[3] = gte_state.SZ3; \
} while(0)

#define gte_stszotz(r0)  do { *(long *)(r0) = gte_state.SZ0 + gte_state.OTZ; } while(0)
#define gte_stotz(r0)    do { *(long *)(r0) = gte_state.OTZ; } while(0)
#define gte_stopz(r0)    do { *(long *)(r0) = gte_state.MAC0; } while(0)

/* Store flags */
#define gte_stflg(r0)   do { *(long *)(r0) = gte_state.FLAG; } while(0)
#define gte_stflg_4(r0)  gte_stflg(r0)

/* Store vectors */
#define gte_stlvl(r0) do { \
    VECTOR *_v = (VECTOR *)(r0); \
    _v->vx = gte_state.MAC1; _v->vy = gte_state.MAC2; _v->vz = gte_state.MAC3; \
} while(0)

#define gte_stlvnl(r0) do { \
    VECTOR *_v = (VECTOR *)(r0); \
    _v->vx = gte_state.MAC1; _v->vy = gte_state.MAC2; _v->vz = gte_state.MAC3; \
} while(0)

#define gte_stlvnl0(r0) do { \
    SVECTOR *_v = (SVECTOR *)(r0); \
    _v->vx = (short)gte_state.IR1; _v->vy = (short)gte_state.IR2; _v->vz = (short)gte_state.IR3; \
} while(0)
#define gte_stlvnl1(r0) gte_stlvnl0(r0)
#define gte_stlvnl2(r0) gte_stlvnl0(r0)

#define gte_stsv(r0)    gte_stlvnl0(r0)

#define gte_stclmv(r0) do { \
    short *_s = (short *)(r0); \
    _s[0] = (short)gte_state.IR1; _s[3] = (short)gte_state.IR2; _s[6] = (short)gte_state.IR3; \
} while(0)

#define gte_stbv(r0) do { \
    CVECTOR *_c = (CVECTOR *)(r0); \
    _c->r = (u_char)gte_state.IR1; _c->g = (u_char)gte_state.IR2; _c->b = (u_char)gte_state.IR3; \
} while(0)

#define gte_stcv(r0) gte_stbv(r0)

/* Store RGB */
#define gte_strgb(r0)   do { *(CVECTOR *)(r0) = gte_state.RGB2; } while(0)

#define gte_strgb3(r0, r1, r2) do { \
    *(CVECTOR *)(r0) = gte_state.RGB0; \
    *(CVECTOR *)(r1) = gte_state.RGB1; \
    *(CVECTOR *)(r2) = gte_state.RGB2; \
} while(0)

/* Store RGB3 into specific primitive types */
#define gte_strgb3_g3(r0) do { \
    char *_p = (char *)(r0); \
    *(CVECTOR *)(_p + 4)  = gte_state.RGB0; \
    *(CVECTOR *)(_p + 12) = gte_state.RGB1; \
    *(CVECTOR *)(_p + 20) = gte_state.RGB2; \
} while(0)
#define gte_strgb3_gt3(r0) do { \
    char *_p = (char *)(r0); \
    *(CVECTOR *)(_p + 4)  = gte_state.RGB0; \
    *(CVECTOR *)(_p + 16) = gte_state.RGB1; \
    *(CVECTOR *)(_p + 28) = gte_state.RGB2; \
} while(0)
#define gte_strgb3_g4(r0)  gte_strgb3_g3(r0)
#define gte_strgb3_gt4(r0) gte_strgb3_gt3(r0)

/* Read matrices back */
#define gte_ReadRotMatrix(r0) do { \
    MATRIX *_m = (MATRIX *)(r0); \
    int _i, _j; \
    for (_i = 0; _i < 3; _i++) for (_j = 0; _j < 3; _j++) \
        _m->m[_i][_j] = gte_state.R.m[_i][_j]; \
    _m->t[0] = gte_state.TRX; _m->t[1] = gte_state.TRY; _m->t[2] = gte_state.TRZ; \
} while(0)

#define gte_ReadLightMatrix(r0) do { \
    MATRIX *_m = (MATRIX *)(r0); \
    int _i, _j; \
    for (_i = 0; _i < 3; _i++) for (_j = 0; _j < 3; _j++) \
        _m->m[_i][_j] = gte_state.L.m[_i][_j]; \
} while(0)

#define gte_ReadColorMatrix(r0) do { \
    MATRIX *_m = (MATRIX *)(r0); \
    int _i, _j; \
    for (_i = 0; _i < 3; _i++) for (_j = 0; _j < 3; _j++) \
        _m->m[_i][_j] = gte_state.LR.m[_i][_j]; \
} while(0)

#define gte_sttr(r0) do { \
    long *_p = (long *)(r0); \
    _p[0] = gte_state.TRX; _p[1] = gte_state.TRY; _p[2] = gte_state.TRZ; \
} while(0)

#define gte_ReadGeomOffset(r0, r1) do { \
    *(long *)(r0) = gte_state.OFX; *(long *)(r1) = gte_state.OFY; \
} while(0)

#define gte_ReadGeomScreen(r0) do { \
    *(long *)(r0) = gte_state.H; \
} while(0)

#define gte_stlzc(r0)   do { *(long *)(r0) = gte_state.LZCR; } while(0)

#define gte_stfc(r0) do { \
    long *_p = (long *)(r0); \
    _p[0] = gte_state.RFC; _p[1] = gte_state.GFC; _p[2] = gte_state.BFC; \
} while(0)

/*---------------------------------------------------------------------------*/
/* Misc macros                                                               */
/*---------------------------------------------------------------------------*/

#define gte_mvlvtr() do { \
    gte_state.TRX = gte_state.IR1; \
    gte_state.TRY = gte_state.IR2; \
    gte_state.TRZ = gte_state.IR3; \
} while(0)

#define gte_nop() do {} while(0)

#define gte_subdvl(r0, r1, r2) do { \
    const VECTOR *_a = (const VECTOR *)(r0); \
    const VECTOR *_b = (const VECTOR *)(r1); \
    VECTOR *_c = (VECTOR *)(r2); \
    _c->vx = _a->vx - _b->vx; _c->vy = _a->vy - _b->vy; _c->vz = _a->vz - _b->vz; \
} while(0)

#define gte_subdvd(r0, r1, r2) do { \
    const DVECTOR *_a = (const DVECTOR *)(r0); \
    const DVECTOR *_b = (const DVECTOR *)(r1); \
    DVECTOR *_c = (DVECTOR *)(r2); \
    _c->vx = _a->vx - _b->vx; _c->vy = _a->vy - _b->vy; \
} while(0)

#define gte_adddvl(r0, r1, r2) do { \
    const VECTOR *_a = (const VECTOR *)(r0); \
    const VECTOR *_b = (const VECTOR *)(r1); \
    VECTOR *_c = (VECTOR *)(r2); \
    _c->vx = _a->vx + _b->vx; _c->vy = _a->vy + _b->vy; _c->vz = _a->vz + _b->vz; \
} while(0)

#define gte_adddvd(r0, r1, r2) do { \
    const DVECTOR *_a = (const DVECTOR *)(r0); \
    const DVECTOR *_b = (const DVECTOR *)(r1); \
    DVECTOR *_c = (DVECTOR *)(r2); \
    _c->vx = _a->vx + _b->vx; _c->vy = _a->vy + _b->vy; \
} while(0)

#define gte_FlipRotMatrixX() do { \
    gte_state.R.m[1][0] = -gte_state.R.m[1][0]; \
    gte_state.R.m[1][1] = -gte_state.R.m[1][1]; \
    gte_state.R.m[1][2] = -gte_state.R.m[1][2]; \
    gte_state.R.m[2][0] = -gte_state.R.m[2][0]; \
    gte_state.R.m[2][1] = -gte_state.R.m[2][1]; \
    gte_state.R.m[2][2] = -gte_state.R.m[2][2]; \
} while(0)

#define gte_FlipTRX() do { \
    gte_state.TRY = -gte_state.TRY; \
    gte_state.TRZ = -gte_state.TRZ; \
} while(0)

/* gte_NormalClip — custom MGS function (NOT a standard GTE inline macro).
   Computes 2D cross product from three packed (x,y) short pairs.
   sxy0, sxy1, sxy2 are int-packed DVECTOR values, result stored at *out. */
static inline void gte_NormalClip(int sxy0, int sxy1, int sxy2, void *out)
{
    short x0 = (short)(sxy0 & 0xffff), y0 = (short)(sxy0 >> 16);
    short x1 = (short)(sxy1 & 0xffff), y1 = (short)(sxy1 >> 16);
    short x2 = (short)(sxy2 & 0xffff), y2 = (short)(sxy2 >> 16);
    *(int *)out = (int)((long)(x1 - x0) * (long)(y2 - y0) - (long)(x2 - x0) * (long)(y1 - y0));
}

/* CompMatrix macro - composite two matrices */
#define gte_CompMatrix(r0, r1, r2) do { \
    const MATRIX *_m0 = (const MATRIX *)(r0); \
    const MATRIX *_m1 = (const MATRIX *)(r1); \
    MATRIX *_out = (MATRIX *)(r2); \
    gte_SetRotMatrix(_m0); \
    gte_SetTransMatrix(_m0); \
    gte_op_comp_matrix(_m1, _out); \
} while(0)

void gte_op_comp_matrix(const MATRIX *m1, MATRIX *out);

/* MulRotMatrix0 macro */
#define gte_MulRotMatrix0(r0, r1, r2) do { \
    const MATRIX *_m0 = (const MATRIX *)(r0); \
    const MATRIX *_m1 = (const MATRIX *)(r1); \
    MATRIX *_out = (MATRIX *)(r2); \
    gte_SetRotMatrix(_m0); \
    gte_op_mul_rot_matrix(_m1, _out); \
} while(0)

void gte_op_mul_rot_matrix(const MATRIX *m1, MATRIX *out);

#endif /* __PORT_INLINE_N_H__ */
