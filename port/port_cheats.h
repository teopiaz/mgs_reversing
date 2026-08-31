/*
 * port_cheats.h — debug cheats, driven from the ImGui "Cheats" tab.
 *
 * Same shape as the lighting overrides in libdg/libdg_stub.c: the ImGui tab
 * only flips these ints, and port_cheats_apply() does the actual work once
 * per game tick. That split matters — imgui_render() returns early when the
 * debug window is closed, and ImGui skips the body of an unselected tab, so
 * a cheat applied inline in the tab would silently stop working the moment
 * you closed the window or switched tabs.
 *
 * Nothing here is persisted: cheats always start off (see port_config.h for
 * the settings that *do* survive a restart).
 */
#ifndef PORT_CHEATS_H
#define PORT_CHEATS_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- continuous toggles: re-applied every tick while non-zero ----------- */

/* Sets PLAYER_INVINCIBLE (the game's own god-mode bit, which damage sources
   already check), holds GM_Vitality at GM_VitalityMax, and holds GM_O2 full
   so drowning/gas can't kill you past the health clamp. Scripted instant
   deaths that write GM_Vitality = 0 directly (furnace, rope) are re-clamped
   on the following tick rather than prevented. */
extern int port_cheat_infinite_health;

/* Tops every owned weapon back up to capacity, and refills the current
   magazine — the same two writes the in-game Bandana makes
   (source/equip/bandana.c). Only touches slots already in the inventory
   (>= 0) with a known capacity (max > 0), so it never silently grants a
   weapon you have not picked up. */
extern int port_cheat_infinite_ammo;

/* Forces GM_AlertMode / GM_AlertLevel / GM_AlertMax to 0 every tick.
   GM_AlertLevel is rebuilt from GM_AlertMax by GM_InitNoise each frame and
   GM_AlertModeSet only ever raises the mode, so all three have to be written
   or the alert comes straight back. Guards still see Snake; this clears the
   alert immediately after it is raised, so expect a brief "!" and sting. */
extern int port_cheat_no_alert;

/* --- one-shot requests: set to 1, cleared by port_cheats_apply() -------- */

extern int port_cheat_give_weapons_request; /* every weapon, filled to capacity */
extern int port_cheat_give_items_request;   /* every item slot */
extern int port_cheat_refill_health_request;
extern int port_cheat_clear_alert_request;

/* Run once per game tick, after GV_ExecActorSystem() so the cheats win over
   whatever the actors just computed. Cheap no-op when nothing is enabled,
   and a no-op before a stage is loaded (GM_VitalityMax still 0). */
void port_cheats_apply(void);

/* 1 when any toggle is on — used by the ImGui header badge row. */
int port_cheats_any_active(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CHEATS_H */
