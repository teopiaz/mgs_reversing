#include "sight.h"
#include "libgv/libgv.h"

// used for all first person HUDs

extern int dword_8009F608;

void sight_act_helper_8007111C(void)
{
    int     message_result;
    GV_MSG *message;

    message_result = GV_ReceiveMessage_80016620(0x30da, &message);

    if (message_result > 0)
    {
        while (--message_result >= 0)
        {
            switch (message->message[0])
            {
            case 0xe4e:
                dword_8009F608 &= ~1;
                break;
            case 0xc927:
                dword_8009F608 |= 1;
                break;
            }

            message++;
        }
    }
}

#pragma INCLUDE_ASM("asm/Thing/sight_800711C0.s")            // 352 bytes
#pragma INCLUDE_ASM("asm/Thing/sight_act_helper_80071320.s") // 220 bytes
#pragma INCLUDE_ASM("asm/Thing/sight_act_helper_800713FC.s") // 156 bytes
#pragma INCLUDE_ASM("asm/Thing/sight_act_helper_80071498.s") // 84 bytes
#pragma INCLUDE_ASM("asm/Thing/sight_act_800714EC.s")        // 1244 bytes

extern int dword_8009F600;
extern int dword_8009F604;
extern int dword_8009F608;

void sight_kill_800719C8(Actor_Sight *sight)
{
    if (sight->field_38_menuStructBuffer)
    {
        GV_DelayedFree_80016254(sight->field_38_menuStructBuffer);
    }

    if (sight->field_44_tPageBuffer)
    {
        GV_DelayedFree_80016254(sight->field_44_tPageBuffer);
    }

    if ((0 < dword_8009F600) && (--dword_8009F600 == 0))
    {
        dword_8009F604 = -1;
    }

    dword_8009F608 &= ~1;
}

#pragma INCLUDE_ASM("asm/Thing/sight_loader_80071A54.s") // 648 bytes

extern const char aSightC[];

Actor_Sight *NewSight_80071CDC(int hashedFileName0, int hashedFileName1, short *itemEquippedIndicator, short itemId,
                               short *xyOffsetBuffer)
{
    Actor_Sight *sight = (Actor_Sight *)0x0;

    if (dword_8009F604 != -1 && dword_8009F604 != hashedFileName1)
    {
        return sight;
    }

    sight = (Actor_Sight *)GV_NewActor_800150E4(7, 0x5c);
    if (sight)
    {
        GV_SetNamedActor_8001514C((Actor *)sight, sight_act_800714EC, sight_kill_800719C8, aSightC);
        sight->field_54_maybeFlags = 0;

        if (sight_loader_80071A54(sight, hashedFileName0, itemEquippedIndicator, itemId, xyOffsetBuffer) < 0)
        {
            GV_DestroyActor_800151C8((Actor *)sight);
            return 0;
        }

        dword_8009F600++;
        dword_8009F604 = hashedFileName1;
    }

    return sight;
}

extern short word_8009F5FC;

Actor_Sight *sight_init_80071DC8(int hashedFileName, short *xyOffsetBuffer)
{
    Actor_Sight *sight = (Actor_Sight *)0x0;

    if (dword_8009F604 != -1 && dword_8009F604 != hashedFileName)
    {
        return sight;
    }

    sight = (Actor_Sight *)GV_NewActor_800150E4(7, 0x5c);
    if (sight)
    {
        GV_SetNamedActor_8001514C((Actor *)sight, sight_act_800714EC, sight_kill_800719C8, aSightC);
        sight->field_54_maybeFlags = 0;

        if (sight_loader_80071A54(sight, hashedFileName, &word_8009F5FC, 1, xyOffsetBuffer) < 0)
        {
            GV_DestroyActor_800151C8((Actor *)sight);
            return 0;
        }

        dword_8009F600++;
        dword_8009F604 = hashedFileName;
        word_8009F5FC = 1;
    }

    return sight;
}

#pragma INCLUDE_ASM("asm/Thing/sight_init_80071EA8.s") // 240 bytes
#pragma INCLUDE_ASM("asm/Thing/sight_init_80071F98.s") // 220 bytes

extern short word_8009F5FC;

void sub_80072074()
{
    word_8009F5FC = 0;
}
