/**
 * PCX texture loader — extracted from source/libdg/loader.c
 * Decodes PCX images and uploads them to VRAM via LoadImage.
 */

#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "libgv/libgv.h"
#include "libdg/libdg.h"
#include "fmt_tex.h"

extern int GV_Clock;

/*---------------------------------------------------------------------------*/
/* PCX decode helpers (from source/libdg/loader.c)                           */
/*---------------------------------------------------------------------------*/

static unsigned char *DG_PcxRead8Bpp(unsigned char *pcxData, unsigned char *imageData, int imageSize)
{
    unsigned char byte;
    int count;

    while (imageSize > 0)
    {
        byte = *pcxData++;
        if (byte >= 0xC0)
        {
            count = byte & 0x3F;
            byte = *pcxData++;
            imageSize -= count;
            while (count-- > 0)
                *imageData++ = byte;
        }
        else
        {
            *imageData++ = byte;
            imageSize--;
        }
    }
    return pcxData;
}

static unsigned char pcxBuffer[4096]; /* line decode buffer (4 planes) */

static unsigned char *DG_PcxRead4Bpp(unsigned char *pcxData, unsigned char *imageData,
                            int bytesPerLine, int width, int height)
{
    /* PCX 4bpp uses 4 bit planes (R,G,B,A). Each plane is 1 bit per pixel.
       Total decoded bytes per line = 4 * bytesPerLine.
       Combine bits from each plane to form 4-bit palette indices. */
    int i = height;
    while (--i >= 0)
    {
        unsigned char *rp, *gp, *bp, *ap;
        int lineRemaining;

        unsigned char *pos = pcxBuffer;
        lineRemaining = 4 * bytesPerLine;
        do
        {
            unsigned char byte = *pcxData++;
            if (byte < 0xC0)
            {
                --lineRemaining;
                *pos++ = byte;
            }
            else
            {
                int count = byte & 0x3F;
                unsigned char color = *pcxData++;
                lineRemaining -= count;
                while (--count >= 0)
                    *pos++ = color;
            }
        } while (lineRemaining > 0);

        rp = pcxBuffer;
        gp = rp + bytesPerLine;
        bp = gp + bytesPerLine;
        ap = bp + bytesPerLine;

        for (lineRemaining = width; lineRemaining > 0; lineRemaining -= 4)
        {
            int r = *rp++;
            int g = *gp++;
            int b = *bp++;
            int a = *ap++;
            int shift = 128;
            int shiftEnd = 8 * (lineRemaining < 4);
            do
            {
                unsigned char color = 0;
                if (shift & r) color |= 1;
                if (shift & g) color |= 2;
                if (shift & b) color |= 4;
                if (shift & a) color |= 8;
                shift >>= 1;

                if (shift & r) color |= 0x10;
                if (shift & g) color |= 0x20;
                if (shift & b) color |= 0x40;
                if (shift & a) color |= 0x80;

                *imageData++ = color;
                shift >>= 1;
            } while (shift != shiftEnd);
        }
    }
    return pcxData;
}

static void DG_PcxReadPalette(unsigned char *pcxPalette, unsigned char *imageData, int width)
{
    int i;
    unsigned short *out = (unsigned short *)imageData;

    for (i = 0; i < width; i++)
    {
        unsigned char r = pcxPalette[0];
        unsigned char g = pcxPalette[1];
        unsigned char b = pcxPalette[2];
        /* Convert RGB888 to PSX 16-bit (1555 BGR). Match the original
           DG_PcxReadPalette (source/libdg/loader.c): set the STP/opaque bit
           (15) for any non-pure-black colour so dark colours that truncate to
           15-bit zero stay opaque instead of decoding to 0x0000 (transparent).
           Pure black stays 0x0000 (genuinely transparent). */
        unsigned short color = !!((r | g | b) & 7) << 5;
        if (r || g || b)
        {
            color |= b >> 3;
            color <<= 5;
            color |= g >> 3;
            color <<= 5;
            color |= r >> 3;
        }
        *out++ = color;
        pcxPalette += 3;
    }
}

/*---------------------------------------------------------------------------*/
/* DG_LoadInitPcx — replaces the stub                                        */
/*---------------------------------------------------------------------------*/

int DG_LoadInitPcx(void *buf, int id)
{
    PCXDATA       *pcx;
    unsigned short flags;
    unsigned char *bytes = (unsigned char *)buf;

    /* Check if this is actually a valid PCX file (manufacturer = 0x0A) */
    if (bytes[0] != 0x0A)
    {
        /* Not a PCX file — might be raw texture data (palette).
           Upload it directly to VRAM at a default location. */
        printf("    [pcx] Not a PCX file (first byte=0x%02X), trying raw upload\n", bytes[0]);

        /* Assume it's a 16-bit color palette, upload to VRAM palette area */
        RECT r = {0, 481, 256, 1};  /* palette row at bottom of VRAM */
        LoadImage(&r, (u_long *)buf);
        return 1;
    }
    int            min_x, min_y;
    int            width, height;
    DG_IMAGE      *images;

    pcx = (PCXDATA *)buf;
    flags = pcx->info.flag;

    min_x = pcx->min_x - 1;
    min_y = pcx->min_y - 1;
    width = pcx->max_x - min_x;
    height = pcx->max_y - min_y;

    if (!(flags & 1))
    {
        width /= 2;
    }

    int alloc_size = width * height + sizeof(DG_IMAGE) * 2;
    images = (DG_IMAGE *)calloc(1, alloc_size);
    if (images)
    {
        DG_IMAGE      *imageB;
        DG_IMAGE      *imageA;
        unsigned char *palette;

        imageB = images;
        imageB->dim.x = pcx->info.cx;
        imageB->dim.y = pcx->info.cy;
        imageB->dim.w = pcx->info.n_colors;
        imageB->dim.h = 1;

        imageA = images + 1;
        imageA->dim.x = pcx->info.px;
        imageA->dim.y = pcx->info.py;
        imageA->dim.w = width / 2;
        imageA->dim.h = height;

        if (flags & 1)
        {
            palette = DG_PcxRead8Bpp(pcx->data, imageA->data, width * height) + 1;
        }
        else
        {
            DG_PcxRead4Bpp(pcx->data, imageA->data, pcx->bytes_per_line, width, height);
            palette = &pcx->header_palette[0];
        }

        DG_PcxReadPalette(palette, imageB->data, imageB->dim.w);

        /* PCX loaded silently */

        LoadImage(&imageB->dim, (u_long *)imageB->data);
        LoadImage(&imageA->dim, (u_long *)imageA->data);

        if (id)
        {
            DG_SetTexture((unsigned short)id, flags & 1, (flags & 0x30) >> 4,
                          &imageA->dim, &imageB->dim, imageB->dim.w);
        }
        free(images); /* free AFTER DG_SetTexture reads the RECTs */
        return 1;
    }

    printf("    [pcx] Failed to allocate %d bytes for texture\n", alloc_size);
    return 0;
}
