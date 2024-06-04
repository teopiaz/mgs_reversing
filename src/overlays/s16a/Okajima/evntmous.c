#include "libdg/libdg.h"
#include "libgv/libgv.h"
#include "Game/game.h"
#include "Game/homing_target.h"
#include "Game/linkvarbuf.h"
#include "Game/object.h"
#include "Okajima/blood.h"

typedef struct _EventmouseWork
{
    GV_ACT   actor;
    TARGET  *target;
    CONTROL  control;
    OBJECT   body;
    MATRIX   light[2];
    DG_PRIM *prim;
    int      map;
    int      f1CC;
    int      f1D0;
    int      f1D4;
    int      f1D8;
    int      f1DC[8];
    int      f1FC;
    int      f200;
    int      f204;
    int      f208;
    SVECTOR  f20C[128];
    SVECTOR  pos;
    SVECTOR  f614;
    int      f61C;
    int      f620;
    int      f624;
    int      f628;
    int      f62C;
    int      weapon;
    int      item;
    int      f638;
    int      f63C;
    SVECTOR  prim_vecs[4];
    int      f660;
    int      f664;
    HOMING  *hom;
    MATRIX   hom_mtx;
    int      f68C;
    void    *f690;
    SVECTOR  eye;
    SVECTOR  center;
} EventmouseWork;

SVECTOR eventmous_vecs[2] = {{48, 0, 96, 0}, {-48, 0, 96, 0}};

extern MATRIX  DG_ZeroMatrix_8009D430;
extern int     GV_Time_800AB330;
extern SVECTOR DG_ZeroVector_800AB39C;
extern int     GM_GameStatus_800AB3CC;
extern int     GM_GameOverTimer_800AB3D4;
extern int     GV_Clock_800AB920;
extern int     GM_CurrentMap_800AB9B0;
extern int     GM_PlayerMap_800ABA0C;
extern SVECTOR GM_PlayerPosition_800ABA10;
extern int     GM_NoisePower_800ABA24;

void   AN_Unknown_800CA320( MATRIX *, int );
void * NewRipple_800D7F30( MATRIX *, int );
void   NewSplash2_800DB6F0( int, SVECTOR *, int );

void * NewCinemaScreen_800DE434( int, int );
int    NewCinemaScreenClose_800DE4CC( void * );

#define EXEC_LEVEL 5

void Eventmouse_800C8E88(EventmouseWork *work, SVECTOR *arg1, int arg2)
{
    SVECTOR view;
    SVECTOR rot;

    if ((GM_SnakeCurrentHealth == 0) || (GM_GameOverTimer_800AB3D4 != 0) || (work->f68C != 0))
    {
        return;
    }

    work->f68C = arg2;

    if ((arg1->vy > -10) && (arg1->vz > -18010))
    {
        work->eye.vx = -12021;
        work->eye.vy = 5304;
        work->eye.vz = -7665;
    }
    else if (arg1->vz > 5990)
    {
        work->eye.vx = -1149;
        work->eye.vy = -348;
        work->eye.vz = 9080;
    }
    else if ((arg1->vx > 11990) && (arg1->vz > -4010))
    {
        work->eye.vx = 13633;
        work->eye.vy = 824;
        work->eye.vz = 7871;
    }
    else
    {
        work->f68C = 1;
        return;
    }

    work->center = work->control.field_0_mov;
    GV_SubVec3_80016D40(&work->center, &work->eye, &view);

    rot.vy = ratan2(view.vx, view.vz) & 0xFFF;
    rot.vx = ratan2(GV_VecLen3_80016D80(&view), rot.vy) & 0xFFF;
    rot.vz = 0;

    view.vx = 0;
    view.vy = 0;
    view.vz = -6000;

    DG_SetPos2_8001BC8C(&work->center, &rot);
    DG_PutVector_8001BE48(&view, &work->eye, 1);

    work->item = GM_CurrentItemId;
    work->weapon = GM_CurrentWeaponId;

    if ((GM_CurrentItemId == ITEM_SCOPE) || (GM_CurrentItemId == ITEM_CAMERA) ||
        (GM_CurrentItemId == ITEM_N_V_G) || (GM_CurrentItemId == ITEM_THERM_G))
    {
        GM_CurrentItemId = ITEM_NONE;
    }

    if ((GM_CurrentWeaponId == WEAPON_PSG1) || (GM_CurrentWeaponId == WEAPON_STINGER))
    {
        GM_CurrentWeaponId = WEAPON_NONE;
    }

    GM_GameStatus_800AB3CC |= GAME_FLAG_BIT_23 | GAME_FLAG_BIT_20 | GAME_FLAG_BIT_18;
    GM_GameStatus_800AB3CC |= GAME_FLAG_BIT_29;

    work->f690 = NewCinemaScreen_800DE434(0x77359400, 1);
}

