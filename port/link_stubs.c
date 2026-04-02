/* Auto-generated stubs — functions as no-ops, data as zero */
#include <stddef.h>

typedef struct { short vx, vy, vz, pad; } STUB_SVECTOR;

/* Data variables — types MUST match declarations in game.h */
int DG_CurrentGroupID = 0;
int GM_AlertLevel = 0;
int GM_AlertMax = 0;
int GM_AlertMode = 0;
int GM_ClaymoreMap = 0;
int GM_DisableItem = 0;
unsigned int GM_DisableWeapon = 0;
short GM_Magazine = 0;
short GM_MagazineMax = 0;
int GM_NoiseLength = 0;
STUB_SVECTOR GM_NoisePosition = {0};
int GM_NoisePower = 0;
short GM_O2 = 0;
int GM_PadVibration = 0;
int GM_PadVibration2 = 0;
STUB_SVECTOR GM_PhotoViewPos = {0};
int GM_Photocode = 0;
int GM_PlayerAction = 0;
int GM_PlayerAddress = 0;
void *GM_PlayerBody = NULL;        /* OBJECT * */
void *GM_PlayerControl = NULL;     /* CONTROL * */
int GM_PlayerMap = 0;
STUB_SVECTOR GM_PlayerPosition = {0};  /* SVECTOR */
short GM_WeaponChanged = 0;
int MGS_DiskName = 0;
int MGS_MemoryCardName = 0;
int N_ChanlPerfMax = 0;
int DG_HikituriFlagOld = 0;
int dword_800AB9D4 = 0;
int dword_800ABA1C = 0;

/* Variables from overlay data (declared as extern int in various source files) */
int ZAKO11F_GameFlag_800D5C4C = 0;
int ZAKOCOM_PlayerAddress_800D5C50 = 0;
int ZAKOCOM_PlayerAddress_800DF3B8 = 0;
int ZAKOCOM_PlayerMap_800D5C54 = 0;
int ZAKOCOM_PlayerMap_800DF3BC = 0;
STUB_SVECTOR ZAKOCOM_PlayerPosition_800D5AF0 = {0};
STUB_SVECTOR ZAKOCOM_PlayerPosition_800DF278 = {0};
int Zako11FCommand_800D5AF8 = 0;
int ZakoCommand_800DF280 = 0;
int TOPCOMMAND_800D5C40 = 0;
int TOPCOMMAND_800DF3A8 = 0;
int s07a_dword_800E3650 = 0;
int s07a_dword_800E3654 = 0;
int s07a_dword_800E3658 = 0;
int s11e_dword_800DF3B0 = 0;
int s11e_dword_800DF3B4 = 0;
int s11i_dword_800D5C48 = 0;
int s12c_800D497C = 0;
int s12c_800D4AB4 = 0;

/* Function stubs — these are actual functions in excluded source files */
void ENE_ExecPutChar_800D9DE8(void) { }
void ENE_PutMark_800D998C(void) { }
int  ENE_SetPutChar_800D9D6C(void *work, int idx) { return 0; }
void HZD_LineNearSurface(void) { }
void s07a_meryl_unk_800D952C(void) { }
int PClseek(int fd, int offset, int mode) { (void)fd; (void)offset; (void)mode; return 0; }
void MENU_SetRadarFunc(void) { }
void MENU_SetRadarScale(void) { }
void NewVibrationEditor(void) { }
int  SafetyCheck(int a, int b, int c) { return 0; }
void SetPriority(void) { }
void menu_radar_init_8003B474(void) { }
void menu_radar_kill_8003B554(void) { }
void menu_radar_load_rpk_8003AD64(void) { }

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
