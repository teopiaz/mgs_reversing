#include "libgv/libgv.h"
#include "libdg/libdg.h"

typedef struct EyeflashWork
{
    GV_ACT   actor;
    SVECTOR  field_20;
    DG_PRIM *field_28;
    MATRIX  *field_2C;
    RECT     field_30;
    int      field_38;
    DG_TEX  *field_3C;
} EyeflashWork;

SVECTOR eyeflash_svec = {0, 80, 50, 0};
const RECT eyeflash_rect = {175, 100, 350, 200};

extern int GV_Clock_800AB920;

void s00a_eyeflash_800D0984(POLY_FT4 *poly, DG_TEX *tex)
{
    int u0, u1;
    int v0, v1;

    setPolyFT4(poly);
    poly->r0 = 0x64;
    poly->g0 = 0x64;
    poly->b0 = 0x64;
    setSemiTrans(poly, 1);

    u0 = tex->field_8_offx;
    u1 = (tex->field_A_width + 1) / 3 + tex->field_8_offx - 1;

    poly->u0 = poly->u2 = u0;
    poly->u1 = poly->u3 = u1;

    v0 = tex->field_9_offy;
    v1 = tex->field_B_height + v0;
    poly->v0 = poly->v1 = v0;
    poly->v2 = poly->v3 = v1;

    poly->tpage = tex->field_4_tPage;
    poly->clut = tex->field_6_clut;
}

void s00a_eyeflash_800D0A28(EyeflashWork *work)
{
    DG_TEX   *tex;
    int       field_38;
    int       u0, u1;
    int       v0, v1;
    POLY_FT4 *poly;
    short     clut;
    int       tpage;

    field_38 = 5 - work->field_38;
    if (field_38 >= 3)
    {
        field_38 -= 3;
    }

    tex = work->field_3C;
    poly = &work->field_28->packs[GV_Clock_800AB920]->poly_ft4;

    u0 = tex->field_8_offx + (tex->field_A_width + 1) * field_38 / 3;
    u1 = tex->field_8_offx + (tex->field_A_width + 1) * (field_38 + 1) / 3 - 1;

    poly->u0 = poly->u2 = u0;
    poly->u1 = poly->u3 = u1;

    v0 = tex->field_9_offy;
    v1 = tex->field_B_height + v0;

    poly->v0 = poly->v1 = v0;
    poly->v2 = poly->v3 = v1;

    poly->tpage = tex->field_4_tPage;

    clut = tex->field_6_clut;
    tpage = (poly->tpage & 0xFF9F) | 0x20;

    poly->tpage = tpage;
    poly->clut = clut;
    poly->tpage = tpage;
}

void s00a_eyeflash_800D0B18(EyeflashWork *work)
{
    if (--work->field_38 == 0)
    {
        GV_DestroyActor_800151C8(&work->actor);
    }
    work->field_28->world = *work->field_2C;
    s00a_eyeflash_800D0A28(work);
}

void s00a_eyeflash_800D0BA4(EyeflashWork *work)
{
    DG_PRIM *prim;

    prim = work->field_28;
    if (prim)
    {
        DG_DequeuePrim_800182E0(prim);
        DG_FreePrim_8001BC04(prim);
    }
}

int s00a_eyeflash_800D0BE0(EyeflashWork *work, int arg1)
{
    DG_PRIM *prim;
    RECT     unused;
    DG_TEX  *tex;

    unused = eyeflash_rect; // let's waste cycles

    work->field_30 = eyeflash_rect;
    work->field_28 = prim = DG_GetPrim(0x412, 1, 0, &eyeflash_svec, &work->field_30);

    if (prim)
    {
        prim->field_2E_k500 = 1000;
        work->field_3C = tex = DG_GetTexture_8001D830(GV_StrCode_80016CCC("kirari"));
        if (tex)
        {
            s00a_eyeflash_800D0984(&prim->packs[0]->poly_ft4, tex);
            s00a_eyeflash_800D0984(&prim->packs[1]->poly_ft4, tex);
            return 0;
        }
    }

    return -1;
}

GV_ACT *NewEyeflash_800D0CF4(MATRIX *arg0, SVECTOR *arg1, int arg2, int arg3)
{
    EyeflashWork *work;

    work = (EyeflashWork *)GV_NewActor_800150E4(5, sizeof(EyeflashWork));
    if (work != NULL)
    {
        GV_SetNamedActor_8001514C(&work->actor, (TActorFunction)s00a_eyeflash_800D0B18,
                                  (TActorFunction)s00a_eyeflash_800D0BA4, "eyeflash.c");
        work->field_20 = *arg1;
        work->field_38 = 6;
        work->field_2C = arg0;
        work->field_20.vy += arg3;
        if (s00a_eyeflash_800D0BE0(work, arg2) < 0)
        {
            GV_DestroyActor_800151C8(&work->actor);
            return NULL;
        }
    }
    return &work->actor;
}
