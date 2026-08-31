/* Static size/layout assertions for PSX-derived structs on the 64-bit port.
 *
 * The port mixes 64-bit pointers into PSX-shaped structs. When a struct
 * grows unexpectedly (e.g. a field gets retyped, a pointer sneaks in, or
 * the compiler adds alignment padding we didn't account for) the failure
 * mode is *silent*: code reads adjacent memory as if it were a struct
 * field, the imgui debug pane displays garbage (-7424 anyone?), or the
 * stage-init loader writes past the end of an array.
 *
 * Every assertion here corresponds to a struct that the port's C code
 * indexes or memcpys at the known size. If you change a struct layout
 * and one of these breaks, the fix is one of:
 *
 *   (a) update the constant here, *and* audit every imgui mirror,
 *       loader, and scratchpad-offset that depended on the old size;
 *   (b) reshape the struct to preserve the old size (e.g. via padding);
 *   (c) decide the struct shouldn't be size-pinned and remove the
 *       assertion (rare; document why).
 *
 * The file compiles to nothing at runtime -- the assertions live entirely
 * in _Static_assert directives plus a single unused sentinel symbol.
 */

#include <stddef.h>
#include <sys/types.h>
#include "libgte.h"
#include "libgpu.h"
#include "libdg/libdg.h"
#include "game/game.h"
#include "libhzd/libhzd.h"
#include "fmt_hzd.h"

