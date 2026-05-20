#ifndef __PSX_LIBGPU_H__
#define __PSX_LIBGPU_H__

#include <sys/types.h>
#include <libgte.h>

/*---------------------------------------------------------------------------*/
/* Primitive tag                                                             */
/*---------------------------------------------------------------------------*/

/* 64-bit safe OT system.
 * PSX stores 24-bit pointers in OT entries. On 64-bit, we store 24-bit OFFSETS
 * relative to a base pointer. All OT and primitive memory must be allocated
 * from this base region (set by port_gpu_set_base). */

typedef u_long P_TAG;

/* 64-bit OT pointer system.
 * Instead of storing 24-bit offsets, we use a lookup table that maps
 * 24-bit indices to full 64-bit pointers. This avoids all address
 * truncation issues. */

/* 24-bit handle space. Entries must persist across frames because chanl
 * linking (chanl0.ot[which][link].tag stores handle pointing to env1[...])
 * is set up at DG_SwapFrame time and read multiple frames later. Smaller
 * tables + per-frame reset overwrite slots while old tags still reference
 * them, breaking the OT chain (e.g. GAME OVER text disappeared because the
 * chanl-2 link resolved to a stale object). 2^24 × 8 bytes = 128 MB; at
 * ~400 addPrim/frame this wraps every ~14 hours of gameplay. */
#define PORT_OT_TABLE_SIZE (1 << 24)
extern void *port_ot_table[PORT_OT_TABLE_SIZE];
extern int port_ot_next;

/* Register a pointer and get a 24-bit handle */
static inline u_long _ptr_to_handle(const void *p) {
    if (!p) return 0x00ffffff;
    int idx = __sync_fetch_and_add(&port_ot_next, 1);
    if (idx >= PORT_OT_TABLE_SIZE) { port_ot_next = 1; idx = 0; }  /* wrap */
    port_ot_table[idx] = (void *)p;
    return (u_long)idx;
}

/* Resolve a 24-bit handle to a pointer */
static inline void *_handle_to_ptr(u_long handle) {
    if (handle == 0x00ffffff || handle >= PORT_OT_TABLE_SIZE) return NULL;
    return port_ot_table[handle];
}

#define setlen(p, _len)  (*(u_long *)(p) = (*(u_long *)(p) & 0x00ffffff) | ((u_long)(_len) << 24))
#define getlen(p)        ((*(u_long *)(p)) >> 24)
#define setaddr(p, _addr) (*(u_long *)(p) = (*(u_long *)(p) & 0xff000000) | _ptr_to_handle((void *)(_addr)))
#define getaddr(p)       ((u_long)(uintptr_t)_handle_to_ptr((*(u_long *)(p)) & 0x00ffffff))
#define setcode(p, _code) (*((u_char *)(p) + 3 + 4) = (u_char)(_code))
#define getcode(p)       (*((u_char *)(p) + 3 + 4))
#define nextPrim(p)      _handle_to_ptr((*(u_long *)(p)) & 0x00ffffff)
#define isendprim(p)     (((*(u_long *)(p)) & 0x00ffffff) == 0x00ffffff)
#define termPrim(p)      (*(u_long *)(p) = (*(u_long *)(p) & 0xff000000) | 0x00ffffff)

#define addPrim(ot, p) do { \
    u_long _old = (*(u_long *)(ot)) & 0x00ffffff; \
    *(u_long *)(p) = (*(u_long *)(p) & 0xff000000) | _old; \
    *(u_long *)(ot) = (*(u_long *)(ot) & 0xff000000) | _ptr_to_handle(p); \
} while(0)

#define addPrims(ot, p0, p1) do { \
    u_long _old = (*(u_long *)(ot)) & 0x00ffffff; \
    *(u_long *)(p1) = (*(u_long *)(p1) & 0xff000000) | _old; \
    *(u_long *)(ot) = (*(u_long *)(ot) & 0xff000000) | _ptr_to_handle(p0); \
} while(0)

#define catPrim(p0, p1) \
    (*(u_long *)(p0) = (*(u_long *)(p0) & 0xff000000) | _ptr_to_handle(p1))

