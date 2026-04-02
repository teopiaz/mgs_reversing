/**
 * Stub implementations for PSX GPU, CD, SPU, PAD, and kernel functions.
 * These are no-ops that satisfy linker requirements.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "libgte.h"
#include "libgpu.h"
#include "libcd.h"
#include "libetc.h"
#include "libspu.h"
#include "libpad.h"
#include "libapi.h"
#include "libpress.h"

/*---------------------------------------------------------------------------*/
/* OT handle table for 64-bit pointer support                                */
/*---------------------------------------------------------------------------*/

void *port_ot_table[PORT_OT_TABLE_SIZE];
int port_ot_next = 0;

/*---------------------------------------------------------------------------*/
/* GPU functions                                                             */
/*---------------------------------------------------------------------------*/

/* VRAM functions — implemented in port/libdg/vram.c */
extern void port_ClearImage(RECT *rect, u_char r, u_char g, u_char b);
extern void port_LoadImage(RECT *rect, u_long *p);
extern void port_StoreImage(RECT *rect, u_long *p);
extern void port_MoveImage(RECT *rect, int dx, int dy);
extern DRAWENV *port_SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
extern DISPENV *port_SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
extern void port_DrawOTag(u_long *ot);

int  ResetGraph(int mode) { (void)mode; return 0; }
int  SetGraphDebug(int level) { (void)level; return 0; }
int  SetDispMask(int mask) { (void)mask; return 0; }
static int clearimage_calls = 0;
void ClearImage(RECT *rect, u_char r, u_char g, u_char b) {
    port_ClearImage(rect, r, g, b);
}
void ClearImage2(RECT *rect, u_char r, u_char g, u_char b) { port_ClearImage(rect, r, g, b); }
void DrawSync(int mode) { (void)mode; }
void DrawSyncCallback(void (*func)(void)) { (void)func; }
static int drawotag_calls = 0;
int  DrawOTag(u_long *ot) {
    drawotag_calls++;
    if (0) { /* debug disabled */
        int prim_count = 0;
        if (ot) {
            u_long *p = ot;
            for (int i = 0; i < 300 && p && !isendprim(p); i++) {
                if (getlen(p) > 0) prim_count++;
                p = nextPrim(p);
                if (!p) break;
            }
        }
        printf("[gpu] DrawOTag #%d: ot=%p, prims_in_ot=%d\n", drawotag_calls, ot, prim_count);
        fflush(stdout);
    }
    port_DrawOTag(ot);
    return 0;
}
int  DrawOTagEnv(u_long *ot, DRAWENV *env) { (void)env; port_DrawOTag(ot); return 0; }

static int putdraw_calls = 0;
DRAWENV *PutDrawEnv(DRAWENV *env) {
    if (env && env->isbg) {
        port_ClearImage(&env->clip, env->r0, env->g0, env->b0);
    }
    return env;
}
static int putdisp_calls = 0;
DISPENV *PutDispEnv(DISPENV *env) {
    putdisp_calls++;
    if (env && putdisp_calls <= 5) {
        printf("[gpu] PutDispEnv #%d: disp=(%d,%d,%d,%d)\n",
               putdisp_calls, env->disp.x, env->disp.y, env->disp.w, env->disp.h);
        fflush(stdout);
    }
    if (env) {
        port_SetDefDispEnv(env, env->disp.x, env->disp.y, env->disp.w, env->disp.h);
    }
    return env;
}

static int setdefdraw_calls = 0;
DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h)
{
    setdefdraw_calls++;
    if (setdefdraw_calls <= 5) {
        printf("[gpu] SetDefDrawEnv #%d: (%d,%d,%d,%d)\n", setdefdraw_calls, x, y, w, h);
        fflush(stdout);
    }
    return port_SetDefDrawEnv(env, x, y, w, h);
}

static int setdefdisp_calls = 0;
DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
    setdefdisp_calls++;
    if (setdefdisp_calls <= 5) {
        printf("[gpu] SetDefDispEnv #%d: (%d,%d,%d,%d)\n", setdefdisp_calls, x, y, w, h);
        fflush(stdout);
    }
    return port_SetDefDispEnv(env, x, y, w, h);
}

