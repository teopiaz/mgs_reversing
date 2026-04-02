/**
 * PSX VRAM simulation — 1024x512 16-bit framebuffer.
 * Provides LoadImage/StoreImage/ClearImage and SDL display.
 */

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "libgte.h"
#include "libgpu.h"

/*---------------------------------------------------------------------------*/
/* VRAM                                                                      */
/*---------------------------------------------------------------------------*/

#define VRAM_WIDTH  1024
#define VRAM_HEIGHT 512

uint16_t vram[VRAM_HEIGHT][VRAM_WIDTH];

/* Simple Z-buffer for the framebuffer area (320x224) */
uint16_t port_zbuf[224][320];

/* Current display environment */
static int disp_x = 0, disp_y = 0;
static int disp_w = 320, disp_h = 224;
static int draw_x = 0, draw_y = 0;

/*---------------------------------------------------------------------------*/
/* SDL display                                                               */
/*---------------------------------------------------------------------------*/

static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture  *sdl_texture = NULL;

static int debug_vram_view = 0;

void port_vram_init(SDL_Renderer *renderer)
{
    sdl_renderer = renderer;
    /* Create texture large enough for debug VRAM view (1024x512) */
    sdl_texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING,
        1024, 512);
    memset(vram, 0, sizeof(vram));

    /* Check environment variable for debug view */
    if (getenv("MGS_VRAM_DEBUG"))
        debug_vram_view = 1;
}

void port_vram_toggle_debug(void)
{
    debug_vram_view = !debug_vram_view;
    printf("[port] VRAM debug view: %s\n", debug_vram_view ? "ON" : "OFF");
    fflush(stdout);
}

/* Convert 16-bit PSX pixel (1-bit MSB + 5-5-5 BGR) to match SDL ABGR1555 */
void port_vram_display(void)
{
    if (!sdl_renderer) return;

    if (sdl_texture)
    {
        void *pixels;
        int pitch;
        if (SDL_LockTexture(sdl_texture, NULL, &pixels, &pitch) == 0)
        {
            if (debug_vram_view)
            {
                /* Show entire VRAM (1024x512) */
                for (int y = 0; y < VRAM_HEIGHT; y++)
                    memcpy((uint8_t *)pixels + y * pitch, vram[y], VRAM_WIDTH * 2);
            }
            else
            {
                /* Show just the display region */
                for (int y = 0; y < VRAM_HEIGHT; y++)
                {
                    int vy = disp_y + y;
                    if (vy >= 0 && vy < VRAM_HEIGHT)
                        memcpy((uint8_t *)pixels + y * pitch, &vram[vy][disp_x], VRAM_WIDTH * 2);
                }
            }
            SDL_UnlockTexture(sdl_texture);
        }

        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);

        if (debug_vram_view)
        {
            /* Scale full VRAM to window */
            SDL_Rect src = {0, 0, 1024, 512};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src, NULL);
        }
        else
        {
            SDL_Rect src = {0, 0, 320, 224};
            SDL_RenderCopy(sdl_renderer, sdl_texture, &src, NULL);
        }
    }
    else
    {
        /* Fallback: no texture, just clear */
        SDL_SetRenderDrawColor(sdl_renderer, 32, 0, 32, 255);
        SDL_RenderClear(sdl_renderer);
    }

    /* SDL_RenderPresent is called by main.c after ImGui renders */
}

/*---------------------------------------------------------------------------*/
/* GPU function replacements                                                 */
/*---------------------------------------------------------------------------*/

/* Override the stubs from gpu_stubs.c — these now write to VRAM */

void port_ClearImage(RECT *rect, unsigned char r, unsigned char g, unsigned char b)
{
    uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);

    int x0 = rect->x, y0 = rect->y;
    int x1 = x0 + rect->w, y1 = y0 + rect->h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VRAM_WIDTH) x1 = VRAM_WIDTH;
    /* Protect texture area: only clear framebuffer region (y < 224) */
    if (y1 > 224) y1 = 224;
    if (y0 >= 224) return;

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            vram[y][x] = color;
}

void port_LoadImage(RECT *rect, unsigned long *p)
{
    if (!p || !rect) return;
    int x0 = rect->x, y0 = rect->y;
    int w = rect->w, h = rect->h;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512) return;
    uint16_t *src = (uint16_t *)p;

    for (int y = 0; y < h && (y0 + y) < VRAM_HEIGHT; y++)
        for (int x = 0; x < w && (x0 + x) < VRAM_WIDTH; x++)
            vram[y0 + y][x0 + x] = *src++;
}

