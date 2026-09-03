/* Tier 1 + 2 regression tests for the port.
 *
 * Links against every port object except main.o (see the `tests` target in
 * the Makefile) so real implementations are exercised, not reimplementations.
 * Nothing here initialises SDL, GL or the disc -- only leaf functions and
 * shims that are safe to call cold.
 *
 * Design and justification for each case: port/doc/10-regression-tests.md
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* --- symbols main.o would have provided ------------------------------- */
bool         g_running = false;
bool         imgui_request_screenshot = false;
const char  *port_argv0 = "test";

/* --- tiny harness ------------------------------------------------------ */
static int g_pass, g_fail, g_xfail;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (cond) { g_pass++; }                                            \
        else { g_fail++;                                                   \
               printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
               printf(__VA_ARGS__); printf("\n"); }                        \
    } while (0)

/* Known-failing: tracks an open bug without turning the suite red. */
#define XCHECK(cond, ...)                                                  \
    do {                                                                   \
        if (cond) { printf("  XPASS (now fixed, promote to CHECK): ");     \
                    printf(__VA_ARGS__); printf("\n"); g_fail++; }         \
        else      { g_xfail++;                                             \
                    printf("  xfail (known bug): ");                       \
                    printf(__VA_ARGS__); printf("\n"); }                   \
    } while (0)

static void section(const char *s) { printf("\n== %s\n", s); }

/* ====================================================================== */
/* Tier 1 — pure leaf functions                                           */
/* ====================================================================== */

extern int   port_init_memory(void);
extern int   port_ptr_to_int(const void *p);
extern void *port_int_to_ptr(int offset);
extern uintptr_t port_mem_base;

static void t1_pointer_roundtrip(void)
{
    section("T1 pointer <-> int round-trip (port_memory.c)");

    CHECK(port_ptr_to_int(NULL) == 0, "NULL must map to 0");
    CHECK(port_int_to_ptr(0) == NULL, "0 must map back to NULL");

    /* Pool pointers must survive the 32-bit narrowing. This is the scheme the
       GCL bind/delay tables rely on; a break here silently corrupts them. */
    void *p = (void *)(port_mem_base + 0x1234);
    CHECK(port_int_to_ptr(port_ptr_to_int(p)) == p,
          "pool pointer must round-trip (%p)", p);

    void *q = (void *)(port_mem_base + 0x0BADF00D);
    CHECK(port_int_to_ptr(port_ptr_to_int(q)) == q,
          "high pool offset must round-trip");

    /* delay.c distinguishes a GCL proc id from a block by SIGN: negative means
       "negated pool offset". The pool sits at a positive address, so a raw
       pointer handed over unnegated is read as a proc id and the block never
       runs -- exactly the HZD_ExecBind bug. Assert the convention holds. */
    int off = port_ptr_to_int(p);
    CHECK(off > 0, "pool offset must be positive so the sign carries meaning");
    CHECK(port_int_to_ptr((int)(-(intptr_t)(-off))) == p,
          "negated-offset convention must invert cleanly");
}

extern int GV_StrCode(const char *string);

static void t1_strcode(void)
{
    section("T1 GV_StrCode hashing (libgv/strcode.c)");
    /* Oracle: the CMD_* constants in game_script_fix.c, each documented in the
       source as GV_StrCode("<name>"). NOTE: port/strcode_lookup.c is NOT an
       oracle for this -- it maps GCL string ids, which the GCL compiler emits
       with a different hash (GV_StrCode("cape") is 0x1298, not 0xb99f). */
    CHECK(GV_StrCode("mesg")   == 0x22ff, "mesg   -> 0x22ff, got 0x%X", GV_StrCode("mesg"));
    CHECK(GV_StrCode("trap")   == 0xd4cb, "trap   -> 0xd4cb, got 0x%X", GV_StrCode("trap"));
    CHECK(GV_StrCode("chara")  == 0x9906, "chara  -> 0x9906, got 0x%X", GV_StrCode("chara"));
    CHECK(GV_StrCode("sound")  == 0x698d, "sound  -> 0x698d, got 0x%X", GV_StrCode("sound"));
    CHECK(GV_StrCode("jimaku") == 0xec9d, "jimaku -> 0xec9d, got 0x%X", GV_StrCode("jimaku"));
    CHECK(GV_StrCode("")       == 0,      "empty string -> 0");
    CHECK(GV_StrCode("mesg")   != GV_StrCode("trap"), "distinct names must differ");
}

