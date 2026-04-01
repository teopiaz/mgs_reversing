/**
 * Force-included before source/libdg/*.c to fix the enum/struct DG_CHANL conflict.
 * The original libdg.h has both:
 *   typedef struct DG_CHANL { ... } DG_CHANL;  (line 263)
 *   enum DG_CHANL { ... };                      (line 327)
 * C doesn't allow struct and enum with the same tag name.
 * We pre-define the enum values as macros so the enum definition becomes harmless.
 */
#ifndef __PORT_LIBDG_FIX_H__
#define __PORT_LIBDG_FIX_H__

/* Pre-define as macros — the enum line becomes: enum DG_CHANL_UNIT { ... }; */
/* But we can't rename it via macro since enum is a keyword.
   Instead, we'll use a different approach: make the enum values exist before
   the enum is parsed, then the compiler will see redefinitions (which are OK
   if they match). Actually that doesn't work either.

   Real fix: override the source header entirely. We use -iquote to prioritize
   our port/libdg/ directory for quoted includes from source/libdg/. */

#endif
