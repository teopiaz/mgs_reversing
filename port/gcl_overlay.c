#include "gcl_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libgcl/libgcl.h"   /* GCL_StrHash, GCL_GetLong */
#include "libgv/libgv.h"     /* GV_SetCache */
#include "strcode.h"         /* GCX_scenerio, GCX_demo */

extern void font_set_font_addr(int slot, void *data);

/* The game uses a 32-bit-offset pointer scheme (port_int_to_ptr) that
 * only works for addresses inside the 4GB-aligned `port_mem_base` pool.
 * Overlays referenced by GCL bind / delay tables MUST live inside that
 * pool or their stored offsets round-trip back to garbage addresses. */
extern void *port_malloc(size_t size);

static const char *overlay_root(void)
{
    const char *env = getenv("MGS_GCL_OVERLAY_DIR");
    return (env && *env) ? env : "./overlays";
}

static const char *script_name_for_id(int script_id)
{
    if (script_id == (int)GCL_StrHash(GCX_scenerio)) return "scenerio";
    if (script_id == (int)GCL_StrHash(GCX_demo))     return "demo";
    return NULL;
}

static long original_size(unsigned char *top)
{
    /* Recover the total byte length the .gcx occupies in the cache
     * buffer by walking the same header pointers the runtime uses
     * (GCL_LoadScript + font_set_font_addr). The font trailing blob
     * ends at the last byte the game actually reads. */
    unsigned int   proc_len;
    unsigned char *script_len_ptr;
    unsigned int   script_len;
    unsigned int   font_len;
    unsigned char *font_len_ptr;

    proc_len       = (unsigned int)GCL_GetLong((char *)top);
    script_len_ptr = top + sizeof(int) + proc_len;
    script_len     = (unsigned int)GCL_GetLong((char *)script_len_ptr);
    font_len_ptr   = script_len_ptr + sizeof(int) + script_len;
    font_len       = (unsigned int)GCL_GetLong((char *)font_len_ptr);
    return (long)(sizeof(int) + proc_len + sizeof(int) + script_len
                  + sizeof(int) + font_len);
}

unsigned char *port_gcl_overlay_load(const char *stage_name,
                                     int script_id,
                                     unsigned char *original_top)
{
    const char    *script_name;
    const char    *root;
    char           path[512];
    FILE          *fp;
    long           sz;
    long           orig_sz;

    printf("[gcl-overlay] called: stage=%s id=0x%08x\n",
           stage_name ? stage_name : "(null)", script_id);

    if (!stage_name || !stage_name[0]) {
        printf("[gcl-overlay] skip: empty stage name\n");
        return NULL;
    }
    if (!original_top) {
        printf("[gcl-overlay] skip: null original_top\n");
        return NULL;
    }
    script_name = script_name_for_id(script_id);
    if (!script_name) {
        printf("[gcl-overlay] skip: unknown script id 0x%08x "
               "(expected GCX_scenerio=0x%08x or GCX_demo=0x%08x)\n",
               script_id,
               (int)GCL_StrHash(GCX_scenerio),
               (int)GCL_StrHash(GCX_demo));
        return NULL;
    }

    root = overlay_root();
    snprintf(path, sizeof(path), "%s/%s/%s.gcx", root, stage_name, script_name);

    fp = fopen(path, "rb");
    if (!fp) {
        printf("[gcl-overlay] miss: %s\n", path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    sz = ftell(fp);
    if (sz < 12 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

    /* Two paths depending on size. Both keep the cache entry's
     * pointer consistent with what the GCL runtime actually uses —
     * that's what was causing the sna_init crash spam before. */
    orig_sz = original_size(original_top);

    /* Diagnostic: if overlay content exactly matches the cached bytes,
     * skip the swap entirely and let the runtime use `top` as normal.
     * This isolates whether the overlay MECHANISM is broken vs whether
     * specific content differences trigger the problem. */
    if (sz <= orig_sz + 1) {
        unsigned char probe[21300];
        size_t probe_sz = (size_t)sz;
        if (probe_sz > sizeof(probe)) probe_sz = sizeof(probe);
        if (fread(probe, 1, probe_sz, fp) == probe_sz
                && memcmp(probe, original_top, probe_sz) == 0
                && (size_t)sz == probe_sz) {
            fclose(fp);
            printf("[gcl-overlay] hit: %s (%ld bytes, byte-identical to "
                   "cached — no-op)\n", path, sz);
            return original_top;
        }
        /* Different content — rewind and fall through to the real load. */
        if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    }

    if (sz <= orig_sz) {
        /* Fits: overwrite in place. The cached pointer still points at
         * the original buffer, which now contains the overlay bytes. */
        if (fread(original_top, 1, (size_t)sz, fp) != (size_t)sz) {
            fclose(fp);
            return NULL;
        }
        fclose(fp);
        if (sz < orig_sz) {
            memset(original_top + sz, 0, (size_t)(orig_sz - sz));
        }
        printf("[gcl-overlay] hit: %s (%ld bytes, in-place into %p)\n",
               path, sz, (void *)original_top);
        return original_top;
    }

    /* Exceeds original size: allocate a fresh buffer INSIDE the game's
     * port_malloc pool (so port_int_to_ptr round-trips correctly for
     * any GCL pointer stored in bind/delay tables), and redirect the
     * cache entry. port_malloc is a bump allocator with no free; the
     * per-stage cost is ~20KB, acceptable. */
    {
        unsigned char *buf = (unsigned char *)port_malloc((size_t)sz);
        if (!buf) { fclose(fp); return NULL; }
        if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
            fclose(fp);
            return NULL;
        }
        fclose(fp);

        if (GV_SetCache(script_id, buf) != 0) {
            printf("[gcl-overlay] warn: GV_SetCache(0x%x) failed — cache "
                   "entry may become inconsistent\n", script_id);
        }

        printf("[gcl-overlay] hit: %s (%ld bytes, grew past original %ld; "
               "port_malloc'd %p and redirected cache entry)\n",
               path, sz, orig_sz, (void *)buf);
        return buf;
    }
}

void port_gcl_overlay_patch_font(unsigned char *original_top)
{
    unsigned int   proc_len;
    unsigned char *script_len_ptr;
    unsigned int   script_len;
    unsigned char *trailing;

    if (!original_top) return;

    proc_len       = (unsigned int)GCL_GetLong((char *)original_top);
    script_len_ptr = original_top + sizeof(int) + proc_len;
    script_len     = (unsigned int)GCL_GetLong((char *)script_len_ptr);
    trailing       = script_len_ptr + sizeof(int) + script_len;

    font_set_font_addr(2, trailing);
}