#define MargePrim(p0, p1) \
    setlen(p0, getlen(p0) + getlen(p1) + 1), \
    (*(u_long *)(p0) = (*(u_long *)(p0) & 0xff000000) | ((*(u_long *)(p1)) & 0x00ffffff))

/*---------------------------------------------------------------------------*/
/* Polygon primitives                                                        */
/*---------------------------------------------------------------------------*/

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    short  x1, y1;
    short  x2, y2;
} POLY_F3;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    short  x1, y1;
    short  x2, y2;
    short  x3, y3;
} POLY_F4;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
    short  x1, y1;
    u_char u1, v1; u_short tpage;
    short  x2, y2;
    u_char u2, v2; u_short pad1;
} POLY_FT3;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
    short  x1, y1;
    u_char u1, v1; u_short tpage;
    short  x2, y2;
    u_char u2, v2; u_short pad1;
    short  x3, y3;
    u_char u3, v3; u_short pad2;
} POLY_FT4;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char r1, g1, b1, pad1;
    short  x1, y1;
    u_char r2, g2, b2, pad2;
    short  x2, y2;
} POLY_G3;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char r1, g1, b1, pad1;
    short  x1, y1;
    u_char r2, g2, b2, pad2;
    short  x2, y2;
    u_char r3, g3, b3, pad3;
    short  x3, y3;
} POLY_G4;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
    u_char r1, g1, b1, p1;
    short  x1, y1;
    u_char u1, v1; u_short tpage;
    u_char r2, g2, b2, p2;
    short  x2, y2;
    u_char u2, v2; u_short pad2;
} POLY_GT3;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
    u_char r1, g1, b1, p1;
    short  x1, y1;
    u_char u1, v1; u_short tpage;
    u_char r2, g2, b2, p2;
    short  x2, y2;
    u_char u2, v2; u_short pad2;
    u_char r3, g3, b3, p3;
    short  x3, y3;
    u_char u3, v3; u_short pad3;
} POLY_GT4;

/*---------------------------------------------------------------------------*/
/* Line primitives                                                           */
/*---------------------------------------------------------------------------*/

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    short  x1, y1;
} LINE_F2;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    short  x1, y1;
    short  x2, y2;
    u_long pad;
} LINE_F3;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    short  x1, y1;
    short  x2, y2;
    short  x3, y3;
    u_long pad;
} LINE_F4;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char r1, g1, b1, p1;
    short  x1, y1;
} LINE_G2;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char r1, g1, b1, p1;
    short  x1, y1;
    u_char r2, g2, b2, p2;
    short  x2, y2;
    u_long pad;
} LINE_G3;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char r1, g1, b1, p1;
    short  x1, y1;
    u_char r2, g2, b2, p2;
    short  x2, y2;
    u_char r3, g3, b3, p3;
    short  x3, y3;
    u_long pad;
} LINE_G4;

/*---------------------------------------------------------------------------*/
/* Sprite / Tile primitives                                                  */
/*---------------------------------------------------------------------------*/

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
    short  w, h;
} SPRT;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
} SPRT_16;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    u_char u0, v0; u_short clut;
} SPRT_8;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
    short  w, h;
} TILE;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
} TILE_1;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
} TILE_8;

typedef struct {
    u_long tag;
    u_char r0, g0, b0, code;
    short  x0, y0;
} TILE_16;

/*---------------------------------------------------------------------------*/
/* Drawing / Display environment                                             */
/*---------------------------------------------------------------------------*/

typedef struct {
    u_long tag;
    u_long code[15];
} DR_ENV;

typedef struct {
    u_long tag;
    u_long code[1];
} DR_TPAGE;

typedef struct {
    RECT   clip;
    short  ofs[2];
    RECT   tw;
    u_short tpage;
    u_char dtd;
    u_char dfe;
    u_char isbg;
    u_char r0, g0, b0;
    DR_ENV dr_env;
} DRAWENV;

typedef struct {
    RECT   disp;
    RECT   screen;
    u_char isinter;
    u_char isrgb24;
    u_char pad0, pad1;
} DISPENV;

/*---------------------------------------------------------------------------*/
/* Primitive setup macros                                                    */
/*---------------------------------------------------------------------------*/

