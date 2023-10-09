#include "Game/game.h"
#include "Game/object.h"
#include "Game/homing_target.h"

typedef struct _WatcherPad
{
    int   field_00;        //0x00       //0xB34
    int   press;           //0x04       //0xB38
    int   field_08;        //0x08       //0xB3C
    int   tmp;             //0x0A       //0xB40
    int   time;            //0x0C       //0xB44
    short dir;             //0x10       //0xB48
    short sound;           //0x12       //0xB4A
} WatcherPad;

typedef struct _WatcherWork
{
    GV_ACT         actor;
    CONTROL        control;                    //0x20
    OBJECT         body;                       //0x9C
    char           field_C0_padding[0xC0];     //0xC0
    int            field_180;                  //0x180
    void          *kmd;                        //0x184
    DG_DEF        *def;                        //0x188
    MOTION_CONTROL field_18C;                  //0x18C
    OAR_RECORD     field_1DC[34];              //0x1DC
    SVECTOR        rots[16];                   //0x6A4
    SVECTOR        field_724;                  //0x724
    int            field_72C;                  //0x72C
    int            field_730;                  //0x730
    short          field_734;                  //0x734
    short          field_736;                  //0x736
    char           field_738[0x6C];            //0x738
    OBJECT         field_7A4;                  //0x7A4
    char           field_7C8_padding[0xC0];    //0x7C8
    MATRIX         field_888;                  //0x888
    int            field_8A8;                  //0x8A8
    int            field_8AC;                  //0x8AC
    int            field_8B0;                  //0x8B0
    int            field_8B4;                  //0x8B4
    int            field_8B8;                  //0x8B8
    int            field_8BC;                  //0x8BC
    int            field_8C0;                  //0x8C0
    int            field_8C4;                  //0x8C4
    int            field_8C8;                  //0x8C8
    int            field_8CC;                  //0x8CC
    int            field_8D0;                  //0x8D0
    int            field_8D4;                  //0x8D4
    int            field_8D8;                  //0x8D8
    int            field_8DC;                  //0x8DC
    short          field_8E0;
    short          field_8E2;
    short          field_8E4;
    short          field_8E6;
    char           field_8E8_padding[0x04];
    void*          action;                     //0x8EC
    void*          field_8F0_func;
    int            time;                       //0x8FC
    int            field_8F8;
    int            actend;                     //0x8FC
    TARGET        *target;                     //0x900
    TARGET         field_904;                  //0x904
    TARGET         field_94C;                  //0x94C
    char           field_994_padding[0x48];    //0x994
    Homing_Target *hom;                        //0x9DC
    short          scale;                      //0x9E0
    short          field_9E2;                  //0x9E2
    short          visible;                    //0x9E4
    short          field_9E6;                  //0x9E6
    int            field_9E8;                  //0x9E8
    SVECTOR        nodes[0x20];                //0x9EC
    int            field_AEC;                  //0xAEC
    GV_ACT*        field_AF0;                  //0xAF0
    int           *field_AF4;                  //0xAF4
    GV_ACT*        field_AF8;                  //0xAF8
    int           *field_AFC;                  //0xAFC
    int            field_B00[8];               //0xB00
    short          think1;                     //0xB20
    short          think2;                     //0xB22
    short          think3;                     //0xB24
    short          think4;                     //0xB26
    unsigned int   count3;                     //0xB28
    int            t_count;                    //0xB2C
    int            l_count;                    //0xB30
    WatcherPad     pad;                        //0xB34
    short          field_B4C;                  //0xB4C
    short          field_B4E;                  //0xB4E
    unsigned int   trigger;                    //0xB50
    GV_ACT*        subweapon;                  //0xB54
    int            field_B58;                  //0xB58
    int            field_B5C;                  //0xB5C
    int            next_node;                  //0xB60    //could be wrong
    int            search_flag;                //0xB64    //could be wrong
    GV_ACT        *field_B68;                  //0xB68
    int            mark_time;                  //0xB6C    //could be wrong
    int            act_status;                 //0xB70
    int            field_B74;                  //0xB74
    signed char    field_B78;                  //0xB78
    signed char    param_blood;                //0xB79  //param.blood (should be struct)
    signed char    param_area;                 //0xB7A  //param.area  (should be struct)
    signed char    field_B7B;                  //0xB7B
    char           field_B7C;                  //0xB7C
    signed char    field_B7D;                  //0xB7D  //used as node index
    char           field_B7E;                  //0xB7E
    char           field_B7F;                  //0xB7F
    char           param_item;                 //0xB80  //param.item  (should be struct)
    char           field_B81;                  //0xB81
    short          param_life;                 //0xB82  //param.life  (should be struct)
    short          param_faint;                //0xB84  //param.faint (should be struct)
    char           local_data;                 //0xB86
    char           local_data2;                //0xB87
    int            field_B88;                  //0xB88
    short          vision_facedir;             //0xB8C  //vision.facedir (should be struct)
    short          field_B8E;
    unsigned short vision_length;              //0xB90
    short          field_B92;
    short          field_B94;
    short          field_B96;
    int            alert_level;                //0xB98
    signed char    modetime[4];                //0xB9C
    signed char    field_BA0;                  //0xBA0
    char           field_BA1;                  //0xBA1
    char           field_BA2;                  //0xBA2
    char           field_BA3;                  //0xBA3
    SVECTOR        field_BA4;                  //0xBA4
    int            field_BAC;                  //0xBAC
    int            field_BB0[8];               //0xBB0
    short          field_BD0[4];               //0xBD0
    SVECTOR        start_pos;                  //0xBD8
    SVECTOR        target_pos;                 //0xBE0
    int            field_BE8;                  //0xBE8
    int            field_BEC;                  //0xBEC
    int            field_BF0;                  //0xBF0
    int            target_addr;                //0xBF4
    int            target_map;                 //0xBF8
    int            field_BFC;                  //0xBFC
    int            field_C00;                  //0xC00
    int            field_C04;                  //0xC04
    int            field_C08;                  //0xC08
    int            field_C0C;                  //0xC0C
    int            field_C10;                  //0xC10
    SVECTOR        field_C14;                  //0xC14
    int            field_C1C;                  //0xC1C
    int            field_C20;                  //0xC20
    int            field_C24;                  //0xC24
    int            sn_dir;                     //0xC28
    short          faseout;                    //0xC2C
    short          field_C2E;                  //0xC2E
    int            field_C30;                  //0xC30
    char           field_C34;                  //0xC34
    char           field_C35;                  //0xC35
    char           field_C36;                  //0xC36
    char           field_C37;                  //0xC37
    int            field_C38;                  //0xC38
    int            field_C3C;                  //0xC3C
    int            field_C40;                  //0xC40
    int            field_C44;                  //0xC44
    short          field_C48;                  //0xC48
    short          field_C4A;                  //0xC4A
} WatcherWork;

