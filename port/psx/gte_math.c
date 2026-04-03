/**
 * Software GTE (Geometry Transform Engine) implementation.
 * All math uses fixed-point 4.12 format (ONE = 4096).
 */

#include <string.h>
#include <math.h>
#include "libgte.h"
#include "inline_n.h"

/*---------------------------------------------------------------------------*/
/* Global state                                                              */
/*---------------------------------------------------------------------------*/

GTE_State gte_state;
char port_scratchpad[1024];

/*---------------------------------------------------------------------------*/
/* Trig tables (4096 entries, 4.12 fixed-point)                              */
/*---------------------------------------------------------------------------*/

static int sin_table[4096];
static int cos_table[4096];
static int trig_initialized = 0;

static void init_trig_tables(void)
{
    if (trig_initialized) return;
    for (int i = 0; i < 4096; i++)
    {
        double angle = (double)i * (2.0 * 3.14159265358979323846) / 4096.0;
        sin_table[i] = (int)(sin(angle) * 4096.0 + 0.5);
        cos_table[i] = (int)(cos(angle) * 4096.0 + 0.5);
    }
    trig_initialized = 1;
}

/*---------------------------------------------------------------------------*/
/* Clamping helpers                                                          */
/*---------------------------------------------------------------------------*/

static long clamp_ir(long val, int lm)
{
    if (lm)
        return val < 0 ? 0 : (val > 0x7fff ? 0x7fff : val);
    else
        return val < -0x8000 ? -0x8000 : (val > 0x7fff ? 0x7fff : val);
}

static long clamp_mac_to_ir(long val)
{
    return val < -0x8000 ? -0x8000 : (val > 0x7fff ? 0x7fff : val);
}

static u_char clamp_rgb(long val)
{
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (u_char)val;
}

/*---------------------------------------------------------------------------*/
/* Helper: multiply rotation matrix by vector                                */
/*---------------------------------------------------------------------------*/

static void mat_vec_mul(const MATRIX *m, long vx, long vy, long vz)
{
    gte_state.MAC1 = (int)m->m[0][0] * vx + (int)m->m[0][1] * vy + (int)m->m[0][2] * vz;
    gte_state.MAC2 = (int)m->m[1][0] * vx + (int)m->m[1][1] * vy + (int)m->m[1][2] * vz;
    gte_state.MAC3 = (int)m->m[2][0] * vx + (int)m->m[2][1] * vy + (int)m->m[2][2] * vz;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1 >> 12);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2 >> 12);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3 >> 12);
}

static void mat_vec_mul_add_tr(const MATRIX *m, long vx, long vy, long vz)
{
    gte_state.MAC1 = ((int)gte_state.TRX << 12) + (int)m->m[0][0] * vx + (int)m->m[0][1] * vy + (int)m->m[0][2] * vz;
    gte_state.MAC2 = ((int)gte_state.TRY << 12) + (int)m->m[1][0] * vx + (int)m->m[1][1] * vy + (int)m->m[1][2] * vz;
    gte_state.MAC3 = ((int)gte_state.TRZ << 12) + (int)m->m[2][0] * vx + (int)m->m[2][1] * vy + (int)m->m[2][2] * vz;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1 >> 12);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2 >> 12);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3 >> 12);
}

static void mat_vec_mul_add_bk(const MATRIX *m, long vx, long vy, long vz)
{
    gte_state.MAC1 = gte_state.RBK * 4096L + (int)m->m[0][0] * vx + (int)m->m[0][1] * vy + (int)m->m[0][2] * vz;
    gte_state.MAC2 = gte_state.GBK * 4096L + (int)m->m[1][0] * vx + (int)m->m[1][1] * vy + (int)m->m[1][2] * vz;
    gte_state.MAC3 = gte_state.BBK * 4096L + (int)m->m[2][0] * vx + (int)m->m[2][1] * vy + (int)m->m[2][2] * vz;
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
}

/*---------------------------------------------------------------------------*/
/* Perspective projection helper                                             */
/*---------------------------------------------------------------------------*/

static void do_perspective(void)
{
    long sz3 = gte_state.SZ3;
    long h = gte_state.H;

    /* Push SXY FIFO */
    gte_state.SXY0 = gte_state.SXY1;
    gte_state.SXY1 = gte_state.SXY2;

    if (sz3 == 0) sz3 = 1; /* avoid div by zero */

    long quotient = (h * 0x20000) / sz3;
    if (quotient > 0x1ffff) quotient = 0x1ffff;

    long sx = (int)(((long long)gte_state.IR1 * quotient + gte_state.OFX) >> 16);
    long sy = (int)(((long long)gte_state.IR2 * quotient + gte_state.OFY) >> 16);

    gte_state.SXY2.vx = (short)sx;
    gte_state.SXY2.vy = (short)sy;

    /* MAC0 = depth queing */
    gte_state.MAC0 = (int)(((long long)gte_state.DQA * quotient + gte_state.DQB) >> 12);
    gte_state.IR0 = gte_state.MAC0 < 0 ? 0 : (gte_state.MAC0 > 0x1000 ? 0x1000 : gte_state.MAC0);
}

/*---------------------------------------------------------------------------*/
/* Core GTE operations                                                       */
/*---------------------------------------------------------------------------*/

void gte_op_rtps(void)
{
    /* Push SZ FIFO */
    gte_state.SZ0 = gte_state.SZ1;
    gte_state.SZ1 = gte_state.SZ2;
    gte_state.SZ2 = gte_state.SZ3;

    mat_vec_mul_add_tr(&gte_state.R, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz);

    gte_state.SZ3 = gte_state.MAC3 >> 12;
    if (gte_state.SZ3 < 0) gte_state.SZ3 = 0;

    do_perspective();
}

