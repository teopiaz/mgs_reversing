#include "libgcl.h"
#include "common.h"
#include "strcode.h"
#include "libgv/libgv.h"    // for GV_SetLoader
#include "gcl_overlay.h"

/* GV_StrCode("scenerio") / GV_StrCode("demo") */
#define GCX_scenerio  0xea54
#define GCX_demo      0xa242

extern char *GM_GetArea(int flag);

int SECTION(".sbss") scenerio_code;
int SECTION(".sbss") dword_800AB994;

/**
 *  @brief      GCX bytecode initialization handler
 *
 *  If @p id is the same as @c scenerio_code the script will be loaded
 *  and set for execution, otherwise it will be skipped.
 *
 *  @param[in]  buf     pointer to cached GCX script
 *  @param[in]  id      strcode of the script's basename
 *
 *  @retval     1       on success
 *  @retval     <= 0    on failure (but this can't happen)
 */
static int GCL_InitFunc(unsigned char *top, int id)
{
    const char *stage = GM_GetArea(0);
    printf("[gcl-init] id=0x%08x scenerio_code=0x%08x stage=%s match=%d\n",
           id, scenerio_code,
           (stage && *stage) ? stage : "(empty)",
           id == scenerio_code);

    if (id == scenerio_code)
    {
        unsigned char *overlay = port_gcl_overlay_load(stage, id, top);
        if (overlay)
        {
            GCL_LoadScript(overlay);
            /* IMPORTANT: point font #2 at the ORIGINAL buffer's trailing
             * (not the overlay's). The overlay may have empty or missing
             * font data (.tail file) but the cached `top` always has the
             * real font glyph blob from the .gcx on disc. */
            port_gcl_overlay_patch_font(top);
        }
        else
        {
            GCL_LoadScript(top);
        }
    }
    return 1;
}

/**
 *  @brief      Sets which GCX script to load.
 *
 *  If @p demo_flag equals TRUE, demo.gcx will be set for execution
 *  upon loading a stage, otherwise it will default to the standard
 *  scenerio.gcx script.
 *
 *  @param      demo_flag       Sets "demo.gcx" if TRUE
 */
void GCL_ChangeSenerioCode(int demo_flag)
{
    scenerio_code = (demo_flag == TRUE)
        ? GCL_StrHash(GCX_demo)         // 0x0006a242
        : GCL_StrHash(GCX_scenerio);    // 0x0006ea54
}

void GCL_StartDaemon(void)
{
    GCL_ParseInit();
    GCL_InitVar();
    GCL_InitBasicCommands();
    GV_SetLoader('g', GCL_InitFunc);
    GCL_ChangeSenerioCode(0);
}

void GCL_ResetSystem(void)
{
    /* do nothing */
}