/* GCL variable encoding. Mis-decoding this is a documented time sink:
   0x11800002 is difficulty (linkvarbuf, byte offset 2), while $w:00000A has
   no 0x800000 bit and so addresses the non-linkvar bank at byte 10. */
#define GCL_GetVarTypeCode(v) (((v << 1) >> 25) & 0xF)
#define GCL_GetVarOffset(v)   ((v) & 0xFFFF)
#define GCL_IsGameStateVar(v) (((v) & 0xF00000) == 0x800000)

static void t1_gcl_var_decode(void)
{
    section("T1 GCL variable decoding (libgcl_fix/libgcl.h)");
    CHECK(GCL_IsGameStateVar(0x11800002), "0x11800002 must select linkvarbuf");
    CHECK(GCL_GetVarOffset(0x11800002) == 2, "offset must be 2 (bytes, not index)");
    CHECK(!GCL_IsGameStateVar(0x1100000A), "$w:00000A must select the var bank");
    CHECK(GCL_GetVarOffset(0x1100000A) == 0x0A, "offset must be 0x0A");
    CHECK(GCL_GetVarOffset(0x1100000A) / 2 == 5, "byte 0x0A is short index 5");
}

/* ====================================================================== */
/* Tier 2 — port shim parity with upstream semantics                      */
/* ====================================================================== */

extern int  FS_StreamInit(void *heap, int size);
extern void FS_StreamOpen(void);
extern void FS_StreamClose(void);
extern int  FS_StreamIsEnd(void);
extern void FS_StreamStop(void);
extern int  FS_StreamIsForceStop(void);

static void t2_stream_refcount(void)
{
    section("T2 FS_StreamIsEnd is a reference count (upstream stream.c:369)");

    static char heap[64];
    FS_StreamInit(heap, sizeof heap);

    CHECK(FS_StreamIsEnd(), "no holders -> ended");
    FS_StreamOpen();
    CHECK(!FS_StreamIsEnd(), "one holder -> not ended");
    FS_StreamOpen();
    FS_StreamClose();
    CHECK(!FS_StreamIsEnd(), "nested holders must not end early");
    FS_StreamClose();
    CHECK(FS_StreamIsEnd(), "last close -> ended");

    /* An unbalanced close must not drive the count negative: `== 0` would then
       never be true again and strctrl would hang forever. */
    FS_StreamClose();
    CHECK(FS_StreamIsEnd(), "count must clamp at 0, never go negative");

    FS_StreamInit(heap, sizeof heap);
    CHECK(FS_StreamIsEnd(), "Init must reset the count");
}

static void t2_stream_forcestop(void)
{
    section("T2 FS_StreamIsForceStop reflects stop state");
    /* Returned a constant 0. sd_str.c case 5 leaves playback only via this
       flag, so str_status stuck at 5 and every stream deadlocked strctrl. */
    static char heap[64];
    FS_StreamInit(heap, sizeof heap);
    FS_StreamStop();
    CHECK(FS_StreamIsForceStop(), "after FS_StreamStop it must report stopped");
}

extern int CdControl(unsigned char com, unsigned char *param, unsigned char *result);
extern int CdControlB(unsigned char com, unsigned char *param, unsigned char *result);
extern int CdReady(int mode, unsigned char *result);
extern int CdSync(int mode, unsigned char *result);

