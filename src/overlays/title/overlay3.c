#include "linker.h"

const char title_aGameleveld_800D8848[] = "\n Game Level = %d\n\n";
const char title_aErrormemcardcheckerror_800D885C[] = "ERROR!!!! MEMCARD Check ERROR!!!\n";
const char title_aSppre_800D8880[] = "sp_pre";
const char title_aSpexit_800D8888[] = "sp_exit";
const char title_aSpalbum_800D8890[] = "sp_album";
const char title_aSpdemo_800D889C[] = "sp_demo";
const char title_aSpon_800D88A4[] = "sp_on";
const char title_aSpoffw_800D88AC[] = "sp_off_w";
const char title_aSponw_800D88B8[] = "sp_on_w";
const char title_aSpoff_800D88C0[] = "sp_off";
const char title_aDsppmode_800D88C8[] = "d3_sp_1p_mode";
const int title_dword_800D88D8 = 0x800C6398;
const int title_dword_800D88DC = 0x800C63C0;
const int title_dword_800D88E0 = 0x800CCDA0;
const int title_dword_800D88E4 = 0x800CCDA0;
const int title_dword_800D88E8 = 0x800C63E8;
const int title_dword_800D88EC = 0x800C6414;
const int title_dword_800D88F0 = 0x800C6440;
const int title_dword_800D88F4 = 0x800C6C50;
const int title_dword_800D88F8 = 0x800C805C;
const int title_dword_800D88FC = 0x800C8104;
const int title_dword_800D8900 = 0x800C8110;
const int title_dword_800D8904 = 0x800C812C;
const int title_dword_800D8908 = 0x800C8148;
const int title_dword_800D890C = 0x800C8164;
const int title_dword_800D8910 = 0x800CCCE4;
const int title_dword_800D8914 = 0x800CCD10;
const int title_dword_800D8918 = 0x800C8104;
const int title_dword_800D891C = 0x800CCDA0;
const int title_dword_800D8920 = 0x800C7A5C;
const int title_dword_800D8924 = 0x800C7A5C;
const int title_dword_800D8928 = 0x800C7A0C;
const int title_dword_800D892C = 0x800C760C;
const int title_dword_800D8930 = 0x800C6484;
const int title_dword_800D8934 = 0x800C65CC;
const int title_dword_800D8938 = 0x800C6714;
const int title_dword_800D893C = 0x800C6868;
const int title_dword_800D8940 = 0x800C69B0;
const int title_dword_800D8944 = 0x800C6B00;
const int title_dword_800D8948 = 0x800C6C80;
const int title_dword_800D894C = 0x800C6E48;
const int title_dword_800D8950 = 0x800C6FB4;
const int title_dword_800D8954 = 0x800C7128;
const int title_dword_800D8958 = 0x800C72AC;
const int title_dword_800D895C = 0x800C7440;
const int title_dword_800D8960 = 0x800C8194;
const int title_dword_800D8964 = 0x800C88F4;
const int title_dword_800D8968 = 0x800C911C;
const int title_dword_800D896C = 0x800C9C98;
const int title_dword_800D8970 = 0x800CA8A4;
const int title_dword_800D8974 = 0x800CB0B8;
const int title_dword_800D8978 = 0x800CB99C;
const int title_dword_800D897C = 0x800CC2B8;
const int title_dword_800D8980 = 0x800C8924;
const int title_dword_800D8984 = 0x800C8D80;
const int title_dword_800D8988 = 0x800C8D88;
const int title_dword_800D898C = 0x800C906C;
const int title_dword_800D8990 = 0x800C906C;
const int title_dword_800D8994 = 0x800C906C;
const int title_dword_800D8998 = 0x800C906C;
const int title_dword_800D899C = 0x800C8DE0;
const int title_dword_800D89A0 = 0x800C8E34;
const char title_dword_800D89A4[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D89A8 = 0x800C914C;
const int title_dword_800D89AC = 0x800C95C8;
const int title_dword_800D89B0 = 0x800C9AE4;
const int title_dword_800D89B4 = 0x800C95D0;
const int title_dword_800D89B8 = 0x800C9AE4;
const int title_dword_800D89BC = 0x800C9628;
const int title_dword_800D89C0 = 0x800C9AE4;
const int title_dword_800D89C4 = 0x800C9854;
const int title_dword_800D89C8 = 0x800C98AC;
const char title_dword_800D89CC[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D89D0 = 0x800C9CC8;
const int title_dword_800D89D4 = 0x800CA1A0;
const int title_dword_800D89D8 = 0x800CA1C0;
const int title_dword_800D89DC = 0x800CA1FC;
const int title_dword_800D89E0 = 0x800CA678;
const int title_dword_800D89E4 = 0x800CA240;
const int title_dword_800D89E8 = 0x800CA678;
const int title_dword_800D89EC = 0x800CA3EC;
const int title_dword_800D89F0 = 0x800CA440;
const char title_dword_800D89F4[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D89F8 = 0x800CA8D4;
const int title_dword_800D89FC = 0x800CAD4C;
const int title_dword_800D8A00 = 0x800CB038;
const int title_dword_800D8A04 = 0x800CB038;
const int title_dword_800D8A08 = 0x800CB038;
const int title_dword_800D8A0C = 0x800CB038;
const int title_dword_800D8A10 = 0x800CAD54;
const int title_dword_800D8A14 = 0x800CADAC;
const int title_dword_800D8A18 = 0x800CAE00;
const char title_dword_800D8A1C[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8A20 = 0x800CB0E8;
const int title_dword_800D8A24 = 0x800CB5BC;
const int title_dword_800D8A28 = 0x800CB5DC;
const int title_dword_800D8A2C = 0x800CB8E8;
const int title_dword_800D8A30 = 0x800CB8E8;
const int title_dword_800D8A34 = 0x800CB8E8;
const int title_dword_800D8A38 = 0x800CB618;
const int title_dword_800D8A3C = 0x800CB65C;
const int title_dword_800D8A40 = 0x800CB6B0;
const char title_dword_800D8A44[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8A48 = 0x800CB9CC;
const int title_dword_800D8A4C = 0x800CBEC0;
const int title_dword_800D8A50 = 0x800CC1EC;
const int title_dword_800D8A54 = 0x800CBEE0;
const int title_dword_800D8A58 = 0x800CC1EC;
const int title_dword_800D8A5C = 0x800CC1EC;
const int title_dword_800D8A60 = 0x800CBF1C;
const int title_dword_800D8A64 = 0x800CBF60;
const int title_dword_800D8A68 = 0x800CBFB4;
const char title_dword_800D8A6C[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8A70 = 0x800CC2E8;
const int title_dword_800D8A74 = 0x800CC838;
const int title_dword_800D8A78 = 0x800CC858;
const int title_dword_800D8A7C = 0x800CC8A8;
const int title_dword_800D8A80 = 0x800CCBA0;
const int title_dword_800D8A84 = 0x800CCBA0;
const int title_dword_800D8A88 = 0x800CC8D0;
const int title_dword_800D8A8C = 0x800CC914;
const int title_dword_800D8A90 = 0x800CC968;
const char title_dword_800D8A94[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8A98 = 0x800CCE20;
const int title_dword_800D8A9C = 0x800CCFB0;
const int title_dword_800D8AA0 = 0x800CCE58;
const int title_dword_800D8AA4 = 0x800CCEC0;
const int title_dword_800D8AA8 = 0x800CCF5C;
const int title_dword_800D8AAC = 0x800CCFB0;
const char title_aOpbackr_800D8AB0[] = "op_back_r";
const char title_aOpbackl_800D8ABC[] = "op_back_l";
const int title_dword_800D8AC8 = 0x800CD850;
const int title_dword_800D8ACC = 0x800CD86C;
const int title_dword_800D8AD0 = 0x800CD8CC;
const int title_dword_800D8AD4 = 0x800CD928;
const int title_dword_800D8AD8 = 0x800CD994;
const int title_dword_800D8ADC = 0x800CDA08;
const int title_dword_800D8AE0 = 0x800CDA68;
const char title_dword_800D8AE4[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8AE8 = 0x800CE3B8;
const int title_dword_800D8AEC = 0x800CE3D8;
const int title_dword_800D8AF0 = 0x800CE408;
const int title_dword_800D8AF4 = 0x800CE440;
const int title_dword_800D8AF8 = 0x800CE4A0;
const char title_dword_800D8AFC[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8B00 = 0x800CEB74;
const int title_dword_800D8B04 = 0x800CEC14;
const int title_dword_800D8B08 = 0x800CEC68;
const int title_dword_800D8B0C = 0x800CED18;
const int title_dword_800D8B10 = 0x800CEDC8;
const int title_dword_800D8B14 = 0x800CEE20;
const int title_dword_800D8B18 = 0x800CF808;
const int title_dword_800D8B1C = 0x800CFDE4;
const int title_dword_800D8B20 = 0x800D03C0;
const int title_dword_800D8B24 = 0x800D09A8;
const int title_dword_800D8B28 = 0x800D0F80;
const int title_dword_800D8B2C = 0x800D1550;
const char title_aClearflagd_800D8B30[] = "clear flag %d\n";
const char title_aCleardataexistss_800D8B40[] = "clear data exists %s\n";
const char title_aBislpm_800D8B58[] = "BISLPM-86111";
const char title_aOldclearflagd_800D8B68[] = "old clear flag %d\n";
const char title_aOldcleardataexistss_800D8B7C[] = "old clear data exists %s\n";
const char title_aGamedatafind_800D8B98[] = "gamedata find!\n";
const char title_aFlagx_800D8BA8[] = "flag = %x\n";
const char title_aFlagnewx_800D8BB4[] = "flag_new = %x\n";
const char title_aThisisrank_800D8BC4[] = "This Is Rank 0\n";
const char title_aThisisrank_800D8BD4[] = "This Is Rank 1\n";
const char title_aThisisrank_800D8BE4[] = "This Is Rank 2\n";
const char title_aThisisrank_800D8BF4[] = "This Is Rank 3\n";
const char title_aThisisrank_800D8C04[] = "This Is Rank 4\n";
const char title_aThisisrank_800D8C14[] = "This Is Rank 5\n";
const char title_aThisisrank_800D8C24[] = "This Is Rank 6\n";
const char title_aPhotodatafind_800D8C34[] = "photodata find!\n";
const char title_aVrfind_800D8C48[] = "VR find!\n";
const char title_aTitlememcardcheckreult_800D8C54[] = "-- Title MemCard Check Reult! --\n";
const char title_aSaveflagd_800D8C78[] = "save_flag = %d\n";
const char title_aPhotoflagd_800D8C88[] = "photo_flag = %d\n";
const char title_aVrflagd_800D8C9C[] = "vr_flag = %d\n";
const char title_aSperankd_800D8CAC[] = "spe_rank = %d\n";
const char title_aDemorankd_800D8CBC[] = "demo_rank = %d\n";
const char title_dword_800D8CCC[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8CD0 = 0x800D2408;
const int title_dword_800D8CD4 = 0x800D2408;
const int title_dword_800D8CD8 = 0x800D2408;
const int title_dword_800D8CDC = 0x800D2414;
const int title_dword_800D8CE0 = 0x800D2414;
const int title_dword_800D8CE4 = 0x800D2430;
const int title_dword_800D8CE8 = 0x800D2430;
const int title_dword_800D8CEC = 0x800D2430;
const int title_dword_800D8CF0 = 0x800D2430;
const int title_dword_800D8CF4 = 0x800D244C;
const int title_dword_800D8CF8 = 0x800D243C;
const char title_aCdcaseopen_800D8CFC[] = "CD CASE OPEN!!\n";
const char title_aCdnormalspeedsetfailed_800D8D0C[] = "CD NORMAL SPEED SET FAILED!!\n";
const char title_aCdnormalspeedsetsuccess_800D8D2C[] = "CD NORMAL SPEED SET SUCCESS!!\n";
const char title_a_800D8D4C[] = "???????\n";
const char title_aThisisnotpsdisc_800D8D58[] = "THIS IS NOT PS DISC!!\n";
const char title_aCdstopfailed_800D8D70[] = "CD STOP FAILED!!\n";
const char title_aCdstopsuccess_800D8D84[] = "CD STOP SUCCESS!!\n";
const char title_aCdcaseclose_800D8D98[] = "CD CASE CLOSE!!\n";
const char title_aStartbuttonpush_800D8DAC[] = "START BUTTON PUSH!!\n";
const char title_aCdspinstart_800D8DC4[] = "CD SPIN START!!\n";
const char title_aCdnotspin_800D8DD8[] = "CD NOT SPIN!!\n";
const char title_aCdtocreadnotfinish_800D8DE8[] = "CD TOC READ NOT FINISH!!\n";
const char title_aCdtocreadsuccess_800D8E04[] = "CD TOC READ SUCCESS!!\n";
const char title_aCdspeedinitfailed_800D8E1C[] = "CD SPEED INIT FAILED!!\n";
const char title_aCdspeedinitsuccess_800D8E34[] = "CD SPEED INIT SUCCESS!!\n";
const char title_aCdcheckfailed_800D8E50[] = "CD CHECK FAILED!!\n";
const char title_aCdcheckok_800D8E64[] = "CD CHECK OK!!\n";
const char title_aThisisnotpsdisc_800D8E74[] = "THIS IS NOT PS DISC!!!\n";
const char title_aCdcheckerror_800D8E8C[] = "CD CHECK ERROR!!\n";
const char title_aThisispsdisc_800D8EA0[] = "THIS IS PS DISC!!\n";
const char title_aThisisdisc_800D8EB4[] = "THIS IS DISC 1!!\n";
const char title_aThisisnotdisc_800D8EC8[] = "THIS IS NOT DISC 1!!!\n";
const char title_aOkok_800D8EE0[] = "OK! OK!\n";
const char title_aNotoknotok_800D8EEC[] = "NOT OK! NOT OK!\n";
const int title_dword_800D8F00 = 0x800D24AC;
const int title_dword_800D8F04 = 0x800D254C;
const int title_dword_800D8F08 = 0x800D25F0;
const int title_dword_800D8F0C = 0x800D2620;
const int title_dword_800D8F10 = 0x800D2678;
const int title_dword_800D8F14 = 0x800D26B8;
const int title_dword_800D8F18 = 0x800D2708;
const int title_dword_800D8F1C = 0x800D2754;
const int title_dword_800D8F20 = 0x800D27BC;
const int title_dword_800D8F24 = 0x800D2984;
const int title_dword_800D8F28 = 0x800D29B8;
const char title_dword_800D8F2C[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8F30 = 0x800D2E20;
const int title_dword_800D8F34 = 0x800D2D18;
const int title_dword_800D8F38 = 0x800D2D24;
const int title_dword_800D8F3C = 0x800D2D30;
const int title_dword_800D8F40 = 0x800D2D94;
const int title_dword_800D8F44 = 0x800D2D48;
const int title_dword_800D8F48 = 0x800D2D3C;
const int title_dword_800D8F4C = 0x800D2D88;
const int title_dword_800D8F50 = 0x800D2E20;
const char title_dword_800D8F54[] = {0x0, 0x0, 0x0, 0x0};
const int title_dword_800D8F58 = 0x800D2E20;
const int title_dword_800D8F5C = 0x800D2DDC;
const int title_dword_800D8F60 = 0x800D2DEC;
const int title_dword_800D8F64 = 0x800D2DFC;
const int title_dword_800D8F68 = 0x800D2E0C;
const char title_aVeryeasy_800D8F6C[] = "VERY EASY";
const char title_aEasy_800D8F78[] = "EASY";
const char title_aNormal_800D8F80[] = "NORMAL";
const char title_aHard_800D8F88[] = "HARD";
const char title_aExtreme_800D8F90[] = "EXTREME";
const int title_dword_800D8F98 = 0x800D2ED8;
const int title_dword_800D8F9C = 0x800D3024;
const int title_dword_800D8FA0 = 0x800D3170;
const int title_dword_800D8FA4 = 0x800D32B4;
const int title_dword_800D8FA8 = 0x800D33C4;
const char title_aMemorycardslot_800D8FAC[] = "MEMORY CARD SLOT 1  :  ";
const char title_aFull_800D8FC4[] = "   FULL";
const char title_aNocard_800D8FCC[] = "NO CARD";
const char title_aMemorycardslot_800D8FD4[] = "MEMORY CARD SLOT 2  :  ";
const char title_aDoyouwanttocontinue_800D8FEC[] = "DO YOU WANT TO CONTINUE\n";
const char title_aYourgamelikethis_800D9008[] = "YOUR GAME LIKE THIS?";
const char title_aYes_800D9020[] = "YES";
const char title_aNo_800D9024[] = "NO";
const int title_dword_800D9028 = 0x800D3FC0;
const int title_dword_800D902C = 0x800D400C;
const int title_dword_800D9030 = 0x800D404C;
const int title_dword_800D9034 = 0x800D4058;
const int title_dword_800D9038 = 0x800D3878;
const int title_dword_800D903C = 0x800D3990;
const int title_dword_800D9040 = 0x800D3B10;
const int title_dword_800D9044 = 0x800D3C80;
const int title_dword_800D9048 = 0x800D4078;
const int title_dword_800D904C = 0x800D3EE0;
const int title_dword_800D9050 = 0x800D4078;
const int title_dword_800D9054 = 0x800D4078;
const int title_dword_800D9058 = 0x800D4078;
const int title_dword_800D905C = 0x800D3F78;
const int title_dword_800D9060 = 0x800D4078;
const int title_dword_800D9064 = 0x800D4078;
const int title_dword_800D9068 = 0x800D3F2C;
const int title_dword_800D906C = 0x800D4078;
const int title_dword_800D9070 = 0x800D3F90;
const int title_dword_800D9074 = 0x800D3F90;
const int title_dword_800D9078 = 0x800D4078;
const int title_dword_800D907C = 0x800D3FA8;
const int title_dword_800D9080 = 0x800D3D30;
const int title_dword_800D9084 = 0x800D3D48;
const int title_dword_800D9088 = 0x800D3D60;
const int title_dword_800D908C = 0x800D3D80;
const int title_dword_800D9090 = 0x800D3D94;
const int title_dword_800D9094 = 0x800D3DB4;
const int title_dword_800D9098 = 0x800D3E34;
const int title_dword_800D909C = 0x800D3E40;
const int title_dword_800D90A0 = 0x800D3E4C;
const int title_dword_800D90A4 = 0x800D3E58;
const int title_dword_800D90A8 = 0x800D3E64;
const int title_dword_800D90AC = 0x800D3E70;
const char title_aOptitle_800D90B0[] = "op_title";
const char title_aOpcopy_800D90BC[] = "op_copy";
const char title_aOpsnakecut_800D90C4[] = "op_snake_cut";
const char title_aOpsnakewaku_800D90D4[] = "op_snake_waku";
const char title_aOppstart_800D90E4[] = "op_p_start";
const char title_aOpngame_800D90F0[] = "op_n_game";
const char title_aOplgame_800D90FC[] = "op_l_game";
const char title_aOpoption_800D9108[] = "op_option";
const char title_aOpsellevel_800D9114[] = "op_sellevel";
const char title_aOpwarning_800D9120[] = "op_warning";
const char title_aOpkonamil_800D912C[] = "op_konami_l";
const char title_aOpkonamir_800D9138[] = "op_konami_r";
const char title_aOpkcejl_800D9144[] = "op_kcej_l";
const char title_aOpkcejr_800D9150[] = "op_kcej_r";
const char title_aCurlu_800D915C[] = "cur_lu";
const char title_aCurru_800D9164[] = "cur_ru";
const char title_aCurld_800D916C[] = "cur_ld";
const char title_aCurrd_800D9174[] = "cur_rd";
const char title_aCuru_800D917C[] = "cur_u";
const char title_aCurd_800D9184[] = "cur_d";
const char title_aCurl_800D918C[] = "cur_l";
const char title_aCurr_800D9194[] = "cur_r";
const char title_aCurc_800D919C[] = "cur_c";
const char title_aSpbackl_800D91A4[] = "sp_back_l";
const char title_aSpbackr_800D91B0[] = "sp_back_r";
const char title_aSpspecial_800D91BC[] = "sp_special";
const char title_aSpradar_800D91C8[] = "sp_radar";
const char title_aSpline_800D91D4[] = "sp_line";
const char title_aDemodemo_800D91DC[] = "demo_demo";
const char title_aDemora_800D91E8[] = "demo_ra";
const char title_aDemorb_800D91F0[] = "demo_rb";
const char title_aDemorollc_800D91F8[] = "demo_roll_c";
const char title_aDemorolld_800D9204[] = "demo_roll_d";
const char title_aOpbrf_800D9210[] = "op_brf";
const char title_aOpspecial_800D9218[] = "op_special";
const char title_aOpvr_800D9224[] = "op_vr";
const char title_aStartflagd_800D922C[] = "start flag = %d\n";
const char title_aMemflagd_800D9240[] = "mem flag = %d\n";
const int title_dword_800D9250 = 0x800D5B84;
const int title_dword_800D9254 = 0x800D5B94;
const int title_dword_800D9258 = 0x800D5B9C;
const int title_dword_800D925C = 0x800D67B4;
const int title_dword_800D9260 = 0x800D5BB0;
const int title_dword_800D9264 = 0x800D5E10;
const int title_dword_800D9268 = 0x800D607C;
const int title_dword_800D926C = 0x800D62E0;
const int title_dword_800D9270 = 0x800D654C;
const int title_dword_800D9274 = 0x800D67B4;

const char aOpenC[] = "open.c";

char SECTION("overlay.bss") title_dword_800D92D0[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") vrwindow_800D92D4[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92D8[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92DC[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92E0[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92E4[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92E8[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92EC[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92F0[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92F4[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92F8[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D92FC[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9300[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9304[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9308[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D930C[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9310[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9314[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9318[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D931C[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9320[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9324[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9328[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D932C[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9330[] = {0x0, 0x0, 0x0, 0x0};
char SECTION("overlay.bss") title_dword_800D9334[] = {0x0, 0x0, 0x0, 0x0};