/* C11 _Static_assert; same spelling as static_assert without <assert.h>. */
#define ASSERT_SIZE(type, expected)  \
    _Static_assert(sizeof(type) == (expected), \
        "sizeof(" #type ") changed -- audit imgui mirror + loaders before bumping")

#define ASSERT_OFFSET(type, field, expected) \
    _Static_assert(offsetof(type, field) == (expected), \
        "offsetof(" #type "." #field ") changed -- check raw scratchpad/byte-offset accesses")

/* --- libgte primitives ----------------------------------------------- */
/* Size-stable across PSX and port: the port pads MATRIX to 32 bytes to
   match PSX, and SVECTOR/CVECTOR/DVECTOR have no pointers. */
ASSERT_SIZE(SVECTOR,  8);
ASSERT_SIZE(CVECTOR,  4);
ASSERT_SIZE(DVECTOR,  4);
ASSERT_SIZE(VECTOR,  16);
ASSERT_SIZE(MATRIX,  32);

/* --- libgpu primitives ----------------------------------------------- */
/* POLY_GT4 packs are written to obj->packs[] by the shade pipeline, then
   read back by both port_RenderChanl and the OT walker. A size change
   would silently misalign per-face iteration. */
ASSERT_SIZE(POLY_F3,   20);
ASSERT_SIZE(POLY_F4,   24);
ASSERT_SIZE(POLY_G3,   28);
ASSERT_SIZE(POLY_G4,   36);
ASSERT_SIZE(POLY_FT3,  32);
ASSERT_SIZE(POLY_FT4,  40);
ASSERT_SIZE(POLY_GT3,  40);
ASSERT_SIZE(POLY_GT4,  52);

ASSERT_SIZE(TILE,    16);
ASSERT_SIZE(TILE_1,  12);
ASSERT_SIZE(TILE_8,  12);
ASSERT_SIZE(TILE_16, 12);
ASSERT_SIZE(SPRT,    20);
ASSERT_SIZE(SPRT_8,  16);
ASSERT_SIZE(SPRT_16, 16);
ASSERT_SIZE(LINE_F2, 16);
ASSERT_SIZE(LINE_F3, 24);
ASSERT_SIZE(LINE_F4, 28);
ASSERT_SIZE(LINE_G2, 20);
ASSERT_SIZE(LINE_G3, 32);
ASSERT_SIZE(LINE_G4, 40);
ASSERT_SIZE(DR_TPAGE, 8);
ASSERT_SIZE(DR_ENV,  64);

/* POLY_GT4 field offsets touched by the Stealth pack override + the GL
   submission in libdg_stub.c (port_RenderChanl reads tpage, clut, u/v at
   these exact bytes). */
ASSERT_OFFSET(POLY_GT4, tag,   0);
ASSERT_OFFSET(POLY_GT4, r0,    4);
ASSERT_OFFSET(POLY_GT4, code,  7);
ASSERT_OFFSET(POLY_GT4, u0,   12);
ASSERT_OFFSET(POLY_GT4, clut, 14);
ASSERT_OFFSET(POLY_GT4, tpage, 26);
ASSERT_OFFSET(POLY_GT4, u3,   48);

/* --- libdg --------------------------------------------------------- */
/* DG_CHANL: hit by the imgui debug pane mirror (PortDG_CHANL_Partial in
   port/imgui_debug.cpp). The mirror was 96 bytes when the real struct
   was 504 -- DG_Chanls[1] read garbage for months until we caught it. */
ASSERT_SIZE(DG_CHANL, 504);
ASSERT_OFFSET(DG_CHANL, eye_inv,        24);
ASSERT_OFFSET(DG_CHANL, eye,            56);
ASSERT_OFFSET(DG_CHANL, screen,  88);
ASSERT_OFFSET(DG_CHANL, objs_index,     94);
ASSERT_OFFSET(DG_CHANL, queue,          96);

/* DG_OBJ: per-obj packs[2] live at offset 0x60 on the port (0x54 on PSX
   due to 32-bit DG_MDL / CVECTOR / _DG_OBJ pointers). The OT-walker +
   libdg_stub iterate obj++ at this stride, so size drift = silent
   corruption. */
ASSERT_OFFSET(DG_OBJ, packs,  96);
ASSERT_SIZE(DG_OBJ,         112);

/* DG_TEX: read by every textured prim in libdg_stub.c. */
ASSERT_SIZE(DG_TEX, 12);

/* --- game/* ---------------------------------------------------------- */
/* MAP: gMapRecs_800B7910[16] is in BSS and indexed by GM_AddMap with
   sizeof(MAP) stride. */
ASSERT_SIZE(MAP,    32);
ASSERT_OFFSET(MAP, hzd,   8);
ASSERT_OFFSET(MAP, lit,  16);
ASSERT_OFFSET(MAP, zone, 24);

/* TARGET: gTargets_800B64E0[64] in BSS, indexed by sizeof(TARGET) stride.
   GM_PushTarget and GM_TargetIntersects walk this array. */
ASSERT_SIZE(TARGET, 88);
ASSERT_OFFSET(TARGET, class,     0);
ASSERT_OFFSET(TARGET, damaged,   6);
ASSERT_OFFSET(TARGET, center,    8);
ASSERT_OFFSET(TARGET, size,     16);

/* HITTABLE: GM_ClayDatas[8] in BSS -- iterated by jirai.c. */
ASSERT_SIZE(HITTABLE, 32);

/* --- fmt_hzd / libhzd ------------------------------------------------ */
/* HZD_GRP: the port's hzd_loader.c converts from PSX-format raw (24 bytes)
   into this in-memory struct. Size on port grows because of pointer
   fields. Both the raw format and this port struct must stay in sync
   with their respective callers. */
ASSERT_SIZE(HZD_GRP, 40);
ASSERT_SIZE(HZD_SEG, 16);
ASSERT_SIZE(HZD_VEC,  8);
ASSERT_SIZE(HZD_FLR, 48);

/* HZD_HDL: allocated by HZD_MakeHandler with a size formula that depends
   on dynamic_floors/segments counts. Base struct size matters. */
ASSERT_SIZE(HZD_HDL, 72);

/* Sentinel symbol so this TU contributes object code (otherwise some
   linkers drop the .o silently if it has no externs). */
const int port_struct_assertions_present = 1;