void Eventmouse_800C90E4(int proc_id, SVECTOR *vec)
{
    GCL_ARGS args;
    long     data[4];

    data[0] = vec->vx;
    data[1] = vec->vy;
    data[2] = vec->vz;
    data[3] = vec->pad;

    args.argc = 4;
    args.argv = data;
    GCL_ExecProc_8001FF2C(proc_id, &args);
}

int Eventmouse_800C9140(SVECTOR *a, SVECTOR *b)
{
    int x, y, z;

    x = (a->vx - b->vx) >> 4;
    y = (a->vy - b->vy) >> 4;
    z = (a->vz - b->vz) >> 4;
    return SquareRoot0(x * x + y * y + z * z) * 16;
}

int Eventmouse_800C91B0(EventmouseWork *work, SVECTOR *arg1)
{
    int ret;

    if (((arg1->vy > -10) && (arg1->vz > -18010)) || (arg1->vz >= 0x1767) || ((arg1->vx > 11990) && (arg1->vz > -4010)))
    {
        work->hom->flag = ret = 1;
        work->hom_mtx = work->body.objs->world;
        work->hom_mtx.t[1] += 200;
    }
    else
    {
        work->hom->flag = 0;
        ret = 0;
    }

    return ret;
}

void Eventmouse_800C9288(SVECTOR *from, SVECTOR *to, SVECTOR *out)
{
    SVECTOR diff;
    int     y;

    GV_SubVec3_80016D40(to, from, &diff);
    out->vy = ratan2(diff.vx, diff.vz) & 0xFFF;

    y = diff.vy;
    diff.vy = 0;

    out->vx = (ratan2(GV_VecLen3_80016D80(&diff), y) & 0xFFF) - 1024;
    out->vz = 0;
}

void Eventmouse_800C9308(EventmouseWork *work)
{
    SVECTOR sp10;
    SVECTOR pos;
    SVECTOR rot;

    pos = work->control.field_0_mov;

    if ((pos.vy > -10) && (pos.vz > -18010))
    {
        sp10.vx = -8000;
        sp10.vy = 0;
        sp10.vz = -14500;
    }
    else if (pos.vz > 5990)
    {
        sp10.vx = 7000;
        sp10.vy = -2000;
        sp10.vz = 9000;
    }
    else if ((pos.vx > 11990) && (pos.vz > -4010))
    {
        sp10.vx = 12500;
        sp10.vy = -2000;
        sp10.vz = -1500;
    }

    Eventmouse_800C9288(&pos, &sp10, &rot);

    work->f614 = DG_ZeroVector_800AB39C;
    work->f614.vz = Eventmouse_800C9140(&pos, &sp10) / 30;

    DG_SetPos2_8001BC8C(&DG_ZeroVector_800AB39C, &rot);
    DG_PutVector_8001BE48(&work->f614, &work->f614, 1);
}

void Eventmouse_800C948C(EventmouseWork *work, SVECTOR *pos)
{
    SVECTOR sp10;
    SVECTOR sp18;

    work->f208 = 1;

    sp10 = *pos;
    Eventmouse_800C9308(work);

    sp10.vx += work->f614.vx / 2;
    sp10.vz += work->f614.vz / 2;

    sp18 = sp10;

    if ((sp10.vy > -10) && (sp10.vz > -18010))
    {
        sp10.vy = 0;
        Eventmouse_800C8E88(work, &sp10, 120);
        sp10.pad = 500;
    }
    else if (sp10.vz > 5990)
    {
        if (sp10.vz >= 0x1B59)
        {
            sp10.vy = -3000;
            sp18.vy = -2000;
            NewSplash2_800DB6F0(GV_RandU_80017090(4096), &sp18, 0);
            sp10.pad = 1000;
            GM_SeSet2_80032968(0, 63, 187);
        }
        else
        {
            sp10.vy = -2000;
            sp10.pad = 500;
        }

        Eventmouse_800C8E88(work, &sp10, 120);
    }
    else if ((sp10.vx > 11990) && (sp10.vz > -4010))
    {
        sp10.vy = -3000;
        sp18.vy = -2000;
        NewSplash2_800DB6F0(GV_RandU_80017090(4096), &sp18, 0);
        Eventmouse_800C8E88(work, &sp10, 120);
        sp10.pad = 1000;
        GM_SeSet2_80032968(0, 63, 176);
    }

    GM_SeSet2_80032968(0, 63, 13);
    Eventmouse_800C90E4(work->f660, &sp10);
}