#define setPolyF3(p)    setlen(p, 4),  setcode(p, 0x20)
#define setPolyF4(p)    setlen(p, 5),  setcode(p, 0x28)
#define setPolyFT3(p)   setlen(p, 7),  setcode(p, 0x24)
#define setPolyFT4(p)   setlen(p, 9),  setcode(p, 0x2c)
#define setPolyG3(p)    setlen(p, 6),  setcode(p, 0x30)
#define setPolyG4(p)    setlen(p, 8),  setcode(p, 0x38)
#define setPolyGT3(p)   setlen(p, 9),  setcode(p, 0x34)
#define setPolyGT4(p)   setlen(p, 12), setcode(p, 0x3c)
#define setLineF2(p)    setlen(p, 3),  setcode(p, 0x40)
#define setLineF3(p)    setlen(p, 5),  setcode(p, 0x48)
#define setLineF4(p)    setlen(p, 6),  setcode(p, 0x4c)
#define setLineG2(p)    setlen(p, 4),  setcode(p, 0x50)
#define setLineG3(p)    setlen(p, 7),  setcode(p, 0x58)
#define setLineG4(p)    setlen(p, 9),  setcode(p, 0x5c)
#define setSprt(p)      setlen(p, 4),  setcode(p, 0x64)
#define setSprt8(p)     setlen(p, 3),  setcode(p, 0x74)
#define setSprt16(p)    setlen(p, 3),  setcode(p, 0x7c)
#define setTile(p)      setlen(p, 3),  setcode(p, 0x60)
#define setTile1(p)     setlen(p, 2),  setcode(p, 0x68)
#define setTile8(p)     setlen(p, 2),  setcode(p, 0x70)
#define setTile16(p)    setlen(p, 2),  setcode(p, 0x78)

#define setRGB0(p,_r,_g,_b)  (p)->r0=(_r),(p)->g0=(_g),(p)->b0=(_b)
#define setRGB1(p,_r,_g,_b)  (p)->r1=(_r),(p)->g1=(_g),(p)->b1=(_b)
#define setRGB2(p,_r,_g,_b)  (p)->r2=(_r),(p)->g2=(_g),(p)->b2=(_b)
#define setRGB3(p,_r,_g,_b)  (p)->r3=(_r),(p)->g3=(_g),(p)->b3=(_b)

#define setXY0(p,_x,_y)      (p)->x0=(_x),(p)->y0=(_y)
/* setXY2 has two forms: (p,x,y) for single point, (p,x0,y0,x1,y1) for LINE */
#define setXY2_3(p,_x,_y)        (p)->x2=(_x),(p)->y2=(_y)
#define setXY2_5(p,_x0,_y0,_x1,_y1)  (p)->x0=(_x0),(p)->y0=(_y0),(p)->x1=(_x1),(p)->y1=(_y1)
#define GET_XY2_MACRO(_1,_2,_3,_4,_5,NAME,...) NAME
#define setXY2(...) GET_XY2_MACRO(__VA_ARGS__, setXY2_5, setXY2_5, setXY2_3, setXY2_3)(__VA_ARGS__)
#define setXY3(p,_x0,_y0,_x1,_y1,_x2,_y2)      \
    (p)->x0=(_x0),(p)->y0=(_y0),                 \
    (p)->x1=(_x1),(p)->y1=(_y1),                 \
    (p)->x2=(_x2),(p)->y2=(_y2)
#define setXY4(p,_x0,_y0,_x1,_y1,_x2,_y2,_x3,_y3) \
    (p)->x0=(_x0),(p)->y0=(_y0),                 \
    (p)->x1=(_x1),(p)->y1=(_y1),                 \
    (p)->x2=(_x2),(p)->y2=(_y2),                 \
    (p)->x3=(_x3),(p)->y3=(_y3)

#define setUV0(p,_u,_v)      (p)->u0=(_u),(p)->v0=(_v)
#define setUV3(p,_u0,_v0,_u1,_v1,_u2,_v2)       \
    (p)->u0=(_u0),(p)->v0=(_v0),                  \
    (p)->u1=(_u1),(p)->v1=(_v1),                  \
    (p)->u2=(_u2),(p)->v2=(_v2)
