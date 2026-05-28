/* Centralised CHARA override table for the port.
 *
 * Hooked at source/game/chara.c:GM_GetCharaID — every actor lookup passes
 * through port_chara_override(), which consults the table below to either:
 *   - REPLACE the actor's constructor with a port-specific implementation, or
 *   - DISABLE the actor entirely (replacement = NULL → actor isn't spawned).
 *
 * Why a table here instead of #ifdef PORT_BUILD in each actor's New*():
 *   - source/ stays pristine: future "disable actor X on the port" is a
 *     one-line edit in THIS file; no source/ touch, no rebase conflicts.
 *   - One place to audit which PSX-only effects are currently dropped.
 *
 * What goes here:
 *   - PSX-hardware-specific effects whose ports aren't implemented yet
 *     (framebuffer-readback blur, DR_STP mask machinery, ...). Set the
 *     replacement to NULL to make the actor a no-op.
 *   - Effects that need a different port-side implementation. Set the
 *     replacement to your port function.
 *
 * Defensive fallback: any chara whose registered function pointer is below
 * 0x100000000 is treated as a stale PSX address (0x80XXXXXX) and dropped.
 * This was previously inline in GM_GetCharaID; centralised here so source/
 * has no special handling for it.
 */

#include "common.h"
#include "game/game.h"
#include "strcode.h"

typedef struct {
    int       chara_id;
    NEWCHARA *replacement;  /* NULL = disable actor entirely */
} PortCharaOverride;

static const PortCharaOverride k_overrides[] = {
    /* PSX-hardware-only effects: framebuffer-readback blur + DR_STP mask.
       Without these PSX features the port renders opaque overlays — see
       commit fad64d75a for the original symptom (s02b duct iris). */
    { CHARAID_0025_BLUR,  NULL },   /* okajima/blur.c       NewBlurSet     */
    { CHARAID_0044_GHOST, NULL },   /* okajima/blurpure.c   NewBlurPure    */
    /* { CHARAID_X, NewPortReplacementY }, // future overrides go here */
    { 0, NULL }   /* sentinel */
};

NEWCHARA *port_chara_override(int chara_id, NEWCHARA *original)
{
    for (const PortCharaOverride *o = k_overrides; o->chara_id != 0; o++)
    {
        if (o->chara_id == chara_id)
            return o->replacement;
    }
    /* Defensive: drop raw PSX addresses (0x80XXXXXX) — stage tables that
       weren't migrated to port symbols still have these. Originally inline
       in GM_GetCharaID. */
    if ((unsigned long)(unsigned long long)original < 0x100000000ULL)
        return NULL;
    return original;
}