void port_StoreImage(RECT *rect, unsigned long *p)
{
    int x0 = rect->x, y0 = rect->y;
    int w = rect->w, h = rect->h;
    uint16_t *dst = (uint16_t *)p;

    for (int y = 0; y < h && (y0 + y) < VRAM_HEIGHT; y++)
        for (int x = 0; x < w && (x0 + x) < VRAM_WIDTH; x++)
            *dst++ = vram[y0 + y][x0 + x];
}

void port_MoveImage(RECT *rect, int dx, int dy)
{
    /* Simple copy within VRAM — doesn't handle overlap correctly */
    int w = rect->w, h = rect->h;
    uint16_t temp[512]; /* max width per line */

    for (int y = 0; y < h; y++)
    {
        int sy = rect->y + y, ddy = dy + y;
        if (sy >= 0 && sy < VRAM_HEIGHT && ddy >= 0 && ddy < VRAM_HEIGHT)
        {
            int copy_w = w;
            if (copy_w > 512) copy_w = 512;
            memcpy(temp, &vram[sy][rect->x], copy_w * 2);
            memcpy(&vram[ddy][dx], temp, copy_w * 2);
        }
    }
}

DRAWENV *port_SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h)
{
    memset(env, 0, sizeof(*env));
    env->clip.x = x; env->clip.y = y; env->clip.w = w; env->clip.h = h;
    draw_x = x; draw_y = y;
    return env;
}

DISPENV *port_SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
    memset(env, 0, sizeof(*env));
    env->disp.x = x; env->disp.y = y; env->disp.w = w; env->disp.h = h;
    disp_x = x; disp_y = y; disp_w = w; disp_h = h;
    return env;
}

/* Draw a filled rectangle in VRAM (for TILE primitives) */
void port_DrawTile(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
    x += draw_x; y += draw_y;

    for (int dy = 0; dy < h; dy++)
    {
        int vy = y + dy;
        if (vy < 0 || vy >= VRAM_HEIGHT) continue;
        for (int dx = 0; dx < w; dx++)
        {
            int vx = x + dx;
            if (vx >= 0 && vx < VRAM_WIDTH)
                vram[vy][vx] = color;
        }
    }
}

/* Current face Z for depth testing */
uint16_t port_current_z = 0;

/* Texture state for textured triangle drawing */
uint16_t port_tex_tpage = 0;
uint16_t port_tex_clut = 0;
int port_tex_enabled = 0;
int port_tex_semi_trans = 0; /* 1 = semi-transparent model */
int port_tex_abr = 0;       /* ABR blend mode (0-3) */
/* Per-triangle UV coords */
int port_tri_u[3], port_tri_v[3];

extern uint16_t sample_vram_texel(uint16_t tpage, uint16_t clut, int u, int v);

/* Draw a textured or flat-colored triangle */