#define setUV4(p,_u0,_v0,_u1,_v1,_u2,_v2,_u3,_v3) \
    (p)->u0=(_u0),(p)->v0=(_v0),                  \
    (p)->u1=(_u1),(p)->v1=(_v1),                  \
    (p)->u2=(_u2),(p)->v2=(_v2),                  \
    (p)->u3=(_u3),(p)->v3=(_v3)

#define setUVWH(p,_u,_v,_w,_h) \
    (p)->u0=(_u),       (p)->v0=(_v),             \
    (p)->u1=(_u)+(_w),  (p)->v1=(_v),             \
    (p)->u2=(_u),       (p)->v2=(_v)+(_h),        \
    (p)->u3=(_u)+(_w),  (p)->v3=(_v)+(_h)

#define setWH(p,_w,_h)   (p)->w=(_w),(p)->h=(_h)
#define setClut(p,_x,_y) (p)->clut = getClut(_x,_y)
#define setTPage(p,tp,abr,x,y) (p)->tpage = getTPage(tp,abr,x,y)

#define setSemiTrans(p, abe) \
    setcode(p, (getcode(p) & ~0x02) | ((abe) ? 0x02 : 0x00))
#define setShadeTex(p, tge) \
    setcode(p, (getcode(p) & ~0x01) | ((tge) ? 0x01 : 0x00))

#define setDrawTPage(p, dfe, dtd, tpage) \
    setlen(p, 1), \
    *((u_long *)((u_char *)(p) + 4)) = (u_long)(((tpage) & 0x9ff) | ((dtd) ? 0x200 : 0) | ((dfe) ? 0x400 : 0)) | 0xe1000000UL

/*---------------------------------------------------------------------------*/
/* TPAGE / CLUT helpers                                                      */
/*---------------------------------------------------------------------------*/

#define getTPage(tp, abr, x, y) \
    ((((tp)  & 0x3) << 7) | (((abr) & 0x3) << 5) | (((y) & 0x100) >> 4) | (((x) & 0x3ff) >> 6) | (((y) & 0x200) << 2))

#define getClut(x, y) \
    ((((y) & 0x1ff) << 6) | (((x) >> 4) & 0x3f))

/*---------------------------------------------------------------------------*/
/* OT helpers                                                                */
/*---------------------------------------------------------------------------*/

#define ClearOTag(ot, n)   do { int _i; for (_i = 0; _i < (int)(n); _i++) ((u_long *)(ot))[_i] = 0x00ffffff; } while(0)
/* PSX ClearOTagR creates a REVERSE chain: ot[n-1]→ot[n-2]→...→ot[0]→terminator.
   This is essential for DG_ClearChanlSystem which links env1→ot[n-1] and ot[0]→env2. */
#define ClearOTagR(ot, n)  do { int _i; for (_i = 0; _i < (int)(n); _i++) { \
    if (_i == 0) ((u_long *)(ot))[_i] = 0x00ffffff; \
    else ((u_long *)(ot))[_i] = _ptr_to_handle(&((u_long *)(ot))[_i-1]); \
} } while(0)

/*---------------------------------------------------------------------------*/
/* GPU functions (stubs)                                                     */
/*---------------------------------------------------------------------------*/

int  ResetGraph(int mode);
int  SetGraphDebug(int level);
int  SetDispMask(int mask);
void ClearImage(RECT *rect, u_char r, u_char g, u_char b);
void ClearImage2(RECT *rect, u_char r, u_char g, u_char b);
void DrawSync(int mode);
void DrawSyncCallback(void (*func)(void));
int  DrawOTag(u_long *ot);
int  DrawOTagEnv(u_long *ot, DRAWENV *env);

DRAWENV *PutDrawEnv(DRAWENV *env);
DISPENV *PutDispEnv(DISPENV *env);
DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
u_long  *SetDrawEnv(DR_ENV *dr_env, DRAWENV *env);

void LoadImage(RECT *rect, u_long *p);
void StoreImage(RECT *rect, u_long *p);
void MoveImage(RECT *rect, int x, int y);

