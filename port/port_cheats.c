/*
 * port_cheats.c — implementation of the ImGui "Cheats" tab.
 *
 * Everything here pokes engine globals directly rather than going through
 * the GCL scripting layer, so the interesting part is *when* it runs:
 * port_cheats_apply() is called from game_tick() right after
 * GV_ExecActorSystem(), which means the cheat writes land after the actors
 * have finished recomputing the same values for this frame.
 *
 * The engine headers are not includable from here (32-bit psyq types, and
 * source/game/g_define.h drags in most of the game), so the handful of
 * globals and the two constants we need are declared locally. Each one is
 * annotated with where it really lives so the next person can re-check the
 * types after an upstream rebase — a silent type mismatch on these would
 * corrupt neighbouring globals rather than fail to link.
 */
#include "port_cheats.h"

/* source/include/linkvar.h:150 — the GCL link-variable table. All entries
   are `short`, and 0x8000 is overloaded as IT_TYPE_DISABLED, so a "disabled"
   weapon or item reads back negative. */
extern short linkvarbuf[];

#define GM_Vitality       linkvarbuf[11]
#define GM_VitalityMax    linkvarbuf[12]
#define GM_Weapons        (&linkvarbuf[17])   /* 10 entries, -1 = not owned */
#define GM_WeaponsMax     (&linkvarbuf[27])   /* 10 entries               */
#define GM_Items          (&linkvarbuf[37])   /* 24 entries, -1 = not owned */
#define GM_RationMax      linkvarbuf[61]
#define GM_ColdMedicineMax linkvarbuf[62]
#define GM_TranquilizerMax linkvarbuf[63]

/* NB: linkvar.h also defines GM_ItemsMax as (GM_Items + 24), but only the
   three entries above are real — index 3 onwards is GM_EnvironTemp and then
   the end-of-stage stats block. Never loop GM_ItemsMax to IT_Max. */

/* source/game/g_define.h:251 / :269 */
#define WP_Max            10
#define IT_Max            24
#define IT_Ration         13
#define IT_ColdMedicine   14
#define IT_Diazepam       15
#define IT_Card           17   /* value is the security level, not a count */
#define IT_TimerBomb      18   /* value is a live countdown — never gift it */

/* source/game/gamed.c — player and alert state. */
extern unsigned int GM_PlayerStatus;   /* PlayerStatusFlag enum, gamed.c:86  */
extern int          GM_AlertMode;      /* gamed.c:66, ALERT_* enum           */
extern int          GM_AlertLevel;     /* gamed.c:71, 0..256 meter           */
extern int          GM_AlertMax;       /* gamed.c:59, latched into Level     */
extern short        GM_Magazine;       /* gamed.c:62, current clip           */
extern short        GM_MagazineMax;    /* gamed.c:76                         */
extern short        GM_O2;             /* gamed.c:78, 0..1024                */

/* source/game/g_define.h:20 — the game's own god-mode bit, already honoured
   by the damage paths (dog.c:630, valcan.c:952, hindbody.c:320, ...). */
#define PLAYER_INVINCIBLE 0x00800000u

/* source/game/item.h — clears IT_TYPE_DISABLED from every owned slot. */
extern void enable_equipment(void);

#define O2_FULL 1024

int port_cheat_infinite_health;
int port_cheat_infinite_ammo;
int port_cheat_no_alert;

int port_cheat_give_weapons_request;
int port_cheat_give_items_request;
int port_cheat_refill_health_request;
int port_cheat_clear_alert_request;

/* Remembers whether *we* raised PLAYER_INVINCIBLE, so turning the cheat off
   clears it exactly once instead of fighting the cutscene code that sets the
   same bit legitimately. */
static int invincible_owned;

static void give_all_weapons(void)
{
    int i;

    for (i = 0; i < WP_Max; i++)
    {
        short max = GM_WeaponsMax[i];

        /* The weapon menu lists any slot >= 0, so 0 still means "owned, no
           ammo" — that is the right result for a weapon whose capacity the
           game has not filled in yet.

           Only ever raise: some stages hand out more than the nominal
           capacity (s00a starts the SOCOM at 99 against a max of 50), and
           "give all" should not quietly confiscate that. */
        if (max < 0)
        {
            max = 0;
        }
        if (GM_Weapons[i] < max)
        {
            GM_Weapons[i] = max;
        }
    }

    enable_equipment();
}