u_long *SetDrawEnv(DR_ENV *dr_env, DRAWENV *env) {
    /* Pack GPU commands into DR_ENV so the OT walker can process them.
       The PSX GPU uses E3/E4/E5 commands for draw area and offset. */
    int i = 0;
    int x1 = env->clip.x, y1 = env->clip.y;
    int x2 = x1 + env->clip.w - 1, y2 = y1 + env->clip.h - 1;

    /* E3: Set Drawing Area top-left */
    dr_env->code[i++] = 0xE3000000 | (x1 & 0x3FF) | ((y1 & 0x1FF) << 10);
    /* E4: Set Drawing Area bottom-right */
    dr_env->code[i++] = 0xE4000000 | (x2 & 0x3FF) | ((y2 & 0x1FF) << 10);
    /* E5: Set Drawing Offset */
    dr_env->code[i++] = 0xE5000000 | (env->ofs[0] & 0x7FF) | ((env->ofs[1] & 0x7FF) << 11);
    /* E1: Draw Mode / TPage */
    dr_env->code[i++] = 0xE1000000 | env->tpage;

    /* Set the tag: length = number of words, linked list terminator */
    setlen(dr_env, i);
    termPrim(dr_env);

    return (u_long *)dr_env;
}

void LoadImage(RECT *rect, u_long *p) { port_LoadImage(rect, p); }
void StoreImage(RECT *rect, u_long *p) { port_StoreImage(rect, p); }
void MoveImage(RECT *rect, int x, int y) { port_MoveImage(rect, x, y); }

u_short GetTPage(int tp, int abr, int x, int y) { return getTPage(tp, abr, x, y); }
u_short GetClut(int x, int y) { return getClut(x, y); }
u_short LoadTPage(u_long *pix, int tp, int abr, int x, int y, int w, int h)
{ (void)pix; (void)w; (void)h; return getTPage(tp, abr, x, y); }

int  FntLoad(int tx, int ty) { (void)tx; (void)ty; return 0; }
int  FntOpen(int x, int y, int w, int h, int isbg, int n) { (void)x; (void)y; (void)w; (void)h; (void)isbg; (void)n; return 0; }
void FntPrint(int id, const char *fmt, ...) { (void)id; (void)fmt; }
void FntFlush(int id) { (void)id; }

int  KanjiFntOpen(int x, int y, int w, int h, int dx, int dy, int cx, int cy) { (void)x; (void)y; (void)w; (void)h; (void)dx; (void)dy; (void)cx; (void)cy; return 0; }
void KanjiFntPrint(int id, const char *fmt, ...) { (void)id; (void)fmt; }
void KanjiFntFlush(int id) { (void)id; }

void SetDrawArea(DR_AREA *p, RECT *r) { (void)p; (void)r; }
void SetDrawOffset(DR_OFFSET *p, u_short *ofs) { (void)p; (void)ofs; }

u_long GetTimSize(u_char *sjis, u_long *x, u_long *y) { (void)sjis; (void)x; (void)y; return 0; }

/*---------------------------------------------------------------------------*/
/* CD functions                                                              */
/*---------------------------------------------------------------------------*/

int    CdInit(void) { return 1; }
int    CdControl(u_char com, u_char *param, u_char *result) { (void)com; (void)param; (void)result; return 1; }
int    CdControlB(u_char com, u_char *param, u_char *result) { (void)com; (void)param; (void)result; return 1; }
int    CdControlF(u_char com, u_char *param) { (void)com; (void)param; return 1; }
int    CdRead(int sectors, u_long *buf, int mode) { (void)sectors; (void)buf; (void)mode; return 0; }
int    CdRead2(long mode) { (void)mode; return 0; }
int    CdReady(int mode, u_char *result) { (void)mode; (void)result; return 1; }
int    CdSync(int mode, u_char *result) { (void)mode; (void)result; return 0; }
CdlCB CdReadyCallback(CdlCB func) { (void)func; return (CdlCB)0; }
CdlCB CdSyncCallback(CdlCB func) { (void)func; return (CdlCB)0; }
int    CdFlush(void) { return 0; }
int    CdDiskReady(int mode) { (void)mode; return 2; }
void  *CdGetSector(void *madr, int size) { (void)size; return madr; }
int    CdGetToc(CdlLOC *loc) { (void)loc; return 0; }
CdlLOC *CdIntToPos(int i, CdlLOC *p) { (void)i; memset(p, 0, sizeof(*p)); return p; }
int    CdPosToInt(CdlLOC *p) { (void)p; return 0; }
CdlLOC *CdLastPos(void) { static CdlLOC loc; return &loc; }
u_char *CdLastCom(void) { static u_char com[8]; return com; }

/*---------------------------------------------------------------------------*/
/* VSync / Callback                                                          */
/*---------------------------------------------------------------------------*/

int  VSync(int mode) { (void)mode; return 0; }
void ResetCallback(void) {}
void StopCallback(void) {}
void RestartCallback(void) {}
int  CheckCallback(void) { return 0; }
void VSyncCallback(void (*func)(void)) { (void)func; }
long PadInit(int mode) { (void)mode; return 0; }