void Eventmouse_800C96A8(EventmouseWork *work)
{
    SVECTOR  pos;
    SVECTOR  sp18[2];
    MATRIX   rot;
    SVECTOR *vecs;

    pos = work->control.field_0_mov;
    rot = DG_ZeroMatrix_8009D430;
    vecs = work->prim_vecs;

    RotMatrixYXZ_gte(&work->control.field_8_rot, &rot);
    DG_SetPos_8001BC44(&rot);
    DG_PutVector_8001BE48(eventmous_vecs, sp18, 2);

    vecs[0].vx = pos.vx + sp18[0].vx;
    vecs[3].vx = pos.vx - sp18[0].vx;
    vecs[0].vz = pos.vz + sp18[0].vz;
    vecs[3].vz = pos.vz - sp18[0].vz;

    vecs[1].vx = pos.vx + sp18[1].vx;
    vecs[2].vx = pos.vx - sp18[1].vx;
    vecs[1].vz = pos.vz + sp18[1].vz;
    vecs[2].vz = pos.vz - sp18[1].vz;

    vecs[0].vy = vecs[1].vy = vecs[2].vy = vecs[3].vy = pos.vy;
}

int Eventmouse_800C9828(EventmouseWork *work)
{
    if (!(work->map & GM_PlayerMap_800ABA0C))
    {
        return 0;
    }

    if (work->target->damaged & (TARGET_PUSH | TARGET_POWER))
    {
        GM_SeSet2_80032968(0, 63, 185);
        return 1;
    }

    return 0;
}

void Eventmouse_800C9890(POLY_FT4 *poly, DG_TEX *tex)
{
    int x, y, w, h;

    setPolyFT4(poly);
    setSemiTrans(poly, 1);

    x = tex->field_8_offx;
    w = tex->field_A_width;
    y = tex->field_9_offy;
    h = tex->field_B_height;
    setUVWH(poly, x, y, w, h);

    poly->tpage = tex->field_4_tPage;
    poly->clut = tex->field_6_clut;
}

