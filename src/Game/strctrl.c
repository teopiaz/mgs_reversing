#include "strctrl.h"
#include "mts/mts_new.h"
#include "data/data/data.h"

extern Actor_strctrl strctrl_800B82B0;
extern int GM_GameStatus_800AB3CC;
extern const char aVoxstreamD[];
extern const char aGmStreamplayst[];

extern int str_sector_8009E280;
extern int str_gcl_proc_8009E284;
extern int str_8009E288;

int FS_StreamGetTop_80023F94(int is_movie);
Actor_strctrl* strctrl_init_80037B64(int sector, int gcl_proc, int a3);
void srand_8008E6E8(int s);

#pragma INCLUDE_ASM("asm/Game/strctrl_act_helper_800377EC.s")
#pragma INCLUDE_ASM("asm/Game/strctrl_act_80037820.s")


void strctrl_kill_80037AE4(Actor_strctrl *pActor)
{
    int gmStatus; // $v0
    int field_38_proc; // $a1

    field_38_proc = pActor->field_38_proc;
    pActor->field_20_state = 0;
    GM_GameStatus_800AB3CC &= ~0x20u;
    if ( field_38_proc >= 0 )
    {
        pActor->field_38_proc = -1;
        GCL_ExecProc_8001FF2C( field_38_proc, 0);
        
    }
    if ( str_sector_8009E280 )
    {
        strctrl_init_80037B64(str_sector_8009E280, str_gcl_proc_8009E284, str_8009E288);
        str_sector_8009E280 = 0;
    }
}


#pragma INCLUDE_ASM("asm/Game/strctrl_init_80037B64.s")
#pragma INCLUDE_ASM("asm/Game/GM_StreamStatus_80037CD8.s")
#pragma INCLUDE_ASM("asm/Game/GM_StreamPlayStart_80037D1C.s")

void GM_StreamPlayStop_80037D64()
{
    mts_printf_8008BBA0(aGmStreamplayst);
    FS_StreamStop_80024028();

    // TODO: Probably a switch
    if ( (unsigned int)(unsigned short)strctrl_800B82B0.field_20_state - 1 < 2 )
    {
        GV_DestroyOtherActor_800151D8(&strctrl_800B82B0.field_0_actor);
    }
}

void sub_80037DB8(void)
{
    strctrl_800B82B0.field_38_proc = -1;
}

int GM_StreamGetLastCode_80037DC8(void)
{
    return strctrl_800B82B0.field_30_voxStream;
}

Actor_strctrl* GCL_Command_demo_helper_80037DD8(int base_sector, int gcl_proc)
{
    int total_sector; // $s0

    strctrl_800B82B0.field_30_voxStream = base_sector;
    GM_GameStatus_800AB3CC |= 0x20u;
    total_sector = base_sector + FS_StreamGetTop_80023F94(1);
    do {} while (0);
    srand_8008E6E8(1);
    return strctrl_init_80037B64(total_sector, gcl_proc, 2);
}

#pragma INCLUDE_ASM("asm/Game/GM_VoxStream_80037E40.s")

Actor_strctrl* sub_80037EE0(int voxStream, int gclProc)
{
    int pTop; // $v0

    strctrl_800B82B0.field_30_voxStream = voxStream;
    pTop = FS_StreamGetTop_80023F94(0);
    return strctrl_init_80037B64(voxStream + pTop, gclProc, 1);
}
