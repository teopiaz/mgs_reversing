#ifndef PORT_GCL_OVERLAY_H
#define PORT_GCL_OVERLAY_H

/* Runtime GCL script override.

   When a stage loads its scenerio.gcx / demo.gcx, the overlay system
   checks for a compiled replacement on disk and, if found, feeds that
   to GCL_LoadScript instead of the shipped bytecode. Lets you iterate
   on GCL behavior by editing .gcl source + running tools/gcl2gcx.py,
   without a port rebuild.

   Default search root: ./overlays/  (relative to the port's CWD)
   Override with env var: MGS_GCL_OVERLAY_DIR=/path/to/dir

   File layout expected:
       <root>/<stage>/scenerio.gcx
       <root>/<stage>/demo.gcx

   <stage> is the string set in GM_StageName (e.g. "s02a", "d00a").

   Font-trailer handling: the .gcx format normally has a trailing font
   blob past the script body that the engine exposes as font slot 2.
   gcx2gcl strips it during decompile (--trailing <file>) and splices
   it back during compile. If your overlay .gcx was built WITHOUT the
   trailing blob, port_gcl_overlay_load re-points font #2 at the
   ORIGINAL cached buffer's trailing bytes, which stay resident for
   the stage. That means font-using scripts (codec, menus) keep
   working even when the overlay author doesn't care about fonts. */

/* Returns a pointer to the overlay's bytecode buffer (owned by this
   module, freed on next call). NULL when no overlay exists — caller
   should fall back to the original top. original_top is used only to
   read the trailing-blob pointer; the buffer itself is not mutated. */
unsigned char *port_gcl_overlay_load(const char *stage_name,
                                     int script_id,
                                     unsigned char *original_top);

/* Repoint font slot 2 at the original's trailing blob. Call this
   AFTER GCL_LoadScript(overlay) so the overlay's (empty) trailing
   doesn't leak into the font system. No-op if original_top is NULL. */
void port_gcl_overlay_patch_font(unsigned char *original_top);

#endif