void Eventmouse_800C98F0(EventmouseWork *work)
{
    SVECTOR  pos;
    SVECTOR  sp18;
    SVECTOR  rot;
    SVECTOR  mov;
    SVECTOR  mov2;
    MATRIX   sp38;
    CONTROL *control;
    int      flags;
    int      var_s3;
    int      dist;
    int      index;

    pos = work->control.field_0_mov;
    sp18 = work->f20C[work->f620];

    control = &work->control;

    if (!Eventmouse_800C91B0(work, &pos))
    {
        work->target->damaged = 0;
        work->f628 = 0;

        flags = work->target->class & ~(TARGET_SEEK | TARGET_PUSH | TARGET_POWER | TARGET_AVAIL);
        work->target->class = flags | TARGET_AVAIL;

        DG_InvisibleObjs(work->body.objs);
        DG_InvisiblePrim(work->prim);

        if (GV_RandU_80017090(2) > 0)
        {
            return;
        }

        var_s3 = work->f624;
    }
    else
    {
        work->f1D8++;

        work->target->class |= TARGET_SEEK | TARGET_PUSH | TARGET_POWER | TARGET_AVAIL;
        GM_MoveTarget_8002D500(work->target, &control->field_0_mov);
        GM_PushTarget_8002DA14(work->target);

        DG_VisibleObjs(work->body.objs);

        if ((work->f204 != 1) && Eventmouse_800C9828(work))
        {
            work->f204 = 1;
            work->f1CC = 0;
            work->pos = work->control.field_0_mov;
            Eventmouse_800C9308(work);
            return;
        }

        if (work->f1FC > 0)
        {
            work->f1FC--;
            return;
        }

        dist = Eventmouse_800C9140(&control->field_0_mov, &GM_PlayerPosition_800ABA10);

        DG_VisiblePrim(work->prim);
        Eventmouse_800C96A8(work);

        if (work->f628 == 0)
        {
            if ((dist < 2000) || (GM_NoisePower_800ABA24 > 0))
            {
                AN_Unknown_800CA320(&work->body.objs->world, 0);
                GM_SeSet_80032858(&control->field_0_mov, 184);

                work->f628 = 1;
                work->f1FC = 15;

                index = work->f620;
                while (Eventmouse_800C91B0(work, &work->f20C[index]))
                {
                    if (++index >= work->f61C)
                    {
                        index = 0;
                    }
                }

                if (--index < 0)
                {
                    index = work->f61C - 1;
                }

                work->f620 = index;

                if ((work->f208 == 0) && (GM_NoisePower_800ABA24 == 0) && (work->f62C < ++work->f200) && (dist > 1000))
                {
                    mov2 = control->field_0_mov;
                    Eventmouse_800C9308(work);
                    mov2.vx += work->f614.vx;
                    mov2.vz += work->f614.vz;
                    Eventmouse_800C948C(work, &mov2);
                }
            }

            if (--work->f1D0 < 0)
            {
                work->f1D4 = 1 - work->f1D4;
                if (work->f1D4 == 0)
                {
                    work->f1D0 = GV_RandU_80017090(64) + 64;
                }
                else
                {
                    work->f1D0 = GV_RandU_80017090(32) + 32;
                }
            }

            if (work->f1D4 == 0)
            {
                return;
            }

            var_s3 = work->f624 / 4;
            if ((control->field_0_mov.vx > 11990) && (control->field_0_mov.vz > -4010))
            {
                work->f628 = 1;
            }
        }
        else
        {
            var_s3 = work->f624 / 3;
            control->field_8_rot = control->field_4C_turn;
            if (((GV_Time_800AB330 % 3) == 0) && (control->field_0_mov.vx > 11990) && (control->field_0_mov.vz > -4010))
            {
                sp38 = DG_ZeroMatrix_8009D430;
                sp38.t[0] = control->field_0_mov.vx + GV_RandS_800170BC(64);
                sp38.t[1] = -2000;
                sp38.t[2] = control->field_0_mov.vz + GV_RandU_80017090(64);
                NewRipple_800D7F30(&sp38, 1500);
            }
        }
    }

    if (Eventmouse_800C9140(&pos, &sp18) < var_s3)
    {
        control->field_0_mov = sp18;
        if (++work->f620 >= work->f61C)
        {
            work->f620 = 0;
        }
    }
    else
    {
        Eventmouse_800C9288(&pos, &sp18, &rot);
        mov = DG_ZeroVector_800AB39C;
        mov.vz = var_s3;
        DG_SetPos2_8001BC8C(&pos, &rot);
        DG_PutVector_8001BE48(&mov, &mov, 1);
        control->field_0_mov = mov;
        control->field_4C_turn = rot;
    }

    control->field_8_rot.vy += work->f1DC[work->f1D8 % 8] * 64;
}

