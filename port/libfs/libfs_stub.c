/**
 * Stub implementations for libfs (PSX CD-ROM filesystem).
 */
#include <stdio.h>
#include <string.h>
#include "libfs/libfs.h"

void FS_StartDaemon(void) { printf("fs:"); }
void FS_CdStageProgBinFix(void) {}

int  FS_ResetCdFilePosition(void *buffer) { (void)buffer; return 0; }
void FS_CDInit(void) {}
void FS_LoadFileRequest(int fileno, int offset, int size, void *buffer) { (void)fileno; (void)offset; (void)size; (void)buffer; }
int  FS_LoadFileSync(void) { return 0; }
void MakeFullPath(char *name, char *buffer) { (void)name; (void)buffer; }

int  FS_CdMakePositionTable(char *buffer, FS_FILE_INFO *finfo) { (void)buffer; (void)finfo; return 0; }

void FS_CdStageFileInit(void *buffer, int sector) { (void)buffer; (void)sector; }
int  FS_CdGetStageFileTop(char *filename) { (void)filename; return -1; }

void *FS_LoadStageRequest(const char *dirname) { (void)dirname; return NULL; }
int  FS_LoadStageSync(void *info) { (void)info; return 0; }
void FS_LoadStageComplete(void *info) { (void)info; }

void FS_MovieFileInit(void *buffer, int sector) { (void)buffer; (void)sector; }
FS_MOVIE_FILE *FS_GetMovieInfo(unsigned int to_find) { (void)to_find; return NULL; }

void FS_EnableMemfile(int read, int write) { (void)read; (void)write; }
void FS_ClearMemfile(void) {}
int  FS_WriteMemfile(int id, int **buf_ptr, int size) { (void)id; (void)buf_ptr; (void)size; return 0; }
int  FS_ReadMemfile(int id, int **buf_ptr) { (void)id; (void)buf_ptr; return 0; }

void FS_StreamTaskStart(int sector) { (void)sector; }
int  FS_StreamTaskState(void) { return 0; }
void FS_StreamTaskInit(void) {}
int  FS_StreamSync(void) { return 0; }
void FS_StreamCD(void) {}
int  FS_StreamGetTop(int is_demo) { (void)is_demo; return 0; }
int  FS_StreamInit(void *pHeap, int heapSize) { (void)pHeap; (void)heapSize; return 0; }
void FS_StreamStop(void) {}
void FS_StreamOpen(void) {}
void FS_StreamClose(void) {}
int  FS_StreamIsEnd(void) { return 1; }
void *FS_StreamGetData(int target_type) { (void)target_type; return NULL; }
int  FS_StreamGetSize(void *stream) { (void)stream; return 0; }
void FS_StreamUngetData(void *stream) { (void)stream; }
void FS_StreamClear(void *stream) { (void)stream; }
void FS_StreamClearType(void *stream, int target_type) { (void)stream; (void)target_type; }
int  FS_StreamGetEndFlag(void) { return 1; }
int  FS_StreamIsForceStop(void) { return 0; }
void FS_StreamTickStart(void) {}
void FS_StreamSoundMode(void) {}
int  FS_StreamGetTick(void) { return 0; }