u_short GetTPage(int tp, int abr, int x, int y);
u_short GetClut(int x, int y);
u_short LoadTPage(u_long *pix, int tp, int abr, int x, int y, int w, int h);

int  FntLoad(int tx, int ty);
int  FntOpen(int x, int y, int w, int h, int isbg, int n);
void FntPrint(int id, const char *fmt, ...);
void FntFlush(int id);

int  KanjiFntOpen(int x, int y, int w, int h, int dx, int dy, int cx, int cy);
void KanjiFntPrint(int id, const char *fmt, ...);
void KanjiFntFlush(int id);

typedef struct { u_long tag; u_long code[2]; } DR_AREA;
typedef struct { u_long tag; u_long code[2]; } DR_OFFSET;
typedef struct { u_long tag; u_long code[2]; } DR_MODE;
typedef struct { u_long tag; u_long code[2]; } DR_MOVE;
typedef struct { u_long tag; u_long code[2]; } DR_LOAD;
typedef struct { u_long tag; u_long code[5]; } DR_TWIN;
typedef struct { u_long tag; u_long code[2]; } DR_STP;

void SetDrawArea(DR_AREA *p, RECT *r);
void SetDrawOffset(DR_OFFSET *p, u_short *ofs);

u_long GetTimSize(u_char *sjis, u_long *x, u_long *y);

/*---------------------------------------------------------------------------*/
/* Additional macros used by game code                                       */
/*---------------------------------------------------------------------------*/

#define SetTile(p)       setTile(p)
#define SetSprt(p)       setSprt(p)
#define SetSprt16(p)     setSprt16(p)
#define SetSprt8(p)      setSprt8(p)
#define SetLineF2(p)     setLineF2(p)
#define SetLineG2(p)     setLineG2(p)
#define SetPolyF3(p)     setPolyF3(p)
#define SetPolyF4(p)     setPolyF4(p)
#define SetPolyFT3(p)    setPolyFT3(p)
#define SetPolyFT4(p)    setPolyFT4(p)
#define SetPolyG3(p)     setPolyG3(p)
#define SetPolyG4(p)     setPolyG4(p)
#define SetPolyGT3(p)    setPolyGT3(p)
#define SetPolyGT4(p)    setPolyGT4(p)
#define SetSemiTrans(p, abe) setSemiTrans(p, abe)
#define SetShadeTex(p, tge)  setShadeTex(p, tge)

#define SetDrawStp(p, stp) do { \
    setlen(p, 1); \
    *((u_long *)((u_char *)(p) + 4)) = 0xe6000000UL | ((stp) ? 1 : 0); \
} while(0)

#define SetDrawTPage(p, dfe, dtd, tpage)  setDrawTPage(p, dfe, dtd, tpage)
#define SetDrawMode(p, dfe, dtd, tpage, tw) setDrawTPage(p, dfe, dtd, tpage)

/* DrawPrim submits a single GPU primitive for immediate rendering */
extern void port_DrawPrim(void *p);
#define DrawPrim(p)      port_DrawPrim(p)

/* setVector (PSX SDK utility) */
#define setVector(v, _x, _y, _z) \
    (v)->vx = (_x), (v)->vy = (_y), (v)->vz = (_z)

/* setSVector */
#define setSVector(v, _x, _y, _z) \
    (v)->vx = (_x), (v)->vy = (_y), (v)->vz = (_z)

/*---------------------------------------------------------------------------*/
/* Movie / MDEC stream types                                                 */
/*---------------------------------------------------------------------------*/

typedef struct {
    u_short id;
    u_short type;
    u_short secCount;
    u_short nSectors;
    u_long  frameCount;
    u_long  demuxedSize;
    u_short width;
    u_short height;
    u_long  dummy1;
    u_long  dummy2;
} StHEADER;

typedef struct {
    u_long  *vlcbuf[2];
    u_long  *imgbuf;
    short    vlcid;
    short    imgid;
    short    isdone;
    short    pad;
    RECT     rect;
    RECT     slice;
    int      vlccount;
} DECENV;

