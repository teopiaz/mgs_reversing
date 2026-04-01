/**
 * Stub implementations for the sound system (PSX SPU).
 */
#include <stdio.h>

/* sd_cli.c */
int sd_task_active(void) { return 1; } /* return 1 so Main() doesn't loop forever */
int sd_str_play(void) { return 0; }
int sd_sng_play(void) { return 0; }
int sd_se_play(void) { return 0; }
int sd_se_play2(void) { return 0; }
int sd_set_cli(int sound_code, int sync_mode) { (void)sound_code; (void)sync_mode; return 0; }
void sd_set_path(const char *str) { (void)str; }
int get_sng_code(void) { return 0; }
unsigned char *get_sd_buf(int size) { (void)size; return 0; }
void start_xa_sd(void) {}
void stop_xa_sd(void) {}
int SePlay(int sound_code) { (void)sound_code; return 0; }
int get_str_counter(void) { return 0; }

/* sd_main.c */
void SdMain(void) { printf("sound:"); }

/* sd_file.c */
unsigned char *SD_SngDataLoadInit(unsigned short id) { (void)id; return 0; }
void SD_80083ED4(void) {}
unsigned char *SD_SeDataLoadInit(unsigned short id) { (void)id; return 0; }
char *SD_WavDataLoadInit(unsigned short id) { (void)id; return 0; }
char *SD_WavLoadBuf(char *arg0) { (void)arg0; return 0; }
void SD_WavUnload(void) {}