static void t2_cd_status_buffers(void)
{
    section("T2 CD shims must write result[0]");
    /* They left the caller's buffer untouched. Safety_800C45F8 reads
       shell-open/error bits from an uninitialised stack buffer and spins
       forever printing TRY/OPEN whenever the garbage has 0x10 or 0x01 set. */
    unsigned char r[8];

    memset(r, 0xFF, sizeof r); CdControl(0, NULL, r);
    CHECK((r[0] & 0x11) == 0, "CdControl  must clear shell-open/error, got 0x%02X", r[0]);
    memset(r, 0xFF, sizeof r); CdControlB(0, NULL, r);
    CHECK((r[0] & 0x11) == 0, "CdControlB must clear shell-open/error, got 0x%02X", r[0]);
    memset(r, 0xFF, sizeof r); CdReady(0, r);
    CHECK((r[0] & 0x11) == 0, "CdReady    must clear shell-open/error, got 0x%02X", r[0]);
    memset(r, 0xFF, sizeof r); CdSync(0, r);
    CHECK((r[0] & 0x11) == 0, "CdSync     must clear shell-open/error, got 0x%02X", r[0]);

    /* NULL must stay safe -- ending2.c calls CdControlB(CdlPause, NULL, NULL). */
    CdControlB(0, NULL, NULL);
    CHECK(1, "NULL result must not crash");
}

/* GV dynamic memory: GV_AllocMemory2 registers the OWNER'S pointer in
   MEM_TAG.state, and GV_ResetDynamicMemorySystem (the defragmenter, run when
   a dynamic heap sets MEM_SYS_FLAG_FAILED) writes the moved block's new
   address back through it. With a 32-bit state the 64-bit owner pointer is
   truncated and the defrag write lands on a bogus address. Callers today:
   the ending-movie buffers (takabe/ending2.c) and game/movie.c. */
extern void  GV_InitMemorySystem(int which, int dynamic, void *memory, int size);
extern void *GV_AllocMemory(int which, int size);
extern void *GV_AllocMemory2(int which, int size, void **pstart);
extern void  GV_FreeMemory(int which, void *addr);
extern void  GV_ClearMemorySystem(int which);

static void t2_gv_defrag_owner_pointer(void)
{
    section("T2 GV defrag rewrites the registered owner pointer");

    static char heap_mem[1024] __attribute__((aligned(16)));
    enum { HEAP = 0 };   /* mgs_tests never runs game_init; heap 0 is ours */

    GV_InitMemorySystem(HEAP, 1 /* dynamic */, heap_mem, sizeof heap_mem);

    void *owner = NULL;
    void *a = GV_AllocMemory(HEAP, 128);
    void *b = GV_AllocMemory2(HEAP, 128, &owner);
    CHECK(a && b, "allocations must succeed");
    CHECK(owner == b, "AllocMemory2 must write the block into *pstart");

    memset(b, 0xAB, 128);

    GV_FreeMemory(HEAP, a);                    /* hole ahead of b */
    void *big = GV_AllocMemory(HEAP, 4096);    /* can't fit -> FAILED flag */
    CHECK(big == NULL, "oversized alloc must fail and mark the heap");

    GV_ClearMemorySystem(HEAP);                /* triggers the defrag */

    CHECK(owner == (void *)heap_mem,
          "defrag must move b to the heap start and rewrite the owner "
          "pointer (got %p, heap %p)", owner, (void *)heap_mem);
    CHECK(((unsigned char *)owner)[0] == 0xAB &&
          ((unsigned char *)owner)[127] == 0xAB,
          "moved block must carry its contents");
}

extern int DG_LoadInitLit(void *buf, int id);

static void t2_loader_return_codes(void)
{
    section("T2 DG_LoadInit* return codes");
    /* GV_LoadInit (libgv/cache.c) treats <= 0 as failure and drops the
       resource. Upstream DG_LoadInitLit is literally `return 1;`. */
    static char buf[64];
    CHECK(DG_LoadInitLit(buf, 0) == 1,
          "DG_LoadInitLit must return 1 like upstream (GV_LoadInit drops <= 0)");
}

/* ====================================================================== */

int main(void)
{
    if (port_init_memory() != 0) {
        printf("port_init_memory failed -- cannot run pointer tests\n");
        return 2;
    }

    t1_pointer_roundtrip();
    t1_strcode();
    t1_gcl_var_decode();
    t2_stream_refcount();
    t2_stream_forcestop();
    t2_cd_status_buffers();
    t2_gv_defrag_owner_pointer();
    t2_loader_return_codes();

    printf("\n%d passed, %d failed, %d known-failing\n", g_pass, g_fail, g_xfail);
    return g_fail ? 1 : 0;
}