typedef struct _CommanderWork
{
    GV_ACT         actor;
    unsigned short unk; //0x20
    unsigned short unk2; //0x22
    int            name; //0x24
} CommanderWork;

typedef struct _TOPCOMMAND_STRUCT {
    int mode;
    int alert;
} TOPCOMMAND_STRUCT;

typedef struct _ENEMY_COMMAND
{
    int     field_0x00;
    int     field_0x04;
    int     field_0x08;
    int     field_0x0C;
    int     field_0x10;
    int     field_0x14;
    int     mode;        ///0x18
    int     field_0x1C;
    int     field_0x20[8];
    int     field_0x40;
    int     field_0x44;
    int     field_0x48;
    int     field_0x4C;
    int     field_0x50;
    short   field_0x54;
    short   field_0x56;
    short   field_0x58;
    short   field_0x5A;
    int     field_0x5C;
    int     field_0x60;
    int     field_0x64;
    int     field_0x68[8];
    SVECTOR field_0x88;
    int     field_0x90;
    int     field_0x94;
    int     field_0x98;
    int     field_0x9C;
    MAP     *map;
    short   where;        //0xA4
    short   field_0xA6;
    short   field_0xA8;
    short   field_0xAA;
    int     field_0xAC;
    int     field_0xB0;
    int     field_0xB4;
    int     field_0xB8;
    int     field_0xBC;
    int     field_0xC0;
    int     field_0xC4;
    VECTOR  field_0xC8[8];
    int     field_0x148[8];
    int     field_0x168;
    int     field_0x16C;
    int     field_0x170; //reset_enemy_num
    int     field_0x174; //reset_enemy_max
    short   field_0x178;
    short   field_0x17A;
    int     field_0x17C;
    short   field_0x180;
    short   field_0x182;
} ENEMY_COMMAND;

extern int                     s00a_dword_800C3328[8];

extern SVECTOR                 ENEMY_TARGET_SIZE_800C35A4;
extern SVECTOR                 ENEMY_TARGET_FORCE_800C35AC;
extern SVECTOR                 ENEMY_ATTACK_SIZE_800C35B4;
extern SVECTOR                 ENEMY_ATTACK_FORCE_800C35BC;
extern SVECTOR                 ENEMY_TOUCH_SIZE_800C35C4;
extern SVECTOR                 ENEMY_TOUCH_FORCE_800C35CC;
extern SVECTOR                 COM_NO_POINT_800C35D4;