void draw_flat_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    /* Sort vertices by y, keeping UV indices in sync */
    int ui[3] = {0, 1, 2}; /* UV index tracking for sort */
    if (y0 > y1) { int t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; t=ui[0];ui[0]=ui[1];ui[1]=t; }
    if (y0 > y2) { int t; t=x0;x0=x2;x2=t; t=y0;y0=y2;y2=t; t=ui[0];ui[0]=ui[2];ui[2]=t; }
    if (y1 > y2) { int t; t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; t=ui[1];ui[1]=ui[2];ui[2]=t; }

    uint16_t z = port_current_z;
    int textured = port_tex_enabled;

    /* Get sorted UVs (fixed-point 8.8) */
    int su[3], sv[3];
    if (textured) {
        su[0] = port_tri_u[ui[0]]; sv[0] = port_tri_v[ui[0]];
        su[1] = port_tri_u[ui[1]]; sv[1] = port_tri_v[ui[1]];
        su[2] = port_tri_u[ui[2]]; sv[2] = port_tri_v[ui[2]];
    }

    int dy_total = y2 - y0;
    if (dy_total == 0) dy_total = 1;

    for (int y = y0; y <= y2; y++)
    {
        int vy = y + draw_y;
        if (vy < 0 || vy >= 224) continue;

        /* Compute scanline x endpoints */
        int xa, xb;
        int ua, va, ub, vb; /* UV at endpoints */
        int dy0 = y - y0;

        /* Edge a: v0 → v2 (long edge) */
        xa = x0 + (x2 - x0) * dy0 / dy_total;
        if (textured) {
            ua = su[0] + (su[2] - su[0]) * dy0 / dy_total;
            va = sv[0] + (sv[2] - sv[0]) * dy0 / dy_total;
        }

        /* Edge b: v0→v1 or v1→v2 */
        if (y < y1) {
            int dy01 = y1 - y0; if (dy01 == 0) dy01 = 1;
            xb = x0 + (x1 - x0) * dy0 / dy01;
            if (textured) {
                ub = su[0] + (su[1] - su[0]) * dy0 / dy01;
                vb = sv[0] + (sv[1] - sv[0]) * dy0 / dy01;
            }
        } else {
            int dy12 = y2 - y1; if (dy12 == 0) dy12 = 1;
            int dy1 = y - y1;
            xb = x1 + (x2 - x1) * dy1 / dy12;
            if (textured) {
                ub = su[1] + (su[2] - su[1]) * dy1 / dy12;
                vb = sv[1] + (sv[2] - sv[1]) * dy1 / dy12;
            }
        }

        /* Ensure xa <= xb */
        if (xa > xb) {
            int t; t=xa;xa=xb;xb=t;
            if (textured) { t=ua;ua=ub;ub=t; t=va;va=vb;vb=t; }
        }

        int dx = xb - xa;
        if (dx == 0) dx = 1;

        for (int x = xa; x <= xb; x++)
        {
            int vx = x + draw_x;
            if (vx >= 0 && vx < 320 && z <= port_zbuf[vy][vx])
            {
                uint16_t c;
                if (textured) {
                    int frac = (x - xa);
                    int u = ua + (ub - ua) * frac / dx;
                    int v = va + (vb - va) * frac / dx;
                    c = sample_vram_texel(port_tex_tpage, port_tex_clut, u, v);

                    /* PSX transparency:
                       - texel 0x0000 = fully transparent (skip pixel)
                       - STP bit (bit 15) + semi-trans model = alpha blend */
                    if (c == 0) continue; /* fully transparent */

                    if (port_tex_semi_trans && (c & 0x8000)) {
                        uint16_t bg = vram[vy][vx];
                        int br = bg & 0x1F, bg2 = (bg>>5)&0x1F, bb = (bg>>10)&0x1F;
                        int fr = c & 0x1F,  fg = (c>>5)&0x1F,   fb = (c>>10)&0x1F;
                        int rr, rg, rb;
                        switch (port_tex_abr) {
                        case 0: /* 50% blend: B/2 + F/2 */
                            rr=(br+fr)/2; rg=(bg2+fg)/2; rb=(bb+fb)/2; break;
                        case 1: /* additive: B + F */
                            rr=br+fr; rg=bg2+fg; rb=bb+fb; break;
                        case 2: /* subtractive: B - F */
                            rr=br-fr; rg=bg2-fg; rb=bb-fb; break;
                        case 3: /* additive 25%: B + F/4 */
                            rr=br+fr/4; rg=bg2+fg/4; rb=bb+fb/4; break;
                        default: rr=fr; rg=fg; rb=fb; break;
                        }
                        if(rr<0)rr=0; if(rr>31)rr=31;
                        if(rg<0)rg=0; if(rg>31)rg=31;
                        if(rb<0)rb=0; if(rb>31)rb=31;
                        c = (rb<<10)|(rg<<5)|rr;
                    }
                } else {
                    c = color;
                }
                vram[vy][vx] = c;
                port_zbuf[vy][vx] = z;
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* OT traversal — walk the ordering table and render primitives              */
/*---------------------------------------------------------------------------*/

/* Current GPU tpage state (set by DR_TPAGE commands, used by SPRT rendering) */
static uint16_t port_current_tpage = 0;

static int drawot_debug = 0;
void port_DrawOTag(unsigned long *ot)
{
    unsigned long *p = ot;
    int prim_count = 0;
    int node_count = 0;

    while (p && !isendprim(p))
    {
        unsigned long *next = (unsigned long *)nextPrim(p);
        int len = getlen(p);
        node_count++;

        if (node_count > 100000) {
            printf("[ot] ABORT: >100000 nodes, likely cycle\n");
            break;
        }

        if (len > 0)
        {
            unsigned char code = *((unsigned char *)p + 7);  /* command byte */
            unsigned char *data = (unsigned char *)p + 4;    /* after tag */

            if (drawot_debug < 30) {
               // printf("[ot] prim node %d: len=%d code=0x%02X p=%p\n", node_count, len, code, p);
            }

            /* Decode and render based on GPU command code */
            switch (code & 0xFC)  /* mask off semi-trans and texture bits */
            {
            case 0x60: /* TILE */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x = *(short *)(data + 4);
                short y = *(short *)(data + 6);
                short w = *(short *)(data + 8);
                short h = *(short *)(data + 10);
                port_DrawTile(x, y, w, h, r, g, b);
                prim_count++;
                break;
            }
            case 0x68: /* TILE_1 */
            case 0x70: /* TILE_8 */
            case 0x78: /* TILE_16 */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x = *(short *)(data + 4);
                short y = *(short *)(data + 6);
                int w = 1, h = 1;
                if ((code & 0xFC) == 0x70) { w = 8; h = 8; }
                if ((code & 0xFC) == 0x78) { w = 16; h = 16; }
                port_DrawTile(x, y, w, h, r, g, b);
                prim_count++;
                break;
            }
            case 0x20: /* POLY_F3 */
            case 0x28: /* POLY_F4 */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                short x1 = *(short *)(data + 8), y1 = *(short *)(data + 10);
                short x2 = *(short *)(data + 12), y2 = *(short *)(data + 14);
                uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
                draw_flat_tri(x0, y0, x1, y1, x2, y2, color);
                if ((code & 0xFC) == 0x28) /* F4: draw second triangle */
                {
                    short x3 = *(short *)(data + 16), y3 = *(short *)(data + 18);
                    draw_flat_tri(x1, y1, x2, y2, x3, y3, color);
                }
                prim_count++;
                break;
            }
            case 0x30: /* POLY_G3 */
            case 0x38: /* POLY_G4 */
            {
                /* Use first vertex color for now */
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                short x1, y1, x2, y2;
                if ((code & 0xFC) == 0x30) {
                    x1 = *(short *)(data + 12); y1 = *(short *)(data + 14);
                    x2 = *(short *)(data + 20); y2 = *(short *)(data + 22);
                } else {
                    x1 = *(short *)(data + 12); y1 = *(short *)(data + 14);
                    x2 = *(short *)(data + 20); y2 = *(short *)(data + 22);
                }
                uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
                draw_flat_tri(x0, y0, x1, y1, x2, y2, color);
                prim_count++;
                break;
            }
            case 0x24: /* POLY_FT3 */
            case 0x2C: /* POLY_FT4 */
            case 0x34: /* POLY_GT3 */
            case 0x3C: /* POLY_GT4 */
            {
                /* Textured polygons — render as flat color for now */
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                /* The vertex layout differs per type but x0,y0 is always at offset 4,6 */
                uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
                if (color == 0) color = 0x4210; /* dark gray if black */
                /* Just draw a small marker for now */
                port_DrawTile(x0, y0, 4, 4, r ? r : 64, g ? g : 64, b ? b : 64);
                prim_count++;
                break;
            }
            case 0x64: /* SPRT (textured sprite — used by font system) */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x = *(short *)(data + 4);
                short y = *(short *)(data + 6);
                unsigned char u0 = data[8];
                unsigned char v0 = data[9];
                uint16_t clut = *(uint16_t *)(data + 10);
                short w = *(short *)(data + 12);
                short h = *(short *)(data + 14);
                /* Render sprite from VRAM texture */
                for (int sy = 0; sy < h && (y+sy) >= 0 && (y+sy) < 224; sy++) {
                    for (int sx = 0; sx < w && (x+sx) >= 0 && (x+sx) < 320; sx++) {
                        uint16_t c = sample_vram_texel(port_current_tpage, clut, u0+sx, v0+sy);
                        if (c != 0) vram[y+sy][x+sx] = c;
                    }
                }
                prim_count++;
                break;
            }
            case 0x74: /* SPRT_8 */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x = *(short *)(data + 4);
                short y = *(short *)(data + 6);
                port_DrawTile(x, y, 8, 8, r ? r : 128, g ? g : 128, b ? b : 128);
                prim_count++;
                break;
            }
            case 0x7C: /* SPRT_16 */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x = *(short *)(data + 4);
                short y = *(short *)(data + 6);
                port_DrawTile(x, y, 16, 16, r ? r : 128, g ? g : 128, b ? b : 128);
                prim_count++;
                break;
            }
            case 0xE0: /* DR_TPAGE — update current texture page state */
            {
                port_current_tpage = *(uint16_t *)(data + 0);
                prim_count++;
                break;
            }
            case 0xE4: /* DR_AREA */
            case 0xE8: /* DR_OFFSET */
                prim_count++;
                break;
            default:
                break;
            }
        }

        p = next;
        if (prim_count > 10000) break; /* safety limit */
    }
    if (drawot_debug < 3 && node_count > 0) {
        printf("[ot] walked %d nodes, %d prims rendered\n", node_count, prim_count);
    }
    drawot_debug++;
}

