#ifndef PORT_ISO_READER_H
#define PORT_ISO_READER_H

#include <stdio.h>

/* Minimal ISO-9660 reader for PSX discs. Supports:
 *   - Plain .iso (2048 bytes/sector)
 *   - BIN Mode 1 raw (2352 bytes/sector, user data at +16)
 *   - BIN Mode 2 Form 1 raw (2352 bytes/sector, user data at +24)
 *   - .cue files (parsed to find the referenced BIN)
 *
 * Enough for reading flat data files out of /MGS/ — no CDDA, no
 * multi-track, no XA streaming. */

/* Cache of sibling LBAs in a given directory. Used so that after we locate
 * a file like /MGS/RADIO.DAT, we know the next-higher-LBA file in that
 * directory and can extend RADIO.DAT's effective size up to that boundary.
 * PSX .DAT files with XA-interleaved audio usually report a truncated size
 * in the ISO directory and the game code reads raw sectors past it; without
 * this extension our reader returns EOF where the original hardware would
 * happily keep streaming. */
typedef struct {
    int  lba;
} IsoLbaSlot;

typedef struct IsoImage {
    FILE *fp;
    int   sector_size;   /* 2048 or 2352 */
    int   data_offset;   /* 0, 16 or 24 depending on format */

    /* Sorted list of sibling LBAs used to extend file extents. Filled lazily
     * the first time iso_find_file() is called for a given parent directory. */
    IsoLbaSlot *sibling_lbas;
    int         sibling_count;
    int         sibling_parent_lba;
} IsoImage;

typedef struct IsoFile {
    int  lba;            /* starting sector */
    long size;           /* size in bytes */
} IsoFile;

/* Returns 1 if path looks like a disc image by extension. */
int  iso_path_looks_like_image(const char *path);

/* Open an .iso/.bin/.cue. Detects sector format and validates the PVD.
 * Returns 0 on success, -1 on failure. For .cue it parses the first
 * `FILE "..." BINARY` line and opens that bin relative to the .cue dir. */
int  iso_open(IsoImage *img, const char *path);
void iso_close(IsoImage *img);

/* Find a file by ISO path like "MGS/STAGE.DIR". Returns 0 if found
 * and fills *out; -1 if not found. Case-insensitive. */
int  iso_find_file(IsoImage *img, const char *iso_path, IsoFile *out);

/* Read `size` bytes starting at `byte_offset` within the logical file
 * (0 = first byte of file). Spans sectors transparently. Returns the
 * number of bytes actually read (< size means short read / EOF). */
int  iso_read_file(IsoImage *img, const IsoFile *file,
                   long byte_offset, int size, void *buf);

/* One entry from an iso_list_directory walk. */
typedef struct {
    char name[64];   /* filename with the ";<ver>" suffix already stripped */
    int  is_dir;
    int  lba;
    long size;
} IsoDirEntry;

/* Enumerate the entries of `iso_dir_path` (e.g. "" / "MGS" / "MGS/SAFE").
 * Skips the "." and ".." records. Returns a heap-allocated array of
 * IsoDirEntry; caller frees with `free()`. `*out_count` is the number
 * of entries written. Returns NULL if the directory wasn't found or
 * the image is unreadable; in that case `*out_count` is set to 0. */
IsoDirEntry *iso_list_directory(IsoImage *img, const char *iso_dir_path,
                                int *out_count);

#endif /* PORT_ISO_READER_H */
