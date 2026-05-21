#include "common.h"
#include "game/game.h"
#include "psxdefs.h"

typedef struct _Spark2MWork
{
    GV_ACT   actor;        // 0x000
    int      map;          // 0x020
    DG_PRIM *prim;         // 0x024
    MATRIX   world;        // 0x028
    SVECTOR  vecs[16];     // 0x048
    SVECTOR  verts[32];    // 0x0C8
    char     pad2[0x28];   // 0x1C8
    int      f1F0;         // 0x1F0 — last allocated field; sizeof = 0x1F4 (literal in NewSpark2M alloc)
    char     pad_730[0x730 - 0x1F0 - sizeof(int)];
    SVECTOR  sv_730;       // 0x730
    char     pad_750[0x750 - 0x730 - sizeof(SVECTOR)];
    SVECTOR  sv_750;       // 0x750
    char     pad_7A0[0x7A0 - 0x750 - sizeof(SVECTOR)];
    SVECTOR  sv_7A0;       // 0x7A0 (vy at 0x7A2)
    SVECTOR  sv_7A8;       // 0x7A8 (vy at 0x7AA)
    char     pad_8E4[0x8E4 - 0x7A8 - sizeof(SVECTOR)];
    short   *f8E4;         // 0x8E4
    char     pad_900[0x900 - 0x8E4 - sizeof(short *)];
    int      f900;         // 0x900
    char     pad_914[0x914 - 0x900 - sizeof(int)];
    int      f914;         // 0x914
    int      f918;         // 0x918
    char     pad_930[0x930 - 0x918 - sizeof(int)];
    int      f930;         // 0x930
} Spark2MWork;

typedef struct _JEEP_SYSTEM_S
{
    char     pad1[0x4];
    CONTROL *control;
    char     pad2[0x10];
    SVECTOR  pos;
    char     pad3[0x54 - 0x18 - sizeof(SVECTOR)];
    int      field_54;
} JEEP_SYSTEM_S;

extern JEEP_SYSTEM_S Takabe_JeepSystem;

extern void s19b_spark2_m_800DA19C(SVECTOR *, SVECTOR *, int);
extern void s19b_spark2_m_800DA314(void *, SVECTOR *, int);
extern void s19b_spark2_m_800DA3EC(LINE_F2 *, int, int);
extern void s19b_spark2_m_800DA41C(LINE_F2 *, int, int);

#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8724.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D87A4.s")
extern int s19b_spark2_m_800D87A4(Spark2MWork *work);
int s19b_spark2_m_800D88D8(Spark2MWork *work)
{
    if (s19b_spark2_m_800D87A4(work) != 0)
    {
        *work->f8E4 = 1;
        return 1;
    }
    return 0;
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8918.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D899C.s")
void s19b_spark2_m_800D8A48(Spark2MWork *work)
{
    GV_NearExp4PV(&work->sv_730, &work->sv_7A0, 3);
    GV_NearExp4PV(&work->sv_750, &work->sv_7A8, 3);
}
void s19b_spark2_m_800D8A88(Spark2MWork *work)
{
    short *p = (short *)work->map;
    int    v = work->f930 - p[5];
    work->sv_7A0.vy = v;
    work->sv_7A8.vy = v;
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8AAC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8ACC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8AEC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8B54.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8BC8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8CEC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8E10.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8F34.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8FB0.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D902C.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D90A8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9148.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D91DC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D92C8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D932C.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9390.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9434.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D94C8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9558.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D95FC.s")
extern void s19b_spark2_m_800D9558(Spark2MWork *work);
extern void s19b_spark2_m_800D95FC(Spark2MWork *work);
extern void s19b_spark2_m_800D9C04(Spark2MWork *work);

void s19b_spark2_m_800D964C(Spark2MWork *work)
{
    s19b_spark2_m_800D95FC(work);
    s19b_spark2_m_800D9C04(work);
    s19b_spark2_m_800D9558(work);
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9680.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9704.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D97A8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D985C.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9910.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D99C4.s")
void s19b_spark2_m_800D9A30(Spark2MWork *work)
{
    if (Takabe_JeepSystem.field_54 & 0x1000)
    {
        work->f914 = 4;
        work->f918 = 0;
        work->f900 |= 0x10;
    }
    else
    {
        work->f918 += 1;
    }
}
void s19b_spark2_m_800D9A74(Spark2MWork *work)
{
    int new_f900 = work->f900 | 0x10;
    int js = Takabe_JeepSystem.field_54;
    work->f900 = new_f900;
    if (js & 0x2000)
    {
        work->f914 = 0;
    }
    work->f918 += 1;
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9AA8.s")
extern void s19b_spark2_m_800D9A74(Spark2MWork *work);

void s19b_spark2_m_800D9B38(Spark2MWork *work)
{
    switch (work->f914)
    {
    case 0:
        s19b_spark2_m_800D9A30(work);
        break;
    case 4:
        s19b_spark2_m_800D9A74(work);
        break;
    }
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9B88.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9C04.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9C90.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9EC0.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800DA0B4.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800DA19C.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800DA314.s")
void s19b_spark2_m_800DA3EC(LINE_F2 *prims, int count, int unused)
{
    (void)unused;
    while (--count >= 0)
    {
        setLineF2(prims);
        prims++;
    }
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800DA41C.s")
void s19b_spark2_m_800DA46C(Spark2MWork *work)
{
    int      time;
    DG_PRIM *prim;
    int      shade;

    GM_CurrentMap = work->map;

    time = --work->f1F0;
    if (time <= 0)
    {
        GV_DestroyActor(&work->actor);
        return;
    }

    s19b_spark2_m_800DA314(work->vecs, work->verts, 16);

    prim = work->prim;

    shade = (time * 16) + 50;
    if (shade > 255)
    {
        shade = 255;
    }

    s19b_spark2_m_800DA41C((LINE_F2 *)prim->packs[GV_Clock], 16, shade);

    work->world.t[0] += Takabe_JeepSystem.pos.vx;
    work->world.t[1] += Takabe_JeepSystem.pos.vy;
    work->world.t[2] += Takabe_JeepSystem.pos.vz;

    DG_SetPos(&work->world);
    DG_PutPrim(&work->prim->world);
}
void s19b_spark2_m_800DA55C(Spark2MWork *work)
{
    GM_FreePrim(work->prim);
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800DA598.s")
extern int s19b_spark2_m_800DA598(Spark2MWork *work, int arg);
extern const char aSpark2mC_800DDEB0[];

#define EXEC_LEVEL GV_ACTOR_USER

GV_ACT *NewSpark2M_800DA6D8(int arg0)
{
    Spark2MWork *work;

    work = (Spark2MWork *)GV_NewActor(EXEC_LEVEL, 0x1F4);
    if (work != NULL)
    {
        GV_SetNamedActor(&work->actor, s19b_spark2_m_800DA46C, s19b_spark2_m_800DA55C, aSpark2mC_800DDEB0);

        SetSpadStack(SPAD_STACK_ADDR);

        if (s19b_spark2_m_800DA598(work, arg0) < 0)
        {
            ResetSpadStack();

            GV_DestroyActor(&work->actor);
            return NULL;
        }

        ResetSpadStack();
    }

    return &work->actor;
}
void s19b_fadeio_800DA784(void)
{
    GV_ZeroMemory(&Takabe_JeepSystem, 0x16C);
}