/* SPU functions — implemented in port/sound/spu_emu.c */

/*---------------------------------------------------------------------------*/
/* PAD functions                                                             */
/*---------------------------------------------------------------------------*/

void PadInitDirect(u_char *pad1, u_char *pad2) { (void)pad1; (void)pad2; }
void PadStartCom(void) {}
void PadStopCom(void) {}
int  PadSetAct(int port, u_char *table, int len) { (void)port; (void)table; (void)len; return 0; }
int  PadSetActAlign(int port, u_char *table) { (void)port; (void)table; return 0; }
int  PadGetState(int port) { (void)port; return 6; /* PadStateStable */ }
int  PadInfoMode(int port, int term, int offs) { (void)port; (void)term; (void)offs; return 0; }
int  PadInfoAct(int port, int act, int term) { (void)port; (void)act; (void)term; return 0; }
int  PadInfoComb(int port, int listno, int offs) { (void)port; (void)listno; (void)offs; return 0; }

/*---------------------------------------------------------------------------*/
/* Kernel / API functions                                                    */
/*---------------------------------------------------------------------------*/

long OpenEvent(u_long class_id, long spec, long mode, long (*func)()) { (void)class_id; (void)spec; (void)mode; (void)func; return 0; }
long CloseEvent(long event) { (void)event; return 0; }
long EnableEvent(long event) { (void)event; return 0; }
long DisableEvent(long event) { (void)event; return 0; }
long TestEvent(long event) { (void)event; return 0; }
int  DeliverEvent(u_long class_id, long spec) { (void)class_id; (void)spec; return 0; }
int  UnDeliverEvent(u_long class_id, long spec) { (void)class_id; (void)spec; return 0; }

long OpenTh(void (*func)(), u_long sp, u_long gp) { (void)func; (void)sp; (void)gp; return 0; }
int  CloseTh(long thread) { (void)thread; return 0; }
int  ChangeTh(long thread) { (void)thread; return 0; }

int  ChangeClearRCnt(int mode) { (void)mode; return 0; }
long SetRCnt(u_long spec, u_short target, long mode) { (void)spec; (void)target; (void)mode; return 0; }
long GetRCnt(u_long spec) { (void)spec; return 0; }
long StartRCnt(u_long spec) { (void)spec; return 0; }
long StopRCnt(u_long spec) { (void)spec; return 0; }
long ResetRCnt(u_long spec) { (void)spec; return 0; }

void ReturnFromException(void) {}
void FlushCache(void) {}

long EnterCriticalSection(void) { return 0; }
long ExitCriticalSection(void) { return 0; }
long SwEnterCriticalSection(void) { return 0; }
long SwExitCriticalSection(void) { return 0; }

/*---------------------------------------------------------------------------*/
/* MDEC / Press functions                                                    */
/*---------------------------------------------------------------------------*/

void DecDCTReset(int mode) { (void)mode; }
int  DecDCTBufSize(u_long *bs) { (void)bs; return 0; }
void DecDCTin(u_long *mdec_bs, int mode) { (void)mdec_bs; (void)mode; }
void DecDCTout(u_long *buf, int size) { (void)buf; (void)size; }
int  DecDCToutSync(int mode) { (void)mode; return 0; }
void DecDCToutCallback(void (*func)(void)) { (void)func; }
void DecDCTvlc(u_long *bs, u_long *buf) { (void)bs; (void)buf; }
void DecDCTvlc2(u_long *bs, u_long *buf, int q) { (void)bs; (void)buf; (void)q; }
void DecDCTvlcBuild(u_long *addr) { (void)addr; }

/*---------------------------------------------------------------------------*/
/* Streaming functions                                                       */
/*---------------------------------------------------------------------------*/

u_long *BreakDraw(void) { return NULL; }
void StSetRing(u_long *ring, u_long size) { (void)ring; (void)size; }
void StUnSetRing(void) {}
void StSetStream(u_long mode, u_long start, u_long end, void (*func)(), void (*func2)()) { (void)mode; (void)start; (void)end; (void)func; (void)func2; }
void StSetMask(u_long mask, u_long val) { (void)mask; (void)val; }
u_long *StFindNext(u_long *type, u_long *num) { (void)type; (void)num; return NULL; }
void StUnFindNext(void) {}
void StSetChannel(u_long channel) { (void)channel; }
void StClearRing(void) {}
int  StGetBackloc(void *loc) { (void)loc; return 0; }
int  StSetEmulate(u_long *addr, int mode, int area, void (*func)(), void (*func2)()) { (void)addr; (void)mode; (void)area; (void)func; (void)func2; return 0; }