void EventMouseAct_800C9F14(EventmouseWork *work)
{
    VECTOR   scale;
    MATRIX   world;
    CONTROL *control;
    int      f1CC;
    int      t1, t2;

    control = &work->control;

    if (work->f68C > 0)
    {
        work->f68C--;

        DG_LookAt_800172D0(DG_Chanl(0), &work->eye, &work->center, 320);

        if (work->f68C == 0)
        {
            if ((work->weapon == WEAPON_PSG1) || (work->weapon == WEAPON_STINGER))
            {
                GM_CurrentWeaponId = work->weapon;
            }

            if ((work->item == ITEM_SCOPE) || (work->item == ITEM_CAMERA) ||
                (work->item == ITEM_N_V_G) || (work->item == ITEM_THERM_G))
            {
                GM_CurrentItemId = work->item;
            }

            if (work->f690 != 0)
            {
                NewCinemaScreenClose_800DE4CC(work->f690);
            }

            GM_GameStatus_800AB3CC &= ~(PLAYER_PREVENT_WEAPON_SWITCH | PLAYER_THROWING | PLAYER_KNOCKING);
            GM_GameStatus_800AB3CC &= ~STATE_PADRELEASE;

            GV_DestroyActor_800151C8(&work->actor);
        }
    }

    if (work->f1CC < 0)
    {
        DG_InvisibleObjs(work->body.objs);
        DG_InvisiblePrim(work->prim);
        GM_ConfigControlAttribute_8002623C(control, RADAR_OFF);
        return;
    }

    if ((work->f208 == 0) && (GM_CurrentItemId == ITEM_MINE_D))
    {
        GM_ConfigControlAttribute_8002623C(control, RADAR_VISIBLE);
    }
    else
    {
        GM_ConfigControlAttribute_8002623C(control, RADAR_OFF);
    }

    GM_CurrentMap_800AB9B0 = work->map;

    Eventmouse_800C98F0(work);

    if (work->f204 == 1)
    {
        DG_InvisibleObjs(work->body.objs);
        DG_InvisiblePrim(work->prim);

        if (work->f1CC > 30)
        {
            work->control.field_55_skip_flag |= CTRL_SKIP_TRAP;

            if (work->f208 == 0)
            {
                work->f1CC = -10000;
                Eventmouse_800C948C(work, &work->pos);
            }

            if (work->f68C == 0)
            {
                return;
            }
        }
        else
        {
            control->field_8_rot.vy += 128;
            control->field_8_rot.vx += 128;
            control->field_4C_turn = control->field_8_rot;

            f1CC = ++work->f1CC;
            t1 = 21 - f1CC;
            t2 = 20 - f1CC;

            work->pos.vx += work->f614.vx;
            work->pos.vz += work->f614.vz;
            work->pos.vy += (t1 * t1 - t2 * t2) * 4;

            control->field_0_mov = work->pos;

            if ((f1CC & 1) == 0)
            {
                DG_SetPos2_8001BC8C(&work->pos, &control->field_8_rot);
                ReadRotMatrix(&world);
                NewBlood_80072728(&world, 1);
            }
        }
    }

    GM_ActControl_80025A7C(control);

    work->body.objs->light[GV_Clock_800AB920].t[0] = 200;
    work->body.objs->light[GV_Clock_800AB920].t[1] = 200;
    work->body.objs->light[GV_Clock_800AB920].t[2] = 200;

    GM_ActObject2_80034B88(&work->body);

    if (work->f208 == 0)
    {
        scale.vx = 6144;
        scale.vy = 6144;
        scale.vz = 6144;
    }
    else
    {
        scale.vx = 4096;
        scale.vy = 4096;
        scale.vz = 4096;
    }

    ScaleMatrix(&work->body.objs->world, &scale);
    DG_GetLightMatrix2_8001A5D8(&control->field_0_mov, work->light);
}

void EventMouseDie_800CA2C4(EventmouseWork *work)
{
    DG_PRIM *prim;

    GM_FreeControl_800260CC(&work->control);
    GM_FreeObject_80034BF8(&work->body);
    GM_FreeTarget_8002D4B0(work->target);
    HomingTarget_Free_80032CFC(work->hom);

    prim = work->prim;
    if (prim != NULL)
    {
        DG_DequeuePrim_800182E0(prim);
        DG_FreePrim_8001BC04(prim);
    }

    if (work->f690 != NULL)
    {
        NewCinemaScreenClose_800DE4CC(work->f690);
    }

    GM_GameStatus_800AB3CC &= ~(PLAYER_PREVENT_WEAPON_SWITCH | PLAYER_THROWING | PLAYER_KNOCKING);
    GM_GameStatus_800AB3CC &= ~STATE_PADRELEASE;
}