extern int                     dword_800E01F4;

extern const char              aCom_noisemode_disD_800E0940[]; // COM_NOISEMODE_DIS =%d \n
extern const char              aEeeDDDTD_800E095C[];           //eee %d %d %d t %d \n

extern int                     s00a_dword_800E0CA0;

extern int                     s00a_dword_800E0D2C;
extern int                     s00a_dword_800E0D30;

extern int                     COM_SHOOTRANGE_800E0D88; 
extern int                     COM_EYE_LENGTH_800E0D8C;
extern int                     COM_PlayerAddress_800E0D90;
extern unsigned int            ENE_SPECIAL_FLAG_800E0D94;


extern ENEMY_COMMAND           EnemyCommand_800E0D98;

//extern int                   EC_MODE_800E0DB0; // EC_MODE, direct access to EnemyCommand struct member
extern SVECTOR                 s00a_dword_800E0E3C;

extern int                     COM_PlayerMap_800E0F1C;
extern TOPCOMMAND_STRUCT       TOPCOMMAND_800E0F20;
extern TOPCOMMAND_STRUCT       s00a_dword_800E0F28;
extern SVECTOR                 COM_PlayerPosition_800E0F30;
extern int                     COM_NOISEMODE_DIS_800E0F38;
extern unsigned int            COM_GameStatus_800E0F3C;
extern short                   COM_PlayerAddressOne_800E0F40[];

extern int                     COM_ALERT_DECREMENT_800E0F60;
extern int                     GM_GameFlag_800E0F64;
extern int                     COM_VibTime_800E0F68;

//command.c
#define TOP_COMM_TRAVEL 0
#define TOP_COMM_ALERT  1

extern char  s00a_command_800CEA2C( WatcherWork *work );
extern void  s00a_command_800CEC90( void ) ;
extern void  s00a_command_800CECF4( void ) ;
extern short s00a_command_800CEDE8( int ops, void *val, int where );
extern short s00a_command_800CED88( int ops, SVECTOR* svec );
extern void  s00a_command_800CFA94( CommanderWork* work ) ;
extern void  s00a_command_800CFEA8( void ) ;
extern int   s00a_command_800D0128( int ops );
extern void  s00a_command_800D018C( CommanderWork* work ) ;
extern void  s00a_command_800D0218( void ) ;
extern void  s00a_command_800D0344( void ) ;
extern void  s00a_command_800CA0E8( WatcherWork* work );
extern void  s00a_command_800CA07C( WatcherWork* work );

//watcher.c
#define EN_FASEOUT 0x10000000

//put.c
#define PUTBREATH 1
#define BW_MARK 4

extern int ENE_SetPutChar_800C979C( WatcherWork *work, int put ) ;

//action.c
#define COM_ST_DANBOWL 0x2000
#define SP_DANBOWLKERI 0x400000

#define ACTINTERP   4

