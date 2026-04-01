/**
 * Stub implementations for memory card system.
 */
#include <stdio.h>
#include <string.h>
#include "memcard/memcard.h"

void memcard_reset_status(void) {}
int  memcard_check(int port) { (void)port; return 0; }
void memcard_init(void) { printf("mem:"); }
void memcard_exit(void) {}
void memcard_retry(int port) { (void)port; }
MEM_CARD *memcard_get_files(int port) { (void)port; return NULL; }
int  memcard_delete(int port, const char *filename) { (void)port; (void)filename; return 0; }
void memcard_write(int port, const char *filename, int offset, char *buffer, int size) { (void)port; (void)filename; (void)offset; (void)buffer; (void)size; }
void memcard_read(int port, const char *filename, int offset, char *buffer, int size) { (void)port; (void)filename; (void)offset; (void)buffer; (void)size; }
int  memcard_get_status(void) { return 0; }
int  memcard_format(int port) { (void)port; return 0; }