static void give_all_items(void)
{
    int i;

    for (i = 0; i < IT_Max; i++)
    {
        short value;

        switch (i)
        {
        case IT_Ration:       value = GM_RationMax;       break;
        case IT_ColdMedicine: value = GM_ColdMedicineMax; break;
        case IT_Diazepam:     value = GM_TranquilizerMax; break;

        /* Card level, not a count. 8 is the highest door class in the game. */
        case IT_Card:         value = 8;                  break;

        /* Skipped on purpose: a non-zero value here is a running fuse, not
           an inventory count. */
        case IT_TimerBomb:    continue;

        default:              value = 1;                  break;
        }

        /* Capacities are zero until the stage script sets them; 1 is enough
           to make the item appear and be usable. */
        if (value <= 0)
        {
            value = 1;
        }

        /* Only ever raise, for the same reason as give_all_weapons(). */
        if (GM_Items[i] < value)
        {
            GM_Items[i] = value;
        }
    }

    enable_equipment();
}

static void clear_alert(void)
{
    /* All three, every time: GM_InitNoise() rebuilds GM_AlertLevel from
       GM_AlertMax each frame (gamed.c:179), and GM_AlertModeSet() only ever
       raises the mode (alert.c:236), so it cannot be used to stand down. */
    GM_AlertMode = 0;  /* ALERT_OFF */
    GM_AlertLevel = 0;
    GM_AlertMax = 0;
}

void port_cheats_apply(void)
{
    int i;

    /* Before the first stage loads, linkvarbuf is all zeroes and writing
       into it would be undone (or would confuse the save/continue path). */
    if (GM_VitalityMax <= 0)
    {
        return;
    }

    /* --- one-shots ----------------------------------------------------- */

    if (port_cheat_give_weapons_request)
    {
        port_cheat_give_weapons_request = 0;
        give_all_weapons();
    }

    if (port_cheat_give_items_request)
    {
        port_cheat_give_items_request = 0;
        give_all_items();
    }

    if (port_cheat_refill_health_request)
    {
        port_cheat_refill_health_request = 0;
        GM_Vitality = GM_VitalityMax;
        GM_O2 = O2_FULL;
    }

    if (port_cheat_clear_alert_request)
    {
        port_cheat_clear_alert_request = 0;
        clear_alert();
    }

    /* --- continuous toggles -------------------------------------------- */

    if (port_cheat_infinite_health)
    {
        GM_PlayerStatus |= PLAYER_INVINCIBLE;
        invincible_owned = 1;

        if (GM_Vitality < GM_VitalityMax)
        {
            GM_Vitality = GM_VitalityMax;
        }
        if (GM_O2 < O2_FULL)
        {
            GM_O2 = O2_FULL;
        }
    }
    else if (invincible_owned)
    {
        /* Falling edge only. Clearing this every frame would also cancel the
           invincibility the game grants itself during cutscenes. */
        invincible_owned = 0;
        GM_PlayerStatus &= ~PLAYER_INVINCIBLE;
    }

    if (port_cheat_infinite_ammo)
    {
        for (i = 0; i < WP_Max; i++)
        {
            /* < 0 covers both "not picked up" (-1) and "disabled during a
               cutscene" (IT_TYPE_DISABLED makes the short negative) — in
               both cases leaving the slot alone is the correct behaviour. */
            if (GM_Weapons[i] >= 0 && GM_WeaponsMax[i] > 0 &&
                GM_Weapons[i] < GM_WeaponsMax[i])
            {
                GM_Weapons[i] = GM_WeaponsMax[i];
            }
        }

        /* The reserve above is not what the gun fires from; without this the
           weapon still clicks empty until you reload. Same pair of writes as
           the in-game Bandana (source/equip/bandana.c:50). */
        if (GM_MagazineMax > 0 && GM_Magazine < GM_MagazineMax)
        {
            GM_Magazine = GM_MagazineMax;
        }
    }

    if (port_cheat_no_alert)
    {
        clear_alert();
    }
}

int port_cheats_any_active(void)
{
    return port_cheat_infinite_health ||
           port_cheat_infinite_ammo ||
           port_cheat_no_alert;
}