int EventMouseGetResources_800CA370(EventmouseWork *work, HZD_PTP *points, short n_points, int arg3, int arg4, int name, int map, int arg7, int arg8)
{
    SVECTOR  size;
    int      start;
    SVECTOR *vec;
    int      i;
    CONTROL *control;
    OBJECT  *body;
    int      model;
    DG_PRIM *prim;
    DG_TEX  *tex;

    work->f61C = n_points;

    if (n_points < 2)
    {
        return -1;
    }

    start = GV_RandU_80017090(4096) % (n_points - 1);
    work->f620 = start + 1;

    vec = work->f20C;
    for (i = 0; i < n_points; i++)
    {
        vec->vx = points->x;
        vec->vy = points->y;
        vec->vz = points->z;
        vec++;
        points++;
    }

    control = &work->control;
    if (GM_InitLoader_8002599C(control, name, map) < 0)
    {
        return -1;
    }

    GM_ConfigControlAttribute_8002623C(control, RADAR_VISIBLE);
    GM_ConfigControlInterp_80026244(control, 4);
    GM_ConfigControlHazard_8002622C(control, -1, -2, -1);
    work->control.field_0_mov = work->f20C[start];

    body = &work->body;
    model = GV_StrCode_80016CCC("mouse");
    GM_InitObject_80034A18(body, model, BODY_FLAG | DG_FLAG_ONEPIECE, 0);
    GM_ConfigObjectJoint_80034CB4(body);
    GM_ConfigObjectLight_80034C44(body, work->light);
    work->body.objs->objs[0].raise = 200;

    work->f204 = 0;
    work->f624 = arg3;
    work->f1D8 = 0;
    work->f628 = 0;
    work->f200 = 0;
    work->f208 = 0;
    work->f62C = arg4;
    work->f1D0 = GV_RandU_80017090(32) + 16;
    work->f1D4 = 0;
    work->f660 = arg7;
    work->f63C = arg8;
    work->f664 = 0;
    work->f638 = 0;

    work->target = GM_AllocTarget_8002D400();
    if (work->target != NULL)
    {
        size.vx = 70;
        size.vy = 800;
        size.vz = 70;

        GM_SetTarget_8002DC74(work->target, 0x1D, ENEMY_SIDE, &size);
        control->field_36 = -2;
    }

    work->hom = HomingTarget_Alloc_80032C8C(&work->hom_mtx, control);
    work->hom->flag = 1;

    prim = DG_GetPrim(0x12, 1, 0, work->prim_vecs, NULL);
    work->prim = prim;
    if (prim != NULL)
    {
        prim->field_2E_k500 = 500;
    }

    tex = DG_GetTexture_8001D830(GV_StrCode_80016CCC("shadow"));
    if (tex == NULL)
    {
        return 0;
    }

    Eventmouse_800C9890(&work->prim->packs[0]->poly_ft4, tex);
    Eventmouse_800C9890(&work->prim->packs[1]->poly_ft4, tex);

    work->prim->packs[0]->poly_ft4.r0 = 80;
    work->prim->packs[0]->poly_ft4.g0 = 80;
    work->prim->packs[0]->poly_ft4.b0 = 80;

    work->prim->packs[1]->poly_ft4.r0 = 80;
    work->prim->packs[1]->poly_ft4.g0 = 80;
    work->prim->packs[1]->poly_ft4.b0 = 80;

    work->f1DC[2] = 2;
    work->f1DC[0] = 0;
    work->f1DC[1] = 1;
    work->f1DC[3] = 1;
    work->f1DC[4] = 0;
    work->f1DC[5] = -1;
    work->f1DC[6] = -2;
    work->f1DC[7] = -1;

    work->f68C = 0;
    work->f690 = NULL;
    return 0;
}

GV_ACT * NewEventmouse_800CA6F4(HZD_PTP *points, short n_points, int arg2, int arg3, int arg4, int arg5)
{
    EventmouseWork *work;

    work = (EventmouseWork *)GV_NewActor_800150E4(EXEC_LEVEL, sizeof(EventmouseWork));
    if (work != NULL)
    {
        GV_SetNamedActor_8001514C(&work->actor, (TActorFunction)EventMouseAct_800C9F14, (TActorFunction)EventMouseDie_800CA2C4, "evntmous.c");

        if (EventMouseGetResources_800CA370(work, points, n_points, arg2, arg3, 1, GM_CurrentMap_800AB9B0, arg4, arg5) < 0)
        {
            GV_DestroyActor_800151C8(&work->actor);
            return NULL;
        }

        work->map = GM_CurrentMap_800AB9B0;
    }

    return &work->actor;
}
