/* Stubs for symbols not provided by compiled source files.
   Most game globals are now defined in their original source files
   (gamed.c, gvd.c, display.c, chanl.c, overlay files, etc.)
   thanks to -fcommon merging tentative definitions. */
#include <stddef.h>

/* GM_PlayerControl needs a valid default so NULL dereferences don't crash.
   The real definition (CONTROL *) comes from gamed.c as a zero/NULL tentative
   def; this strong initializer wins via -fcommon linkage rules. */
static char _default_control[256] = {0};
void *GM_PlayerControl = _default_control;  /* CONTROL * */

/* Variables whose source files are NOT compiled in the port */
int MGS_DiskName = 0;
/* datasave.c does `strcpy(memoryCardFileName, MGS_MemoryCardName)` then
   overwrites positions 12..19 — so this needs to be a real 12-char string,
   not the int=0 the stub used to be. Matches the INTEGRAL build's
   source/main/main.c definition. */
const char *MGS_MemoryCardName = "BISLPM-86247";
int DG_HikituriFlagOld = 0;

/* s12c fog overlay stubs — libdg2.c is excluded from build */
int s12c_800D497C = 0;
int s12c_800D4AB4 = 0;

/* Function stubs — these are actual functions in excluded source files */
void ENE_ExecPutChar_800D9DE8(void) { }
void ENE_PutMark_800D998C(void) { }
int  ENE_SetPutChar_800D9D6C(void *work, int idx) { return 0; }
void s07a_meryl_unk_800D952C(void) { }
int PClseek(int fd, int offset, int mode) { (void)fd; (void)offset; (void)mode; return 0; }

void NewVibrationEditor(void) { }
int  SafetyCheck(int a, int b, int c) { return 0; }
void SetPriority(void) { }


/* Overlay actor stubs — constructors for actors in excluded source files */
void *NewBed_800C70DC(int name, int where, int argc, char **argv) { return NULL; }
void *NewBoxall_800CA088(int name, int where, int argc, char **argv) { return NULL; }
void *NewCountdownGcl(int name, int where, int argc, char **argv) { return NULL; }
void *NewJohnny_800CA838(int name, int where, int argc, char **argv) { return NULL; }
void *NewMovieGCL(int name, int where, int argc, char **argv) { return NULL; }
void *NewNinja_800CC9B4(int name, int where, int argc, char **argv) { return NULL; }
void *NewOtacom_800CC030(int name, int where, int argc, char **argv) { return NULL; }
void *NewRevolver_800C929C(int name, int where, int argc, char **argv) { return NULL; }
void *NewSnake03c1_800CDAEC(int name, int where, int argc, char **argv) { return NULL; }
void *NewSnake03c2_800CDF18(int name, int where, int argc, char **argv) { return NULL; }
void *NewTorture_800C6E1C(int name, int where, int argc, char **argv) { return NULL; }

/* s12c fog overlay — functions from libdg2.c (excluded due to P_TAG issue) */
int FogBoundChanl_800D5500(void *a, int b) { return 0; }
int FogShadeChanl_800D6A04(void *a, int b) { return 0; }
int FogSortChanl_800D4E98(void *a, int b) { return 0; }
int FogTransChanl_800D63B0(void *a, int b) { return 0; }

/* Title overlay open functions — MIPS asm only (#pragma INCLUDE_ASM in open.c) */
void title_open_800CEF54(void *work, int index) { (void)work; (void)index; }
void title_open_800D2AFC(void *work) { (void)work; }
void title_open_800D2CA8(void *work, unsigned long *ot) { (void)work; (void)ot; }

/* Editor branch: additional overlay actors not present on master */
void *NewPrisonNinja(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewPrisonOtacon(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewPrisonSnake(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewPrisonSnake2(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewTorture(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewTortureBed(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewTortureOcelot(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewJohnny(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewAllItemBox(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }
void *NewVrWindow(int name, int where, int argc, char **argv) { (void)name; (void)where; (void)argc; (void)argv; return NULL; }

/* MERYL stubs — same source files excluded as MERYL_*_800D9xxx */
void MERYL_ExecPutChar(void) { }
void MERYL_PutMark(void) { }
int  MERYL_SetPutChar(void *work, int idx) { (void)work; (void)idx; return 0; }

/* s19b jeep — MIPS asm function */
void s19b_jeep_800D2258(void) { }

/* script.c is excluded from the port build (game/script.c) */
int GM_ResetScript(void) { return 0; }

/* s11d overlay chara constructors (real defs live in the dynamically-loaded
   overlay; the main binary's CHARA table needs a stub). */
void *NewHind(int name, int where) { (void)name; (void)where; return NULL; }
void *NewRope(int name, int where) { (void)name; (void)where; return NULL; }

/* Camera debug struct referenced by imgui_debug.cpp but absent on fork/master.
   Provide zeroed storage so the debug UI links (shows zeros for this panel). */
char gUnkCameraStruct_800B77B8[64] = {0};
