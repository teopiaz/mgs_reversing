/* Stubs for game source files that can't compile yet due to:
 * - Inline MIPS assembly (envmap3.c, sub_efct.c, radar.c, searchli.c)
 * - Lvalue casts (radioanim.c, thing.c)
 * - Other PSX-specific constructs
 * These will be properly ported later.
 */
#include <string.h>
#include "libgte.h"

/* enemy/action.c — enemy action routines */
/* (most functions are static, linked via function pointers) */

/* game/movie.c — FMV playback (PSX MDEC) */
/* movie functions are called but can be no-ops until video is implemented */

/* menu/radar.c — radar display (uses MIPS asm for fast rendering) */

/* menu/life.c — life gauge display */

/* The actual missing symbols will be resolved at link time.
 * If we get undefined symbol errors, we add specific stubs here.
 */
