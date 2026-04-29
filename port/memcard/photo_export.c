/*
 * Host-side photo export: dumps the camera's pre-encoder framebuffer as a PNG
 * alongside the memcard save. The in-game DCT/RLE blob is not a real JPEG
 * (no JFIF / SOI / Huffman), so the PNG is what makes captures viewable
 * outside the game.
 *
 * Output path resolution mirrors the memcard backend:
 *   $PORT_MEMCARD_DIR/../photos/photo_NNN.png   (override)
 *   $HOME/Library/Application Support/MGS/photos/photo_NNN.png  (default)
 *   ./photos/photo_NNN.png                                       (fallback)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

extern void port_StoreImage(void *rect, unsigned long *p);
extern int  gl_renderer_enabled(void);
extern int  gl_renderer_read_psx_region(int psx_x, int psx_y, int psx_w, int psx_h,
                                        unsigned char *out_rgb,
                                        int *out_w, int *out_h);

/* Public API exposed to the game side. */
void port_capture_photo_png(int x, int y, int w, int h);

/* Mirror libgpu RECT layout — short x, short y, short w, short h. */
typedef struct { short x, y, w, h; } port_rect_t;

static int photo_mkdir_p(const char *path)
{
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void resolve_photo_dir(char *out, size_t outsz)
{
    const char *override = getenv("PORT_MEMCARD_DIR");
    const char *home = getenv("HOME");
    if (override && *override)
    {
        /* Place photos as a sibling of the memcard root. */
        snprintf(out, outsz, "%s/../photos", override);
    }
    else if (home && *home)
    {
        snprintf(out, outsz, "%s/Library/Application Support/MGS/photos", home);
    }
    else
    {
        snprintf(out, outsz, "./photos");
    }
}

/* Called from source/equip/jpegcam.c JpegcamTakePhoto state 9, just before
   the lossy in-game encoder runs. Reads the post-render framebuffer
   directly from emulated VRAM (port_StoreImage), unpacks the XBGR1555
   pixels to RGB, and writes a real PNG to the host photo directory. */
void port_capture_photo_png(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;

    char dir[1024];
    resolve_photo_dir(dir, sizeof(dir));
    if (photo_mkdir_p(dir) != 0)
    {
        fprintf(stderr, "photo: cannot create %s: %s\n", dir, strerror(errno));
        return;
    }

    int           out_w = w, out_h = h;
    unsigned char *rgb = NULL;

    /* Prefer the GL framebuffer when GL rendering is active — the actual
       3D scene lives in g_fbo, while emulated VRAM is only kept current
       by the CPU rasterizer (used by the codec / 2D HUD). Fall back to
       VRAM XBGR1555 read if GL is off (software path). */
    if (gl_renderer_enabled())
    {
        int gl_w = w * 8, gl_h = h * 8; /* upper bound at PORT_GL_SCALE max=8 */
        rgb = (unsigned char *)malloc((size_t)gl_w * gl_h * 3);
        if (!rgb) return;
        if (!gl_renderer_read_psx_region(x, y, w, h, rgb, &out_w, &out_h))
        {
            free(rgb);
            rgb = NULL;
        }
    }

    if (!rgb)
    {
        int       npix = w * h;
        uint16_t *src  = (uint16_t *)malloc((size_t)npix * sizeof(uint16_t));
        rgb = (unsigned char *)malloc((size_t)npix * 3);
        if (!src || !rgb) { free(src); free(rgb); return; }

        port_rect_t rect = { (short)x, (short)y, (short)w, (short)h };
        port_StoreImage(&rect, (unsigned long *)src);

        /* PSX XBGR1555: bit 15 mask, 14..10 blue, 9..5 green, 4..0 red. */
        for (int i = 0; i < npix; i++)
        {
            uint16_t p = src[i];
            rgb[i * 3 + 0] = (unsigned char)((p << 3) & 0xF8); /* R */
            rgb[i * 3 + 1] = (unsigned char)((p >> 2) & 0xF8); /* G */
            rgb[i * 3 + 2] = (unsigned char)((p >> 7) & 0xF8); /* B */
        }
        free(src);
        out_w = w;
        out_h = h;
    }

    /* Filename: photo_YYYYMMDD_HHMMSS.png. Stable, sortable, and lets the
       user correlate captures with wall-clock time. */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char path[1280];
    snprintf(path, sizeof(path), "%s/photo_%04d%02d%02d_%02d%02d%02d.png",
             dir,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    if (stbi_write_png(path, out_w, out_h, 3, rgb, out_w * 3))
    {
        printf("photo: wrote %s (%dx%d)\n", path, out_w, out_h);
    }
    else
    {
        fprintf(stderr, "photo: stbi_write_png failed for %s\n", path);
    }

    free(rgb);
}