/* Streaming callback types */
typedef void (*StCB)(void);
void StSetStream(u_long mode, u_long start, u_long end, void (*func)(), void (*func2)());
void StSetMask(u_long mask, u_long val);
u_long *StFindNext(u_long *type, u_long *num);
void StUnFindNext(void);
void StSetChannel(u_long channel);
void StClearRing(void);
int  StGetBackloc(void *loc);
int  StSetEmulate(u_long *addr, int mode, int area, void (*func)(), void (*func2)());

/*---------------------------------------------------------------------------*/
/* Missing SPU functions                                                     */
/*---------------------------------------------------------------------------*/

static inline void SpuSetIRQ(long mode) { (void)mode; }
static inline void SpuSetIRQCallback(void (*func)(void)) { (void)func; }

/* Additional GPU functions and macros used by game code */
extern void port_LoadImage(RECT *rect, u_long *p);
extern void port_StoreImage(RECT *rect, u_long *p);
static inline void LoadImage2(RECT *rect, u_long *p) { port_LoadImage(rect, p); }
static inline void StoreImage2(RECT *rect, u_long *p) { port_StoreImage(rect, p); }
static inline void SetDrawMove(DR_MOVE *p, RECT *rect, int x, int y) { (void)p; (void)rect; (void)x; (void)y; setlen(p, 5); }

/* setXYWH: SPRT/TILE have w/h fields; POLY_FT4 has x0..x3/y0..y3 corners.
 * Use C11 _Generic to pick the right expansion. */
static inline void setXYWH_sprt_(SPRT *p, int x, int y, int w, int h) {
    p->x0 = x; p->y0 = y; p->w = w; p->h = h;
}
static inline void setXYWH_tile_(TILE *p, int x, int y, int w, int h) {
    p->x0 = x; p->y0 = y; p->w = w; p->h = h;
}
static inline void setXYWH_polyft4_(POLY_FT4 *p, int x, int y, int w, int h) {
    p->x0 = x;     p->y0 = y;
    p->x1 = x + w; p->y1 = y;
    p->x2 = x;     p->y2 = y + h;
    p->x3 = x + w; p->y3 = y + h;
}
#define setXYWH(p, _x, _y, _w, _h) _Generic((p), \
    SPRT *:     setXYWH_sprt_, \
    TILE *:     setXYWH_tile_, \
    POLY_FT4 *: setXYWH_polyft4_, \
    default:    setXYWH_sprt_)((p), (_x), (_y), (_w), (_h))

/* Streaming */
static inline u_long *StGetNext(u_long *addr, u_long **header) { (void)addr; if (header) *header = NULL; return NULL; }
static inline void LoadExec(const char *name, u_long stack, u_long heap) { (void)name; (void)stack; (void)heap; }
static inline long PCopen(const char *name, long mode, ...) { (void)name; (void)mode; return -1; }
static inline long PCclose(long fd) { (void)fd; return 0; }
static inline long PCread(long fd, void *buf, long size) { (void)fd; (void)buf; (void)size; return 0; }
static inline long PCwrite(long fd, void *buf, long size) { (void)fd; (void)buf; (void)size; return 0; }
static inline void StFreeRing(u_long *addr) { (void)addr; }
static inline long _get_mode(long dfe, long dtd, long tpage) { (void)dfe; (void)dtd; return tpage; }

/* Additional GTE functions */
static inline void RotMatrix_gte(SVECTOR *r, MATRIX *m) { RotMatrix(r, m); }
static inline void applyVector(VECTOR *v0, SVECTOR *sv, int scale)
{
    v0->vx += ((long)sv->vx * scale) >> 12;
    v0->vy += ((long)sv->vy * scale) >> 12;
    v0->vz += ((long)sv->vz * scale) >> 12;
}
static inline void gte_ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1) { ApplyMatrixSV(m, v0, v1); }

/*---------------------------------------------------------------------------*/
/* Missing kernel/BIOS functions used by game code                           */
/*---------------------------------------------------------------------------*/

static inline void SetConf(int ev, int tcb, int sp) { (void)ev; (void)tcb; (void)sp; }
static inline void _96_remove(void) {}
static inline void _96_init(void) {}
static inline long SetSp(long sp) { (void)sp; return 0; }

#endif /* __PSX_LIBGPU_H__ */
