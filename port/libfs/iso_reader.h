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

typedef struct IsoImage {
    FILE *fp;
    int   sector_size;   /* 2048 or 2352 */
    int   data_offset;   /* 0, 16 or 24 depending on format */
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

#endif /* PORT_ISO_READER_H */