void gte_op_rtpt(void)
{
    SVECTOR *vecs[3] = { &gte_state.V0, &gte_state.V1, &gte_state.V2 };
    for (int i = 0; i < 3; i++)
    {
        gte_state.SZ0 = gte_state.SZ1;
        gte_state.SZ1 = gte_state.SZ2;
        gte_state.SZ2 = gte_state.SZ3;

        mat_vec_mul_add_tr(&gte_state.R, vecs[i]->vx, vecs[i]->vy, vecs[i]->vz);

        gte_state.SZ3 = gte_state.MAC3 >> 12;
        if (gte_state.SZ3 < 0) gte_state.SZ3 = 0;

        do_perspective();
    }
}

void gte_op_rt(void)
{
    /* PSX GTE RT = MVMVA(sf=1, mx=R, v=IR, cv=TR).
       gte_ldv0 copies to IR; gte_ldlv0 sets IR directly. */
    mat_vec_mul_add_tr(&gte_state.R, gte_state.IR1, gte_state.IR2, gte_state.IR3);
}

void gte_op_rtv0(void)  { mat_vec_mul(&gte_state.R, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_rtv1(void)  { mat_vec_mul(&gte_state.R, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_rtv2(void)  { mat_vec_mul(&gte_state.R, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_rtir(void)  { mat_vec_mul(&gte_state.R, gte_state.IR1, gte_state.IR2, gte_state.IR3); }

void gte_op_rtir_sf0(void)
{
    /* Same as rtir but without shift (sf=0) */
    gte_state.MAC1 = (int)gte_state.R.m[0][0] * gte_state.IR1 + (int)gte_state.R.m[0][1] * gte_state.IR2 + (int)gte_state.R.m[0][2] * gte_state.IR3;
    gte_state.MAC2 = (int)gte_state.R.m[1][0] * gte_state.IR1 + (int)gte_state.R.m[1][1] * gte_state.IR2 + (int)gte_state.R.m[1][2] * gte_state.IR3;
    gte_state.MAC3 = (int)gte_state.R.m[2][0] * gte_state.IR1 + (int)gte_state.R.m[2][1] * gte_state.IR2 + (int)gte_state.R.m[2][2] * gte_state.IR3;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
}

/* Rotation + translation variants */
void gte_op_rtv0tr(void) { mat_vec_mul_add_tr(&gte_state.R, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_rtv1tr(void) { mat_vec_mul_add_tr(&gte_state.R, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_rtv2tr(void) { mat_vec_mul_add_tr(&gte_state.R, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_rtirtr(void) { mat_vec_mul_add_tr(&gte_state.R, gte_state.IR1, gte_state.IR2, gte_state.IR3); }

/* Rotation + background color variants */
void gte_op_rtv0bk(void) { mat_vec_mul_add_bk(&gte_state.R, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_rtv1bk(void) { mat_vec_mul_add_bk(&gte_state.R, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_rtv2bk(void) { mat_vec_mul_add_bk(&gte_state.R, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_rtirbk(void) { mat_vec_mul_add_bk(&gte_state.R, gte_state.IR1, gte_state.IR2, gte_state.IR3); }

/* Light matrix variants */
void gte_op_ll(void)     { mat_vec_mul_add_bk(&gte_state.L, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_llv0(void)   { mat_vec_mul(&gte_state.L, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_llv1(void)   { mat_vec_mul(&gte_state.L, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_llv2(void)   { mat_vec_mul(&gte_state.L, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_llir(void)   { mat_vec_mul(&gte_state.L, gte_state.IR1, gte_state.IR2, gte_state.IR3); }
void gte_op_llv0tr(void) { mat_vec_mul_add_tr(&gte_state.L, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_llv1tr(void) { mat_vec_mul_add_tr(&gte_state.L, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_llv2tr(void) { mat_vec_mul_add_tr(&gte_state.L, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_llirtr(void) { mat_vec_mul_add_tr(&gte_state.L, gte_state.IR1, gte_state.IR2, gte_state.IR3); }
void gte_op_llv0bk(void) { mat_vec_mul_add_bk(&gte_state.L, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_llv1bk(void) { mat_vec_mul_add_bk(&gte_state.L, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_llv2bk(void) { mat_vec_mul_add_bk(&gte_state.L, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_llirbk(void) { mat_vec_mul_add_bk(&gte_state.L, gte_state.IR1, gte_state.IR2, gte_state.IR3); }

/* Color matrix variants */
void gte_op_lc(void)     { mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3); }
void gte_op_lcv0(void)   { mat_vec_mul(&gte_state.LR, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_lcv1(void)   { mat_vec_mul(&gte_state.LR, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_lcv2(void)   { mat_vec_mul(&gte_state.LR, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_lcir(void)   { mat_vec_mul(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3); }
void gte_op_lcv0tr(void) { mat_vec_mul_add_tr(&gte_state.LR, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_lcv1tr(void) { mat_vec_mul_add_tr(&gte_state.LR, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_lcv2tr(void) { mat_vec_mul_add_tr(&gte_state.LR, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_lcirtr(void) { mat_vec_mul_add_tr(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3); }
void gte_op_lcv0bk(void) { mat_vec_mul_add_bk(&gte_state.LR, gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }
void gte_op_lcv1bk(void) { mat_vec_mul_add_bk(&gte_state.LR, gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz); }
void gte_op_lcv2bk(void) { mat_vec_mul_add_bk(&gte_state.LR, gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz); }
void gte_op_lcirbk(void) { mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3); }

/*---------------------------------------------------------------------------*/
/* Normal clipping                                                           */
/*---------------------------------------------------------------------------*/

void gte_op_nclip(void)
{
    /* Compute in 64-bit to avoid overflow, then truncate to 32-bit MAC0 */
    long long result =
        (long long)gte_state.SXY0.vx * ((long long)gte_state.SXY1.vy - (long long)gte_state.SXY2.vy) +
        (long long)gte_state.SXY1.vx * ((long long)gte_state.SXY2.vy - (long long)gte_state.SXY0.vy) +
        (long long)gte_state.SXY2.vx * ((long long)gte_state.SXY0.vy - (long long)gte_state.SXY1.vy);
    gte_state.MAC0 = (int)result;
}

/*---------------------------------------------------------------------------*/
/* Average Z                                                                 */
/*---------------------------------------------------------------------------*/

void gte_op_avsz3(void)
{
    gte_state.MAC0 = gte_state.ZSF3 * ((int)gte_state.SZ1 + (int)gte_state.SZ2 + (int)gte_state.SZ3);
    gte_state.OTZ = gte_state.MAC0 >> 12;
    if (gte_state.OTZ < 0) gte_state.OTZ = 0;
    if (gte_state.OTZ > 0xffff) gte_state.OTZ = 0xffff;
}

void gte_op_avsz4(void)
{
    gte_state.MAC0 = gte_state.ZSF4 * ((int)gte_state.SZ0 + (int)gte_state.SZ1 + (int)gte_state.SZ2 + (int)gte_state.SZ3);
    gte_state.OTZ = gte_state.MAC0 >> 12;
    if (gte_state.OTZ < 0) gte_state.OTZ = 0;
    if (gte_state.OTZ > 0xffff) gte_state.OTZ = 0xffff;
}

/*---------------------------------------------------------------------------*/
/* Normal color operations                                                   */
/*---------------------------------------------------------------------------*/

static void push_rgb_fifo(void)
{
    gte_state.RGB0 = gte_state.RGB1;
    gte_state.RGB1 = gte_state.RGB2;
    gte_state.RGB2.r = clamp_rgb(gte_state.MAC1 >> 4);
    gte_state.RGB2.g = clamp_rgb(gte_state.MAC2 >> 4);
    gte_state.RGB2.b = clamp_rgb(gte_state.MAC3 >> 4);
    gte_state.RGB2.cd = gte_state.RGBC.cd;
}

static void do_ncs(long vx, long vy, long vz)
{
    /* L * normal -> IR */
    mat_vec_mul(&gte_state.L, vx, vy, vz);
    /* LR * IR + BK -> color */
    mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3);
    push_rgb_fifo();
}

void gte_op_ncs(void)  { do_ncs(gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }

void gte_op_nct(void)
{
    do_ncs(gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz);
    do_ncs(gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz);
    do_ncs(gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz);
}

static void do_ncds(long vx, long vy, long vz)
{
    /* L * normal -> IR */
    mat_vec_mul(&gte_state.L, vx, vy, vz);
    /* LR * IR + BK -> color */
    mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3);
    /* Interpolate with far color */
    long r = ((int)gte_state.RFC - gte_state.IR1) * gte_state.IR0;
    long g = ((int)gte_state.GFC - gte_state.IR2) * gte_state.IR0;
    long b = ((int)gte_state.BFC - gte_state.IR3) * gte_state.IR0;
    gte_state.MAC1 = (gte_state.IR1 << 12) + r;
    gte_state.MAC2 = (gte_state.IR2 << 12) + g;
    gte_state.MAC3 = (gte_state.IR3 << 12) + b;
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

void gte_op_ncds(void) { do_ncds(gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }

void gte_op_ncdt(void)
{
    do_ncds(gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz);
    do_ncds(gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz);
    do_ncds(gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz);
}

static void do_nccs(long vx, long vy, long vz)
{
    mat_vec_mul(&gte_state.L, vx, vy, vz);
    mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3);
    /* Modulate with RGBC */
    gte_state.MAC1 = ((int)gte_state.RGBC.r * gte_state.IR1) << 4;
    gte_state.MAC2 = ((int)gte_state.RGBC.g * gte_state.IR2) << 4;
    gte_state.MAC3 = ((int)gte_state.RGBC.b * gte_state.IR3) << 4;
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

void gte_op_nccs(void) { do_nccs(gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz); }

void gte_op_ncct(void)
{
    do_nccs(gte_state.V0.vx, gte_state.V0.vy, gte_state.V0.vz);
    do_nccs(gte_state.V1.vx, gte_state.V1.vy, gte_state.V1.vz);
    do_nccs(gte_state.V2.vx, gte_state.V2.vy, gte_state.V2.vz);
}

void gte_op_cc(void)
{
    mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3);
    gte_state.MAC1 = ((int)gte_state.RGBC.r * gte_state.IR1) << 4;
    gte_state.MAC2 = ((int)gte_state.RGBC.g * gte_state.IR2) << 4;
    gte_state.MAC3 = ((int)gte_state.RGBC.b * gte_state.IR3) << 4;
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

void gte_op_cdp(void)
{
    mat_vec_mul_add_bk(&gte_state.LR, gte_state.IR1, gte_state.IR2, gte_state.IR3);
    /* Modulate with RGBC + depth cue */
    long ir1 = ((int)gte_state.RGBC.r * gte_state.IR1) >> 8;
    long ir2 = ((int)gte_state.RGBC.g * gte_state.IR2) >> 8;
    long ir3 = ((int)gte_state.RGBC.b * gte_state.IR3) >> 8;
    gte_state.MAC1 = (ir1 << 12) + ((int)gte_state.RFC - ir1) * gte_state.IR0;
    gte_state.MAC2 = (ir2 << 12) + ((int)gte_state.GFC - ir2) * gte_state.IR0;
    gte_state.MAC3 = (ir3 << 12) + ((int)gte_state.BFC - ir3) * gte_state.IR0;
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

/*---------------------------------------------------------------------------*/
/* Depth cue / interpolation operations                                      */
/*---------------------------------------------------------------------------*/

void gte_op_dpcs(void)
{
    long r = (int)gte_state.RGBC.r << 16;
    long g = (int)gte_state.RGBC.g << 16;
    long b = (int)gte_state.RGBC.b << 16;
    gte_state.MAC1 = r + ((gte_state.RFC - (r >> 4)) * gte_state.IR0);
    gte_state.MAC2 = g + ((gte_state.GFC - (g >> 4)) * gte_state.IR0);
    gte_state.MAC3 = b + ((gte_state.BFC - (b >> 4)) * gte_state.IR0);
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

void gte_op_dpct(void)
{
    for (int i = 0; i < 3; i++)
        gte_op_dpcs();
}

void gte_op_dpcl(void)
{
    long r = ((int)gte_state.RGBC.r * gte_state.IR1) << 4;
    long g = ((int)gte_state.RGBC.g * gte_state.IR2) << 4;
    long b = ((int)gte_state.RGBC.b * gte_state.IR3) << 4;
    gte_state.MAC1 = r + ((gte_state.RFC - (r >> 12)) * gte_state.IR0);
    gte_state.MAC2 = g + ((gte_state.GFC - (g >> 12)) * gte_state.IR0);
    gte_state.MAC3 = b + ((gte_state.BFC - (b >> 12)) * gte_state.IR0);
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

void gte_op_intpl(void)
{
    gte_state.MAC1 = (gte_state.IR1 << 12) + ((int)gte_state.RFC - gte_state.IR1) * gte_state.IR0;
    gte_state.MAC2 = (gte_state.IR2 << 12) + ((int)gte_state.GFC - gte_state.IR2) * gte_state.IR0;
    gte_state.MAC3 = (gte_state.IR3 << 12) + ((int)gte_state.BFC - gte_state.IR3) * gte_state.IR0;
    gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, 1);
    gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, 1);
    gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, 1);
    push_rgb_fifo();
}

/*---------------------------------------------------------------------------*/
/* Misc operations                                                           */
/*---------------------------------------------------------------------------*/

void gte_op_sqr12(void)
{
    gte_state.MAC1 = ((int)gte_state.IR1 * gte_state.IR1) >> 12;
    gte_state.MAC2 = ((int)gte_state.IR2 * gte_state.IR2) >> 12;
    gte_state.MAC3 = ((int)gte_state.IR3 * gte_state.IR3) >> 12;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
}

void gte_op_sqr0(void)
{
    gte_state.MAC1 = (int)gte_state.IR1 * gte_state.IR1;
    gte_state.MAC2 = (int)gte_state.IR2 * gte_state.IR2;
    gte_state.MAC3 = (int)gte_state.IR3 * gte_state.IR3;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
}

void gte_op_op12(void)
{
    long d1 = gte_state.R.m[0][0]; /* RT11 */
    long d2 = gte_state.R.m[1][1]; /* RT22 */
    long d3 = gte_state.R.m[2][2]; /* RT33 */
    gte_state.MAC1 = (d2 * gte_state.IR3 - d3 * gte_state.IR2) >> 12;
    gte_state.MAC2 = (d3 * gte_state.IR1 - d1 * gte_state.IR3) >> 12;
    gte_state.MAC3 = (d1 * gte_state.IR2 - d2 * gte_state.IR1) >> 12;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
}

void gte_op_op0(void)
{
    long d1 = gte_state.R.m[0][0];
    long d2 = gte_state.R.m[1][1];
    long d3 = gte_state.R.m[2][2];
    gte_state.MAC1 = d2 * gte_state.IR3 - d3 * gte_state.IR2;
    gte_state.MAC2 = d3 * gte_state.IR1 - d1 * gte_state.IR3;
    gte_state.MAC3 = d1 * gte_state.IR2 - d2 * gte_state.IR1;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
}

void gte_op_gpf12(void)
{
    gte_state.MAC1 = gte_state.IR0 * gte_state.IR1;
    gte_state.MAC2 = gte_state.IR0 * gte_state.IR2;
    gte_state.MAC3 = gte_state.IR0 * gte_state.IR3;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1 >> 12);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2 >> 12);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3 >> 12);
    push_rgb_fifo();
}

void gte_op_gpf0(void)
{
    gte_state.MAC1 = gte_state.IR0 * gte_state.IR1;
    gte_state.MAC2 = gte_state.IR0 * gte_state.IR2;
    gte_state.MAC3 = gte_state.IR0 * gte_state.IR3;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
    push_rgb_fifo();
}

void gte_op_gpl12(void)
{
    gte_state.MAC1 = gte_state.MAC1 + gte_state.IR0 * gte_state.IR1;
    gte_state.MAC2 = gte_state.MAC2 + gte_state.IR0 * gte_state.IR2;
    gte_state.MAC3 = gte_state.MAC3 + gte_state.IR0 * gte_state.IR3;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1 >> 12);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2 >> 12);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3 >> 12);
    push_rgb_fifo();
}

void gte_op_gpl0(void)
{
    gte_state.MAC1 = gte_state.MAC1 + gte_state.IR0 * gte_state.IR1;
    gte_state.MAC2 = gte_state.MAC2 + gte_state.IR0 * gte_state.IR2;
    gte_state.MAC3 = gte_state.MAC3 + gte_state.IR0 * gte_state.IR3;
    gte_state.IR1 = clamp_mac_to_ir(gte_state.MAC1);
    gte_state.IR2 = clamp_mac_to_ir(gte_state.MAC2);
    gte_state.IR3 = clamp_mac_to_ir(gte_state.MAC3);
    push_rgb_fifo();
}

void gte_op_mvmva(int sf, int mx, int v, int cv, int lm)
{
    const MATRIX *m;
    long vx, vy, vz;

    switch (mx) {
        case 0: m = &gte_state.R;  break;
        case 1: m = &gte_state.L;  break;
        case 2: m = &gte_state.LR; break;
        default: m = &gte_state.R; break;
    }

    switch (v) {
        case 0: vx = gte_state.V0.vx; vy = gte_state.V0.vy; vz = gte_state.V0.vz; break;
        case 1: vx = gte_state.V1.vx; vy = gte_state.V1.vy; vz = gte_state.V1.vz; break;
        case 2: vx = gte_state.V2.vx; vy = gte_state.V2.vy; vz = gte_state.V2.vz; break;
        case 3: vx = gte_state.IR1; vy = gte_state.IR2; vz = gte_state.IR3; break;
        default: vx = vy = vz = 0; break;
    }

    switch (cv) {
        case 0: mat_vec_mul_add_tr(m, vx, vy, vz); break;
        case 1: mat_vec_mul_add_bk(m, vx, vy, vz); break;
        case 2: /* far color - similar to bk but with FC */
            mat_vec_mul(m, vx, vy, vz);
            break;
        case 3: mat_vec_mul(m, vx, vy, vz); break;
    }

    if (sf)
    {
        gte_state.IR1 = clamp_ir(gte_state.MAC1 >> 12, lm);
        gte_state.IR2 = clamp_ir(gte_state.MAC2 >> 12, lm);
        gte_state.IR3 = clamp_ir(gte_state.MAC3 >> 12, lm);
    }
    else
    {
        gte_state.IR1 = clamp_ir(gte_state.MAC1, lm);
        gte_state.IR2 = clamp_ir(gte_state.MAC2, lm);
        gte_state.IR3 = clamp_ir(gte_state.MAC3, lm);
    }
}

/*---------------------------------------------------------------------------*/
/* Matrix composite helpers (used by CompMatrix / MulRotMatrix0 macros)      */
/*---------------------------------------------------------------------------*/

void gte_op_comp_matrix(const MATRIX *m1, MATRIX *out)
{
    /* Multiply current rotation by m1's rotation columns, store in out */
    for (int col = 0; col < 3; col++)
    {
        SVECTOR v;
        v.vx = m1->m[0][col];
        v.vy = m1->m[1][col];
        v.vz = m1->m[2][col];
        mat_vec_mul(&gte_state.R, v.vx, v.vy, v.vz);
        out->m[0][col] = (short)gte_state.IR1;
        out->m[1][col] = (short)gte_state.IR2;
        out->m[2][col] = (short)gte_state.IR3;
    }
    /* Transform m1's translation by current rotation + translation */
    mat_vec_mul_add_tr(&gte_state.R, m1->t[0], m1->t[1], m1->t[2]);
    out->t[0] = gte_state.MAC1 >> 12;
    out->t[1] = gte_state.MAC2 >> 12;
    out->t[2] = gte_state.MAC3 >> 12;
}

void gte_op_mul_rot_matrix(const MATRIX *m1, MATRIX *out)
{
    for (int col = 0; col < 3; col++)
    {
        SVECTOR v;
        v.vx = m1->m[0][col];
        v.vy = m1->m[1][col];
        v.vz = m1->m[2][col];
        mat_vec_mul(&gte_state.R, v.vx, v.vy, v.vz);
        out->m[0][col] = (short)gte_state.IR1;
        out->m[1][col] = (short)gte_state.IR2;
        out->m[2][col] = (short)gte_state.IR3;
    }
}

/*---------------------------------------------------------------------------*/
/* libgte function implementations                                           */
/*---------------------------------------------------------------------------*/

void InitGeom(void)
{
    memset(&gte_state, 0, sizeof(gte_state));
    init_trig_tables();
    gte_state.H = 1000;
    gte_state.ZSF3 = (ONE / 3);
    gte_state.ZSF4 = (ONE / 4);
}

void SetGeomOffset(int ofx, int ofy)
{
    gte_state.OFX = ofx << 16;
    gte_state.OFY = ofy << 16;
}

void SetGeomScreen(int h)
{
    gte_state.H = h;
}

int rsin(int a)
{
    init_trig_tables();
    return sin_table[a & 0xfff];
}

int rcos(int a)
{
    init_trig_tables();
    return cos_table[a & 0xfff];
}

/* csin/ccos are macros aliased to rsin/rcos in libgte.h */

long ratan2(long y, long x)
{
    if (x == 0 && y == 0) return 0;
    double angle = atan2((double)y, (double)x);
    /* PSX ratan2 returns signed values in -2048..2047 range */
    int result = (int)(angle * 4096.0 / (2.0 * 3.14159265358979323846));
    /* Wrap to -2048..2047 */
    result = ((result + 2048) & 0xfff) - 2048;
    return result;
}

long SquareRoot0(long a)
{
    if (a <= 0) return 0;
    return (int)sqrt((double)a);
}

long SquareRoot12(long a)
{
    if (a <= 0) return 0;
    return (int)(sqrt((double)a / 4096.0) * 4096.0);
}

long Square0(SVECTOR *v0, VECTOR *v1)
{
    v1->vx = (int)v0->vx * v0->vx;
    v1->vy = (int)v0->vy * v0->vy;
    v1->vz = (int)v0->vz * v0->vz;
    return v1->vx + v1->vy + v1->vz;
}

long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag)
{
    gte_state.V0 = *v0;
    gte_op_rtps();
    *(short *)sxy = gte_state.SXY2.vx;
    *((short *)sxy + 1) = gte_state.SXY2.vy;
    *p = gte_state.IR0;
    *flag = gte_state.FLAG;
    return gte_state.SZ3;
}

long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                   long *sxy0, long *sxy1, long *sxy2,
                   long *p, long *flag)
{
    gte_state.V0 = *v0;
    gte_state.V1 = *v1;
    gte_state.V2 = *v2;
    gte_op_rtpt();
    *(DVECTOR *)sxy0 = gte_state.SXY0;
    *(DVECTOR *)sxy1 = gte_state.SXY1;
    *(DVECTOR *)sxy2 = gte_state.SXY2;
    *p = gte_state.IR0;
    *flag = gte_state.FLAG;
    return gte_state.SZ3;
}

void RotTrans(SVECTOR *v0, VECTOR *v1, long *flag)
{
    gte_state.V0 = *v0;
    gte_op_rt();
    v1->vx = gte_state.MAC1 >> 12;
    v1->vy = gte_state.MAC2 >> 12;
    v1->vz = gte_state.MAC3 >> 12;
    *flag = gte_state.FLAG;
}

void NormalColor(SVECTOR *v0, CVECTOR *v1)
{
    gte_state.V0 = *v0;
    gte_op_ncs();
    *v1 = gte_state.RGB2;
}

void NormalColor3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                  CVECTOR *c0, CVECTOR *c1, CVECTOR *c2)
{
    gte_state.V0 = *v0;
    gte_state.V1 = *v1;
    gte_state.V2 = *v2;
    gte_op_nct();
    *c0 = gte_state.RGB0;
    *c1 = gte_state.RGB1;
    *c2 = gte_state.RGB2;
}

void NormalColorDpq(SVECTOR *v0, CVECTOR *v1, long p, CVECTOR *v2)
{
    gte_state.V0 = *v0;
    gte_state.RGBC = *v1;
    gte_state.IR0 = p;
    gte_op_ncds();
    *v2 = gte_state.RGB2;
}

void NormalColorDpq3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                     CVECTOR *v3, long p,
                     CVECTOR *v4, CVECTOR *v5, CVECTOR *v6)
{
    gte_state.RGBC = *v3;
    gte_state.IR0 = p;
    gte_state.V0 = *v0; gte_state.V1 = *v1; gte_state.V2 = *v2;
    gte_op_ncdt();
    *v4 = gte_state.RGB0;
    *v5 = gte_state.RGB1;
    *v6 = gte_state.RGB2;
}

void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2)
{
    gte_state.V0 = *v0;
    gte_state.RGBC = *v1;
    gte_op_nccs();
    *v2 = gte_state.RGB2;
}

void NormalColorCol3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                     CVECTOR *v3, CVECTOR *v4, CVECTOR *v5, CVECTOR *v6)
{
    gte_state.RGBC = *v3;
    gte_state.V0 = *v0; gte_state.V1 = *v1; gte_state.V2 = *v2;
    gte_op_ncct();
    *v4 = gte_state.RGB0;
    *v5 = gte_state.RGB1;
    *v6 = gte_state.RGB2;
}

void LocalLight(SVECTOR *v0, VECTOR *v1)
{
    gte_state.V0 = *v0;
    gte_op_llv0();
    v1->vx = gte_state.IR1;
    v1->vy = gte_state.IR2;
    v1->vz = gte_state.IR3;
}

void LightColor(VECTOR *v0, VECTOR *v1)
{
    gte_state.IR1 = v0->vx;
    gte_state.IR2 = v0->vy;
    gte_state.IR3 = v0->vz;
    gte_op_lc();
    v1->vx = gte_state.IR1;
    v1->vy = gte_state.IR2;
    v1->vz = gte_state.IR3;
}

void SetRotMatrix(MATRIX *m)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            gte_state.R.m[i][j] = m->m[i][j];
}

void SetTransMatrix(MATRIX *m)
{
    gte_state.TRX = m->t[0];
    gte_state.TRY = m->t[1];
    gte_state.TRZ = m->t[2];
}

void SetColorMatrix(MATRIX *m)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            gte_state.LR.m[i][j] = m->m[i][j];
}

void SetLightMatrix(MATRIX *m)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            gte_state.L.m[i][j] = m->m[i][j];
}

void SetBackColor(long rbk, long gbk, long bbk)
{
    gte_state.RBK = rbk << 4;
    gte_state.GBK = gbk << 4;
    gte_state.BBK = bbk << 4;
}

void ReadRotMatrix(MATRIX *m)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m->m[i][j] = gte_state.R.m[i][j];
    m->t[0] = gte_state.TRX;
    m->t[1] = gte_state.TRY;
    m->t[2] = gte_state.TRZ;
}

MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m2->m[i][j] = (short)(
                ((int)m0->m[i][0] * m1->m[0][j] +
                 (int)m0->m[i][1] * m1->m[1][j] +
                 (int)m0->m[i][2] * m1->m[2][j]) >> 12);
    return m2;
}

MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1)
{
    return MulMatrix0(m0, m1, m0);
}

MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1)
{
    return MulMatrix0(m0, m1, m1);
}

void CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
    MulMatrix0(m0, m1, m2);
    /* Also transform translation */
    m2->t[0] = m0->t[0] + ((int)m0->m[0][0] * m1->t[0] + (int)m0->m[0][1] * m1->t[1] + (int)m0->m[0][2] * m1->t[2]) / 4096;
    m2->t[1] = m0->t[1] + ((int)m0->m[1][0] * m1->t[0] + (int)m0->m[1][1] * m1->t[1] + (int)m0->m[1][2] * m1->t[2]) / 4096;
    m2->t[2] = m0->t[2] + ((int)m0->m[2][0] * m1->t[0] + (int)m0->m[2][1] * m1->t[1] + (int)m0->m[2][2] * m1->t[2]) / 4096;
}

void CompMatrixLV(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
    CompMatrix(m0, m1, m2);
}

void TransMatrix(MATRIX *m, VECTOR *v)
{
    m->t[0] = v->vx;
    m->t[1] = v->vy;
    m->t[2] = v->vz;
}

void ScaleMatrix(MATRIX *m, VECTOR *v)
{
    for (int i = 0; i < 3; i++)
    {
        m->m[0][i] = (short)(((int)m->m[0][i] * v->vx) >> 12);
        m->m[1][i] = (short)(((int)m->m[1][i] * v->vy) >> 12);
        m->m[2][i] = (short)(((int)m->m[2][i] * v->vz) >> 12);
    }
}

void ScaleMatrixL(MATRIX *m, VECTOR *v)
{
    ScaleMatrix(m, v);
}

MATRIX *RotMatrix(SVECTOR *r, MATRIX *m)
{
    init_trig_tables();
    int sx = rsin(r->vx); int cx = rcos(r->vx);
    int sy = rsin(r->vy); int cy = rcos(r->vy);
    int sz = rsin(r->vz); int cz = rcos(r->vz);

    m->m[0][0] = (short)(((int)cy * cz) >> 12);
    m->m[0][1] = (short)((((int)sx * sy >> 12) * cz >> 12) - ((int)cx * sz >> 12));
    m->m[0][2] = (short)((((int)cx * sy >> 12) * cz >> 12) + ((int)sx * sz >> 12));
    m->m[1][0] = (short)(((int)cy * sz) >> 12);
    m->m[1][1] = (short)((((int)sx * sy >> 12) * sz >> 12) + ((int)cx * cz >> 12));
    m->m[1][2] = (short)((((int)cx * sy >> 12) * sz >> 12) - ((int)sx * cz >> 12));
    m->m[2][0] = (short)(-sy);
    m->m[2][1] = (short)(((int)sx * cy) >> 12);
    m->m[2][2] = (short)(((int)cx * cy) >> 12);
    return m;
}

MATRIX *RotMatrixX(long r, MATRIX *m)
{
    int s = rsin((int)r), c = rcos((int)r);
    for (int i = 0; i < 3; i++)
    {
        long t1 = m->m[1][i], t2 = m->m[2][i];
        m->m[1][i] = (short)((c * t1 - s * t2) >> 12);
        m->m[2][i] = (short)((s * t1 + c * t2) >> 12);
    }
    return m;
}

MATRIX *RotMatrixY(long r, MATRIX *m)
{
    int s = rsin((int)r), c = rcos((int)r);
    for (int i = 0; i < 3; i++)
    {
        long t0 = m->m[0][i], t2 = m->m[2][i];
        m->m[0][i] = (short)((c * t0 + s * t2) >> 12);
        m->m[2][i] = (short)((-s * t0 + c * t2) >> 12);
    }
    return m;
}

MATRIX *RotMatrixZ(long r, MATRIX *m)
{
    int s = rsin((int)r), c = rcos((int)r);
    for (int i = 0; i < 3; i++)
    {
        long t0 = m->m[0][i], t1 = m->m[1][i];
        m->m[0][i] = (short)((c * t0 - s * t1) >> 12);
        m->m[1][i] = (short)((s * t0 + c * t1) >> 12);
    }
    return m;
}

MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m)
{
    /* PSX GTE RotMatrixYXZ computes R = Ry(-vy) * Rx(vx) * Rz(vz)
       where Ry uses negated sin (left-handed Y rotation convention). */
    init_trig_tables();
    int sx = rsin(r->vx); int cx = rcos(r->vx);
    int sy = rsin(r->vy); int cy = rcos(r->vy);
    int sz = rsin(r->vz); int cz = rcos(r->vz);

    m->m[0][0] = (short)(((cy * cz) >> 12) + (((sy * sx) >> 12) * sz >> 12));
    m->m[0][1] = (short)((-(cy * sz) >> 12) + (((sy * sx) >> 12) * cz >> 12));
    m->m[0][2] = (short)(((sy * cx)) >> 12);
    m->m[1][0] = (short)(((cx * sz)) >> 12);
    m->m[1][1] = (short)(((cx * cz)) >> 12);
    m->m[1][2] = (short)(-sx);
    m->m[2][0] = (short)((-(sy * cz) >> 12) + (((cy * sx) >> 12) * sz >> 12));
    m->m[2][1] = (short)(((sy * sz) >> 12) + (((cy * sx) >> 12) * cz >> 12));
    m->m[2][2] = (short)(((cy * cx)) >> 12);
    return m;
}

MATRIX *RotMatrixZYX_gte(SVECTOR *r, MATRIX *m)
{
    /* For now, use the same formula as RotMatrix (XYZ order).
       TODO: verify against PSX GTE with comparison data. */
    RotMatrix(r, m);
    return m;
}

MATRIX *RotMatrixYXZ_gte(SVECTOR *r, MATRIX *m)
{
    return RotMatrixYXZ(r, m);
}

void ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1)
{
    v1->vx = ((int)m->m[0][0] * v0->vx + (int)m->m[0][1] * v0->vy + (int)m->m[0][2] * v0->vz) >> 12;
    v1->vy = ((int)m->m[1][0] * v0->vx + (int)m->m[1][1] * v0->vy + (int)m->m[1][2] * v0->vz) >> 12;
    v1->vz = ((int)m->m[2][0] * v0->vx + (int)m->m[2][1] * v0->vy + (int)m->m[2][2] * v0->vz) >> 12;
}

void ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
    v1->vx = ((int)m->m[0][0] * v0->vx + (int)m->m[0][1] * v0->vy + (int)m->m[0][2] * v0->vz) >> 12;
    v1->vy = ((int)m->m[1][0] * v0->vx + (int)m->m[1][1] * v0->vy + (int)m->m[1][2] * v0->vz) >> 12;
    v1->vz = ((int)m->m[2][0] * v0->vx + (int)m->m[2][1] * v0->vy + (int)m->m[2][2] * v0->vz) >> 12;
}

void ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1)
{
    v1->vx = (short)(((int)m->m[0][0] * v0->vx + (int)m->m[0][1] * v0->vy + (int)m->m[0][2] * v0->vz) >> 12);
    v1->vy = (short)(((int)m->m[1][0] * v0->vx + (int)m->m[1][1] * v0->vy + (int)m->m[1][2] * v0->vz) >> 12);
    v1->vz = (short)(((int)m->m[2][0] * v0->vx + (int)m->m[2][1] * v0->vy + (int)m->m[2][2] * v0->vz) >> 12);
}

void ApplyRotMatrix(SVECTOR *v0, VECTOR *v1)
{
    ApplyMatrix((MATRIX *)&gte_state.R, v0, v1);
}

void ApplyRotMatrixLV(VECTOR *v0, VECTOR *v1)
{
    ApplyMatrixLV((MATRIX *)&gte_state.R, v0, v1);
}

void OuterProduct0(SVECTOR *v0, SVECTOR *v1, VECTOR *v2)
{
    v2->vx = (int)v0->vy * v1->vz - (int)v0->vz * v1->vy;
    v2->vy = (int)v0->vz * v1->vx - (int)v0->vx * v1->vz;
    v2->vz = (int)v0->vx * v1->vy - (int)v0->vy * v1->vx;
}

void OuterProduct12(VECTOR *v0, VECTOR *v1, VECTOR *v2)
{
    v2->vx = ((long long)v0->vy * v1->vz - (long long)v0->vz * v1->vy) >> 12;
    v2->vy = ((long long)v0->vz * v1->vx - (long long)v0->vx * v1->vz) >> 12;
    v2->vz = ((long long)v0->vx * v1->vy - (long long)v0->vy * v1->vx) >> 12;
}

long VectorNormal(VECTOR *v0, VECTOR *v1)
{
    long len = SquareRoot0(v0->vx * v0->vx + v0->vy * v0->vy + v0->vz * v0->vz);
    if (len == 0) { v1->vx = v1->vy = v1->vz = 0; return 0; }
    v1->vx = (v0->vx * ONE) / len;
    v1->vy = (v0->vy * ONE) / len;
    v1->vz = (v0->vz * ONE) / len;
    return len;
}

long VectorNormalS(VECTOR *v0, SVECTOR *v1)
{
    long len = SquareRoot0(v0->vx * v0->vx + v0->vy * v0->vy + v0->vz * v0->vz);
    if (len == 0) { v1->vx = v1->vy = v1->vz = 0; return 0; }
    v1->vx = (short)((v0->vx * ONE) / len);
    v1->vy = (short)((v0->vy * ONE) / len);
    v1->vz = (short)((v0->vz * ONE) / len);
    return len;
}

long VectorNormalSS(SVECTOR *v0, SVECTOR *v1)
{
    long len = SquareRoot0((int)v0->vx * v0->vx + (int)v0->vy * v0->vy + (int)v0->vz * v0->vz);
    if (len == 0) { v1->vx = v1->vy = v1->vz = 0; return 0; }
    v1->vx = (short)(((int)v0->vx * ONE) / len);
    v1->vy = (short)(((int)v0->vy * ONE) / len);
    v1->vz = (short)(((int)v0->vz * ONE) / len);
    return len;
}

void PushMatrix(void) { /* TODO: matrix stack */ }
void PopMatrix(void)  { /* TODO: matrix stack */ }

MATRIX *MulRotMatrix(MATRIX *m)
{
    MATRIX temp;
    MulMatrix0((MATRIX *)&gte_state.R, m, &temp);
    /* PSX writes result to both GTE registers and input matrix m */
    memcpy(m->m, temp.m, sizeof(temp.m));
    memcpy(&gte_state.R, &temp, sizeof(MATRIX));
    return m;
}

void SetMulMatrix(MATRIX *m)
{
    SetRotMatrix(m);
}