#define STANDSTILL  0
#define ACTION1     1  //WALKING HOLDING GUN
#define ACTION2     2  //RUNNING HOLDING GUN
#define ACTION3     3  //STANDING AIMING GUN
#define ACTION4     4  //STANDING FIRING GUN
#define ACTION5     5  //SEMI-CROUCHING AIMING GUN
#define ACTION6     6  //SEMI_CROUCHING FIRING GUN
#define GRENADE     7
#define ACTION8     8  //HITTING WITH GUN
#define ACTION9     9  //STANDING LOOKING TO THE RIGHT
#define ACTION10    10 //STANDING LOOKING TO THE LEFT
#define ACTION11    11 //STANDING LOOKING DOWN
#define ACTION12    12 //CROUCHING LOOKING FORWARD
#define ACTION13    13 //CROUCHING LOOKING A BIT TO THE RIGHT
#define ACTION14    14 //LOOKING UP
#define ACTION15    15 //GETTING HIT FROM THE FRONT
#define ACTION16    16 //GETTING HIT FROM BEHIND?
#define DANBOWLKERI 17
#define DANBOWLPOSE 18
#define ACTION19    19 //STRETCHING
#define ACTION20    20 //SLEEPING WHILST STANDING
#define ACTION21    21 //WAKING UP SUDDENLY?
#define ACTION22    22 //TALKING ON RADIO?
#define ACTION23    23 //RUBBING NECK
#define ACTION24    24 //SNEEZING
#define ACTION25    25 //I-POSE Maybe not used on this stage
#define ACTION26    26 //I-POSE Maybe not used on this stage
#define ACTION27    27 //GETTING HELD BY SNAKE
#define ACTION28    28 //PRE-NECK SNAP
#define ACTION29    29 //NECK SNAP AND FALLING TO FLOOR
#define ACTION30    30 //FALLING TO FLOOR
#define ACTION31    31 //GETTING FLIPPED OVER
#define ACTION32    32 //SHEILDING EYES
#define ACTION33    33 //STRUGGLING WHILST BEING HELD?
#define ACTION34    34 //FALLING TO THE FLOOR QUICKLY
#define ACTION35    35 //KNOCKED BACKWARDS
#define ACTION36    36 //KNOCKED FORWARDS
#define ACTION37    37 //NECK SNAP AND FALLING TO FLOOR FORWARDS
#define ACTION38    38 //FALLING TO THE FLOOR AND HAVING SOME SORT OF SEIZURE
#define ACTION39    39 //ON FLOOR ON STOMACH
#define ACTION40    40 //ON FLOOR ON BACK
#define ACTION41    41 //ON FLOOR ON STOMACH FACE DOWN
#define ACTION42    42 //FALLING FACE FIRST AND GETTING UP
#define ACTION43    43 //FALLING BACKWARDS AND GETTING UP
#define ACTION44    44 //BIKKURI GET UP FACE DOWN
#define ACTION45    45 //BIKKURI GET UP BACKWARDS
#define ACTION46    46 //JUST BEEN KNOCKED ONTO THE FLOOR ON FLOOR STOMACH
#define ACTION47    47 //JUST BEEN KNOCKED ONTO THE FLOOR ON BACK
#define ACTION48    48 //BEING DRAGGED BACKWARDS WHILST BEING HELD
#define ACTION49    49 //SAME ID AS 40
#define ACTION50    50 //SAME ID AS 41
#define ACTION51    51 //SAME ID AS 42

extern short    ActTable_800C3358[];

typedef	void    ( *ACTION )( WatcherWork *, int ) ;

static inline void SetModeFields( WatcherWork *work, ACTION action )
{
    work->action = action;
    work->time = 0;
    work->control.field_4C_turn_vec.vz = 0;
    work->control.field_4C_turn_vec.vx = 0;
}

static inline void SetMode( WatcherWork *work, ACTION action )
{
    work->action = action;
    work->time = 0;
    work->control.field_4C_turn_vec.vz = 0;
    work->control.field_4C_turn_vec.vx = 0;
    GM_ConfigMotionAdjust_80035008( &( work->body ), 0 );
}

static inline void SetMode2( WatcherWork *work, void *func )
{
    if ( work->field_8F0_func == NULL )
    {
        work->field_8F0_func = func;
        work->field_8F8 = 0;
    }

    work->control.field_4C_turn_vec.vz = 0;
    work->control.field_4C_turn_vec.vx = 0;
    GM_ConfigMotionAdjust_80035008( &( work->body ), 0 );
}

static inline void UnsetMode2( WatcherWork *work )
{
    work->field_8E2 = 0;
    GM_ConfigObjectOverride_80034D30( &( work->body ), ActTable_800C3358[STANDSTILL], 0, ACTINTERP, 0 );
    
    work->field_8F0_func = 0;
    work->field_8F8 = 0;
    work->field_8E2 = 0;
    work->control.field_4C_turn_vec.vz = 0;
    work->control.field_4C_turn_vec.vx = 0;
    
    if ( work->field_B68 )
    {
        GV_DestroyOtherActor_800151D8( work->field_B68 );
        work->field_B68 = 0;
    }
    
}

static inline void SetAction( WatcherWork *work, int n_action, int interp )
{
    work->field_8E0 = n_action ;
    GM_ConfigObjectAction_80034CD4( &( work->body ), ActTable_800C3358[n_action], 0, interp );
}

static inline void UnsetAction( WatcherWork *work, int n_action )
{
    work->field_8E2 = n_action;
    GM_ConfigObjectOverride_80034D30( &( work->body ), ActTable_800C3358[n_action], 0, ACTINTERP, 0x3FE );
}

static inline void UnsetAction2( WatcherWork *work )
{
    work->field_8E2 = 0;
    GM_ConfigObjectOverride_80034D30( &( work->body ), ActTable_800C3358[STANDSTILL], 0, ACTINTERP, 0 );
    GV_DestroyOtherActor_800151D8( work->subweapon );
}

//think.c ?
#define T_NOISE 0