/*---------------------------------------------------------------------------*/
/* DrawPrim — render a single GPU primitive immediately                      */
/*---------------------------------------------------------------------------*/

void port_DrawPrim(void *prim)
{
    if (!prim) return;
    u_long *p = (u_long *)prim;
    int len = getlen(p);
    if (len <= 0) return;

    unsigned char code = *((unsigned char *)p + 7);
    unsigned char *data = (unsigned char *)p + 4;

    switch (code & 0xFC)
    {
    case 0x60: /* TILE */
    {
        unsigned char r = data[0], g = data[1], b = data[2];
        short x = *(short *)(data + 4);
        short y = *(short *)(data + 6);
        short w = *(short *)(data + 8);
        short h = *(short *)(data + 10);
        port_DrawTile(x, y, w, h, r, g, b);
        break;
    }
    case 0x68: /* TILE_1 */
    case 0x70: /* TILE_8 */
    case 0x78: /* TILE_16 */
    {
        unsigned char r = data[0], g = data[1], b = data[2];
        short x = *(short *)(data + 4);
        short y = *(short *)(data + 6);
        int w = 1, h = 1;
        if ((code & 0xFC) == 0x70) { w = 8; h = 8; }
        if ((code & 0xFC) == 0x78) { w = 16; h = 16; }
        port_DrawTile(x, y, w, h, r, g, b);
        break;
    }
    case 0x64: /* SPRT (textured sprite) */
    {
        unsigned char r = data[0], g = data[1], b = data[2];
        short x = *(short *)(data + 4);
        short y = *(short *)(data + 6);
        unsigned char u0 = data[8];
        unsigned char v0 = data[9];
        uint16_t clut = *(uint16_t *)(data + 10);
        short w = *(short *)(data + 12);
        short h = *(short *)(data + 14);
        for (int sy = 0; sy < h && (y+sy) >= 0 && (y+sy) < 224; sy++) {
            for (int sx = 0; sx < w && (x+sx) >= 0 && (x+sx) < 320; sx++) {
                uint16_t c = sample_vram_texel(port_current_tpage, clut, u0+sx, v0+sy);
                if (c != 0) vram[y+sy][x+sx] = c;
            }
        }
        break;
    }
    case 0x74: /* SPRT_8 */
    case 0x7C: /* SPRT_16 */
    {
        unsigned char r = data[0], g = data[1], b = data[2];
        short x = *(short *)(data + 4);
        short y = *(short *)(data + 6);
        unsigned char u0 = data[8];
        unsigned char v0 = data[9];
        uint16_t clut = *(uint16_t *)(data + 10);
        int w = ((code & 0xFC) == 0x74) ? 8 : 16;
        int h = w;
        for (int sy = 0; sy < h && (y+sy) >= 0 && (y+sy) < 224; sy++) {
            for (int sx = 0; sx < w && (x+sx) >= 0 && (x+sx) < 320; sx++) {
                uint16_t c = sample_vram_texel(port_current_tpage, clut, u0+sx, v0+sy);
                if (c != 0) vram[y+sy][x+sx] = c;
            }
        }
        break;
    }
    case 0x20: /* POLY_F3 */
    case 0x28: /* POLY_F4 */
    {
        unsigned char r = data[0], g = data[1], b = data[2];
        short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
        short x1 = *(short *)(data + 8), y1 = *(short *)(data + 10);
        short x2 = *(short *)(data + 12), y2 = *(short *)(data + 14);
        uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
        draw_flat_tri(x0, y0, x1, y1, x2, y2, color);
        if ((code & 0xFC) == 0x28) {
            short x3 = *(short *)(data + 16), y3 = *(short *)(data + 18);
            draw_flat_tri(x1, y1, x2, y2, x3, y3, color);
        }
        break;
    }
    case 0xE0: /* DR_TPAGE — update current texture page */
    {
        port_current_tpage = *(uint16_t *)(data + 0);
        break;
    }
    default:
        break;
    }
}
