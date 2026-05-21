#include "common.h"
#include "game/game.h"
#include "linkvar.h"
#include "psxdefs.h"

typedef struct _Spark2MWork
{
    GV_ACT   actor;        // 0x000
    int      map;          // 0x020
    DG_PRIM *prim;         // 0x024
    MATRIX   world;        // 0x028
    SVECTOR  vecs[11];     // 0x048..0x0A0
    char     pad_obj[0x0A4 - 0x048 - sizeof(SVECTOR[11])];
    OBJECT_NO_ROTS obj;    // 0x0A4..0x0C8
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
    TARGET  *f8E4;         // 0x8E4
    char     pad_8EC[0x8EC - 0x8E4 - sizeof(TARGET *)];
    void    *f8EC;         // 0x8EC — function pointer to next state
    char     pad_8F4[0x8F4 - 0x8EC - sizeof(void *)];
    int      f8F4;         // 0x8F4
    char     pad_8FC[0x8FC - 0x8F4 - sizeof(int)];
    int      f8FC;         // 0x8FC
    char     pad_900[0x900 - 0x8FC - sizeof(int)];
    int      f900;         // 0x900
    char     pad_910[0x910 - 0x900 - sizeof(int)];
    int      f910;         // 0x910
    int      f914;         // 0x914
    int      f918;         // 0x918
    char     pad_930[0x930 - 0x918 - sizeof(int)];
    int      f930;         // 0x930
    int      f934;         // 0x934
    char     pad_93C[0x93C - 0x934 - sizeof(int)];
    int      f93C;         // 0x93C
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
        work->f8E4->class = 1;
        return 1;
    }
    return 0;
}
extern int s19b_dword_800C3AB0;
extern int s19b_dword_800C3AB8;

void s19b_spark2_m_800D8918(Spark2MWork *work)
{
    TARGET *target = work->f8E4;
    int     level  = GM_DifficultyFlag;
    int     vital;

    if (level > 0)
    {
        vital = (level << 6) + 0xBF;
    }
    else
    {
        vital = 0xBF;
    }

    GM_SetTarget(target, 20, 2, (SVECTOR *)&s19b_dword_800C3AB0);
    GM_SetPowerTarget(target, 1, -1, vital, 7, (SVECTOR *)&s19b_dword_800C3AB8);
}
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
extern int s19b_spark2_m_800D899C(Spark2MWork *work);

void s19b_spark2_m_800D8B54(Spark2MWork *work, int mode)
{
    if (mode == 0)
    {
        work->f8FC = 0;
        GM_ConfigObjectAction((OBJECT *)&work->obj, 0, 0, 4);
    }
    if (s19b_spark2_m_800D88D8(work) == 0)
    {
        if (s19b_spark2_m_800D899C(work) == 0)
        {
            work->f8E4->class |= 0x14;
        }
    }
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8BC8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8CEC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D8E10.s")
extern void s19b_spark2_m_800D8AEC(Spark2MWork *work);
extern int  s19b_spark2_m_800D88D8(Spark2MWork *work);
extern void s19b_spark2_m_800D8BC8(Spark2MWork *work);

void s19b_spark2_m_800D8F34(Spark2MWork *work, int mode)
{
    if (mode == 0)
    {
        s19b_spark2_m_800D8AEC(work);
    }
    if (s19b_spark2_m_800D88D8(work) != 0)
    {
        return;
    }
    if (mode == 1)
    {
        work->f8EC = (void *)s19b_spark2_m_800D8BC8;
        work->f8F4 = 0;
        work->vecs[6].vx = 0;
        work->vecs[5].vz = 0;
    }
    work->f8E4->class |= 0x14;
}
extern void s19b_spark2_m_800D8CEC(Spark2MWork *work);

void s19b_spark2_m_800D8FB0(Spark2MWork *work, int mode)
{
    if (mode == 0)
    {
        s19b_spark2_m_800D8AEC(work);
    }
    if (s19b_spark2_m_800D88D8(work) != 0)
    {
        return;
    }
    if (mode == 1)
    {
        work->f8EC = (void *)s19b_spark2_m_800D8CEC;
        work->f8F4 = 0;
        work->vecs[6].vx = 0;
        work->vecs[5].vz = 0;
    }
    work->f8E4->class |= 0x14;
}
extern void s19b_spark2_m_800D8E10(Spark2MWork *work);

void s19b_spark2_m_800D902C(Spark2MWork *work, int mode)
{
    if (mode == 0)
    {
        s19b_spark2_m_800D8AEC(work);
    }
    if (s19b_spark2_m_800D88D8(work) != 0)
    {
        return;
    }
    if (mode == 1)
    {
        work->f8EC = (void *)s19b_spark2_m_800D8E10;
        work->f8F4 = 0;
        work->vecs[6].vx = 0;
        work->vecs[5].vz = 0;
    }
    work->f8E4->class |= 0x14;
}
extern void s19b_spark2_m_800D9148(Spark2MWork *work);

void s19b_spark2_m_800D90A8(Spark2MWork *work, int mode)
{
    if (mode < 8)
    {
        work->f8E4->class |= 0x14;
    }
    if (s19b_spark2_m_800D88D8(work) != 0)
    {
        return;
    }
    if (mode == 0)
    {
        work->f8FC = 0x10;
        GM_ConfigObjectAction((OBJECT *)&work->obj, 0x10, 0, 4);
    }
    if (work->obj.is_end != 0)
    {
        work->f8EC = (void *)s19b_spark2_m_800D9148;
        work->f8F4 = 0;
        work->vecs[6].vx = 0;
        work->vecs[5].vz = 0;
    }
}
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9148.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D91DC.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D92C8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D932C.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9390.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9434.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D94C8.s")
#pragma INCLUDE_ASM("asm/overlays/s19b/s19b_spark2_m_800D9558.s")
void s19b_spark2_m_800D95FC(Spark2MWork *work)
{
    SVECTOR diff;

    GV_SubVec3(&GM_PlayerPosition, (SVECTOR *)&work->world, &diff);
    diff.vy = 0;
    work->f930 = GV_VecDir2(&diff);
    work->f934 = GV_VecLen3(&diff);
}
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
void s19b_spark2_m_800D99C4(Spark2MWork *work)
{
    int js = Takabe_JeepSystem.field_54;
    work->f900 |= 0x10;
    if (work->f918 >= 61 || (js & 0x2000))
    {
        work->f93C = 1;
        work->f914 = 0;
    }
    if (js & 0x2)
    {
        work->f914 = 1;
        work->f900 |= 0x02000000;
    }
    work->f918 += 1;
}
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
extern void s19b_spark2_m_800D9AA8(Spark2MWork *work);
extern void s19b_spark2_m_800D9B38(Spark2MWork *work);

void s19b_spark2_m_800D9B88(Spark2MWork *work)
{
    switch (work->f910)
    {
    case 0: s19b_spark2_m_800D9AA8(work); break;
    case 1: s19b_spark2_m_800D9B38(work); break;
    }

    if (Takabe_JeepSystem.field_54 & 0x10000000)
    {
        work->f910 = 1;
    }
    else
    {
        work->f910 = 0;
    }
}
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
void s19b_spark2_m_800DA41C(LINE_F2 *prims, int count, int shade)
{
    int color;
    u_long *cw;

    color = shade | ((shade / 2) << 8) | ((shade / 2) << 16);

    while (--count >= 0)
    {
        cw = (u_long *)&prims->r0;
        *cw = (*cw & 0xFF000000) | color;
        prims++;
    }
}
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
