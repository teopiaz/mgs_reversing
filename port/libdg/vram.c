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

/* Z-buffer for the framebuffer area (320x224) */
uint16_t port_zbuf[224][320];

/* Current display environment */
static int disp_x = 0, disp_y = 0;
static int disp_w = 320, disp_h = 224;
static int draw_x = 0, draw_y = 0;
void port_set_draw_offset(int x, int y) { draw_x = x; draw_y = y; }
/* GPU draw area clipping (set by E3/E4 commands, reset by port_RenderObjects) */
int clip_x0 = 0, clip_y0 = 0, clip_x1 = 319, clip_y1 = 223;
int port_ot_buffer_index = 0;  /* Set by DG_DrawOTag before OT walk */

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
                /* Show the display region — always from (0,0) since
                   the port normalizes draw offsets to buffer 0 */
                for (int y = 0; y < 224 && y < VRAM_HEIGHT; y++)
                    memcpy((uint8_t *)pixels + y * pitch, &vram[y][0], 320 * 2);
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
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
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

void port_LoadImage(RECT *rect, u_long *p)
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

void port_StoreImage(RECT *rect, u_long *p)
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

/* Draw a semi-transparent filled rectangle (PSX blend mode 0: B/2 + F/2) */
static void port_DrawTileSemiTrans(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    x += draw_x; y += draw_y;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1 + 1) x1 = clip_x1 + 1;
    if (y1 > clip_y1 + 1) y1 = clip_y1 + 1;
    if (x1 > 320) x1 = 320;
    if (y1 > 224) y1 = 224;
    int fr = r >> 3, fg = g >> 3, fb = b >> 3;
    for (int vy = y0; vy < y1; vy++) {
        for (int vx = x0; vx < x1; vx++) {
            uint16_t bg = vram[vy][vx];
            int br = (bg & 0x1F), bgr = (bg >> 5) & 0x1F, bb = (bg >> 10) & 0x1F;
            int nr = (br + fr) >> 1;
            int ng = (bgr + fg) >> 1;
            int nb = (bb + fb) >> 1;
            vram[vy][vx] = (uint16_t)(nr | (ng << 5) | (nb << 10));
        }
    }
}

/* Draw a filled rectangle in VRAM (for TILE primitives) */
void port_DrawTile(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
    x += draw_x; y += draw_y;

    /* Clip to draw area (set by E3/E4 GPU commands) and VRAM bounds */
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1 + 1) x1 = clip_x1 + 1;
    if (y1 > clip_y1 + 1) y1 = clip_y1 + 1;
    if (x1 > VRAM_WIDTH) x1 = VRAM_WIDTH;
    if (y1 > VRAM_HEIGHT) y1 = VRAM_HEIGHT;
    int cw = x1 - x0;
    if (cw <= 0 || y0 >= y1) return;

    /* Use memset-style fill for speed */
    for (int vy = y0; vy < y1; vy++)
    {
        uint16_t *row = &vram[vy][x0];
        /* Fill with 32-bit writes for speed */
        uint32_t color32 = ((uint32_t)color << 16) | color;
        int i = 0;
        uint32_t *row32 = (uint32_t *)row;
        int cw2 = cw / 2;
        for (; i < cw2; i++)
            row32[i] = color32;
        if (cw & 1)
            row[cw - 1] = color;
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
/* Per-vertex RGB colors (0-255, 128=neutral for PSX modulation) */
int port_tri_r[3] = {128, 128, 128};
int port_tri_g[3] = {128, 128, 128};
int port_tri_b[3] = {128, 128, 128};

extern uint16_t sample_vram_texel(uint16_t tpage, uint16_t clut, int u, int v);

/* Draw a textured or flat-colored triangle */

/* Per-vertex Z for perspective-correct interpolation */
int port_tri_z[3] = {1, 1, 1};

void draw_flat_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    /* Sort vertices by y, keeping UV/color indices in sync */
    int ui[3] = {0, 1, 2};
    if (y0 > y1) { int t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; t=ui[0];ui[0]=ui[1];ui[1]=t; }
    if (y0 > y2) { int t; t=x0;x0=x2;x2=t; t=y0;y0=y2;y2=t; t=ui[0];ui[0]=ui[2];ui[2]=t; }
    if (y1 > y2) { int t; t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; t=ui[1];ui[1]=ui[2];ui[2]=t; }

    uint16_t z = port_current_z;
    int textured = port_tex_enabled;

    /* Sorted UVs (affine, no perspective correction — matches PSX) */
    int tu[3], tv[3];
    if (textured) {
        tu[0] = port_tri_u[ui[0]]; tv[0] = port_tri_v[ui[0]];
        tu[1] = port_tri_u[ui[1]]; tv[1] = port_tri_v[ui[1]];
        tu[2] = port_tri_u[ui[2]]; tv[2] = port_tri_v[ui[2]];
    }

    /* Sorted per-vertex colors for Gouraud interpolation (8.8 fixed-point) */
    int cr[3], cg[3], cb[3];
    cr[0] = port_tri_r[ui[0]]; cg[0] = port_tri_g[ui[0]]; cb[0] = port_tri_b[ui[0]];
    cr[1] = port_tri_r[ui[1]]; cg[1] = port_tri_g[ui[1]]; cb[1] = port_tri_b[ui[1]];
    cr[2] = port_tri_r[ui[2]]; cg[2] = port_tri_g[ui[2]]; cb[2] = port_tri_b[ui[2]];

    /* Precompute texture constants outside pixel loop */
    int tp = 0, tex_base_x = 0, tex_base_y = 0, clut_x = 0, clut_y = 0;
    if (textured) {
        tp = (port_tex_tpage >> 7) & 0x3;
        tex_base_x = (port_tex_tpage & 0xF) * 64;
        tex_base_y = ((port_tex_tpage >> 4) & 0x1) * 256;
        if (port_tex_tpage & 0x800) tex_base_y += 512;
        clut_x = (port_tex_clut & 0x3F) * 16;
        clut_y = (port_tex_clut >> 6) & 0x1FF;
    }

    int dy_total = y2 - y0;
    if (dy_total == 0) dy_total = 1;

    for (int y = y0; y <= y2; y++)
    {
        int vy = y + draw_y;
        if (vy < clip_y0 || vy > clip_y1) continue;

        int dy0 = y - y0;
        int xa, xb, ua, va, ub, vb;
        int ra, ga, ba, rb2, gb, bb;

        /* Edge a: v0 → v2 (long edge) */
        xa = x0 + (x2 - x0) * dy0 / dy_total;
        ra = cr[0] + (cr[2] - cr[0]) * dy0 / dy_total;
        ga = cg[0] + (cg[2] - cg[0]) * dy0 / dy_total;
        ba = cb[0] + (cb[2] - cb[0]) * dy0 / dy_total;
        if (textured) {
            ua = tu[0] + (tu[2] - tu[0]) * dy0 / dy_total;
            va = tv[0] + (tv[2] - tv[0]) * dy0 / dy_total;
        }

        /* Edge b: v0→v1 or v1→v2 */
        if (y < y1) {
            int dy01 = y1 - y0; if (dy01 == 0) dy01 = 1;
            xb = x0 + (x1 - x0) * dy0 / dy01;
            rb2 = cr[0] + (cr[1] - cr[0]) * dy0 / dy01;
            gb = cg[0] + (cg[1] - cg[0]) * dy0 / dy01;
            bb = cb[0] + (cb[1] - cb[0]) * dy0 / dy01;
            if (textured) {
                ub = tu[0] + (tu[1] - tu[0]) * dy0 / dy01;
                vb = tv[0] + (tv[1] - tv[0]) * dy0 / dy01;
            }
        } else {
            int dy12 = y2 - y1; if (dy12 == 0) dy12 = 1;
            int dy1 = y - y1;
            xb = x1 + (x2 - x1) * dy1 / dy12;
            rb2 = cr[1] + (cr[2] - cr[1]) * dy1 / dy12;
            gb = cg[1] + (cg[2] - cg[1]) * dy1 / dy12;
            bb = cb[1] + (cb[2] - cb[1]) * dy1 / dy12;
            if (textured) {
                ub = tu[1] + (tu[2] - tu[1]) * dy1 / dy12;
                vb = tv[1] + (tv[2] - tv[1]) * dy1 / dy12;
            }
        }

        /* Ensure xa <= xb */
        if (xa > xb) {
            int t; t=xa;xa=xb;xb=t;
            t=ra;ra=rb2;rb2=t; t=ga;ga=gb;gb=t; t=ba;ba=bb;bb=t;
            if (textured) { int ti; ti=ua;ua=ub;ub=ti; ti=va;va=vb;vb=ti; }
        }

        int dx = xb - xa;
        if (dx == 0) dx = 1;

        /* Clamp scanline to clip rect */
        int x_start = xa + draw_x;
        int x_end = xb + draw_x;
        int x_skip = 0;
        if (x_start < clip_x0) { x_skip = clip_x0 - x_start; x_start = clip_x0; }
        if (x_end > clip_x1) x_end = clip_x1;
        if (x_start > x_end) continue;

        /* Fixed-point 16.16 color interpolation across scanline */
        int r16 = ra << 16, dr16 = ((rb2 - ra) << 16) / dx;
        int g16 = ga << 16, dg16 = ((gb - ga) << 16) / dx;
        int b16 = ba << 16, db16 = ((bb - ba) << 16) / dx;
        r16 += dr16 * x_skip;
        g16 += dg16 * x_skip;
        b16 += db16 * x_skip;

        if (textured) {
            /* Fixed-point 16.16 affine interpolation */
            int u16 = ua << 16;
            int v16 = va << 16;
            int du16 = ((ub - ua) << 16) / dx;
            int dv16 = ((vb - va) << 16) / dx;
            u16 += du16 * x_skip;
            v16 += dv16 * x_skip;

            uint16_t *row = &vram[vy][x_start];
            for (int x = x_start; x <= x_end; x++) {
                int u = (u16 >> 16) & 0xFF;
                int v = (v16 >> 16) & 0xFF;
                uint16_t c;

                /* Inline texel lookup */
                if (tp == 0) { /* 4-bit CLUT */
                    uint16_t texel = vram[tex_base_y + v][tex_base_x + (u >> 2)];
                    int idx = (texel >> ((u & 3) * 4)) & 0xF;
                    c = vram[clut_y][clut_x + idx];
                } else if (tp == 1) { /* 8-bit CLUT */
                    uint16_t texel = vram[tex_base_y + v][tex_base_x + (u >> 1)];
                    int idx = (u & 1) ? (texel >> 8) & 0xFF : texel & 0xFF;
                    c = vram[clut_y][clut_x + idx];
                } else { /* 16-bit direct */
                    c = vram[tex_base_y + v][tex_base_x + u];
                }

                if (c != 0 && z <= port_zbuf[vy][x]) {
                    /* PSX texture modulation: texel_component * vertex_color / 128
                       Vertex color 128 = neutral (1.0), 0 = black, 255 = ~2x bright */
                    int ir = r16 >> 16, ig = g16 >> 16, ib = b16 >> 16;
                    if (ir < 0) ir = 0; if (ig < 0) ig = 0; if (ib < 0) ib = 0;
                    uint16_t stp = c & 0x8000; /* preserve semi-transparency bit */
                    int tr = c & 0x1F, tg = (c >> 5) & 0x1F, tb = (c >> 10) & 0x1F;
                    tr = (tr * ir) >> 7; if (tr > 31) tr = 31;
                    tg = (tg * ig) >> 7; if (tg > 31) tg = 31;
                    tb = (tb * ib) >> 7; if (tb > 31) tb = 31;
                    c = stp | (tb << 10) | (tg << 5) | tr;

                    if (port_tex_semi_trans && (c & 0x8000)) {
                        uint16_t bg = *row;
                        int br = bg & 0x1F, bg2 = (bg>>5)&0x1F, bbb = (bg>>10)&0x1F;
                        int fr = c & 0x1F,  fg = (c>>5)&0x1F,   fb = (c>>10)&0x1F;
                        int rr, rg, rrb;
                        switch (port_tex_abr) {
                        case 0: rr=(br+fr)/2; rg=(bg2+fg)/2; rrb=(bbb+fb)/2; break;
                        case 1: rr=br+fr; rg=bg2+fg; rrb=bbb+fb; break;
                        case 2: rr=br-fr; rg=bg2-fg; rrb=bbb-fb; break;
                        case 3: rr=br+fr/4; rg=bg2+fg/4; rrb=bbb+fb/4; break;
                        default: rr=fr; rg=fg; rrb=fb; break;
                        }
                        if(rr<0)rr=0; if(rr>31)rr=31;
                        if(rg<0)rg=0; if(rg>31)rg=31;
                        if(rrb<0)rrb=0; if(rrb>31)rrb=31;
                        c = (rrb<<10)|(rg<<5)|rr;
                    }
                    *row = c;
                    port_zbuf[vy][x] = z;
                }
                row++;
                u16 += du16;
                v16 += dv16;
                r16 += dr16;
                g16 += dg16;
                b16 += db16;
            }
        } else {
            /* Flat/Gouraud shaded — modulate color by vertex lighting */
            uint16_t *row = &vram[vy][x_start];
            uint16_t *zrow = &port_zbuf[vy][x_start];
            for (int x = x_start; x <= x_end; x++) {
                if (z <= *zrow) {
                    int ir = r16 >> 16, ig = g16 >> 16, ib = b16 >> 16;
                    if (ir < 0) ir = 0; if (ig < 0) ig = 0; if (ib < 0) ib = 0;
                    int fr = color & 0x1F, fg = (color >> 5) & 0x1F, fb = (color >> 10) & 0x1F;
                    fr = (fr * ir) >> 7; if (fr > 31) fr = 31;
                    fg = (fg * ig) >> 7; if (fg > 31) fg = 31;
                    fb = (fb * ib) >> 7; if (fb > 31) fb = 31;
                    uint16_t c = (fb << 10) | (fg << 5) | fr;
                    if (port_tex_semi_trans) {
                        uint16_t bg = *row;
                        int br = bg & 0x1F, bg2 = (bg>>5)&0x1F, bbb = (bg>>10)&0x1F;
                        int rr, rg, rrb;
                        switch (port_tex_abr) {
                        case 0: rr=(br+fr)/2; rg=(bg2+fg)/2; rrb=(bbb+fb)/2; break;
                        case 1: rr=br+fr; rg=bg2+fg; rrb=bbb+fb; break;
                        case 2: rr=br-fr; rg=bg2-fg; rrb=bbb-fb; break;
                        case 3: rr=br+fr/4; rg=bg2+fg/4; rrb=bbb+fb/4; break;
                        default: rr=fr; rg=fg; rrb=fb; break;
                        }
                        if(rr<0)rr=0; if(rr>31)rr=31;
                        if(rg<0)rg=0; if(rg>31)rg=31;
                        if(rrb<0)rrb=0; if(rrb>31)rrb=31;
                        c = (rrb<<10)|(rg<<5)|rr;
                    }
                    *row = c;
                    *zrow = z;
                }
                row++; zrow++;
                r16 += dr16;
                g16 += dg16;
                b16 += db16;
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

    /* Deferred clear is now applied in main_game.c before port_RenderObjects,
       so 3D geometry isn't erased. If it wasn't consumed there (e.g. skip_render),
       apply it here as fallback. */
    extern void port_apply_deferred_clear(void);
    port_apply_deferred_clear();

    /* Reset draw offset for each OT walk. */
    draw_x = 0;
    draw_y = 0;
    clip_x0 = 0; clip_y0 = 0;
    clip_x1 = 319; clip_y1 = 223;

    /* 2D OT primitives should always pass the z-test.
       port_RenderObjects clears port_zbuf, but during codec (DG_FrameRate==2)
       it's skipped, leaving stale z values that reject face pixels. */
    port_current_z = 0;

    while (p && !isendprim(p))
    {
        unsigned long *next = (unsigned long *)nextPrim(p);
        int len = getlen(p);
        node_count++;

        if (node_count > 100000) {
            printf("[ot] ABORT: >100000 nodes, likely cycle\n");
            break;
        }
        if (node_count == 1 && drawot_debug < 10) {
            /* Count total nodes once to measure OT length */
            unsigned long *scan = ot;
            int total = 0;
            while (scan && !isendprim(scan) && total < 500000) {
                scan = (unsigned long *)nextPrim(scan);
                total++;
            }
            printf("[ot] chain length: %d nodes\n", total);
        }

        if (len > 0)
        {
            unsigned char code = *((unsigned char *)p + 7);  /* command byte */
            unsigned char *data = (unsigned char *)p + 4;    /* after tag */

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
                if (!(w >= 320 && h >= 200)) {
                    if (code & 0x02) {
                        /* Semi-transparent: PSX blend mode 0 = B/2 + F/2 */
                        port_DrawTileSemiTrans(x, y, w, h, r, g, b);
                    } else {
                        port_DrawTile(x, y, w, h, r, g, b);
                    }
                }
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
                port_tex_enabled = 0;
                port_tri_r[0]=port_tri_r[1]=port_tri_r[2] = 128;
                port_tri_g[0]=port_tri_g[1]=port_tri_g[2] = 128;
                port_tri_b[0]=port_tri_b[1]=port_tri_b[2] = 128;
                draw_flat_tri(x0, y0, x1, y1, x2, y2, color);
                if ((code & 0xFC) == 0x28) /* F4: draw second triangle */
                {
                    short x3 = *(short *)(data + 16), y3 = *(short *)(data + 18);
                    draw_flat_tri(x1, y1, x2, y2, x3, y3, color);
                }
                prim_count++;
                break;
            }
            case 0x30: /* POLY_G3 — gouraud triangle */
            {
                /* G3 layout: r0g0b0 code x0y0 r1g1b1 pad x1y1 r2g2b2 pad x2y2 */
                short x0 = *(short *)(data + 4),  y0 = *(short *)(data + 6);
                short x1 = *(short *)(data + 12), y1 = *(short *)(data + 14);
                short x2 = *(short *)(data + 20), y2 = *(short *)(data + 22);
                port_tex_enabled = 0;
                port_tex_semi_trans = (code & 0x02) ? 1 : 0;
                if (port_tex_semi_trans) port_tex_abr = (port_current_tpage >> 5) & 0x3;
                port_tri_r[0] = data[0];  port_tri_g[0] = data[1];  port_tri_b[0] = data[2];
                port_tri_r[1] = data[8];  port_tri_g[1] = data[9];  port_tri_b[1] = data[10];
                port_tri_r[2] = data[16]; port_tri_g[2] = data[17]; port_tri_b[2] = data[18];
                draw_flat_tri(x0, y0, x1, y1, x2, y2, 0x4210);
                port_tex_semi_trans = 0;
                prim_count++;
                break;
            }
            case 0x38: /* POLY_G4 — gouraud quad */
            {
                /* G4 layout: r0g0b0 code x0y0 r1g1b1 pad x1y1 r2g2b2 pad x2y2 r3g3b3 pad x3y3 */
                short x0 = *(short *)(data + 4),  y0 = *(short *)(data + 6);
                short x1 = *(short *)(data + 12), y1 = *(short *)(data + 14);
                short x2 = *(short *)(data + 20), y2 = *(short *)(data + 22);
                short x3 = *(short *)(data + 28), y3 = *(short *)(data + 30);
                port_tex_enabled = 0;
                port_tex_semi_trans = (code & 0x02) ? 1 : 0;
                if (port_tex_semi_trans) port_tex_abr = (port_current_tpage >> 5) & 0x3;
                /* Tri 1: v0, v1, v2 */
                port_tri_r[0] = data[0];  port_tri_g[0] = data[1];  port_tri_b[0] = data[2];
                port_tri_r[1] = data[8];  port_tri_g[1] = data[9];  port_tri_b[1] = data[10];
                port_tri_r[2] = data[16]; port_tri_g[2] = data[17]; port_tri_b[2] = data[18];
                draw_flat_tri(x0, y0, x1, y1, x2, y2, 0x4210);
                /* Tri 2: v1, v3, v2 */
                port_tri_r[0] = data[8];  port_tri_g[0] = data[9];  port_tri_b[0] = data[10];
                port_tri_r[1] = data[24]; port_tri_g[1] = data[25]; port_tri_b[1] = data[26];
                port_tri_r[2] = data[16]; port_tri_g[2] = data[17]; port_tri_b[2] = data[18];
                draw_flat_tri(x1, y1, x3, y3, x2, y2, 0x4210);
                port_tex_semi_trans = 0;
                prim_count++;
                break;
            }
            case 0x24: /* POLY_FT3 — textured flat-shaded triangle */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4),  y0 = *(short *)(data + 6);
                unsigned char u0 = data[8], v0 = data[9];
                uint16_t clut = *(uint16_t *)(data + 10);
                short x1 = *(short *)(data + 12), y1 = *(short *)(data + 14);
                unsigned char u1 = data[16], v1 = data[17];
                uint16_t tpage = *(uint16_t *)(data + 18);
                short x2 = *(short *)(data + 20), y2 = *(short *)(data + 22);
                unsigned char u2 = data[24], v2 = data[25];

                port_tex_tpage = tpage;
                port_tex_clut = clut;
                port_tex_enabled = 1;
                port_tex_semi_trans = (code & 0x02) ? 1 : 0;
                if (port_tex_semi_trans) port_tex_abr = (tpage >> 5) & 0x3;

                port_tri_r[0] = r; port_tri_g[0] = g; port_tri_b[0] = b;
                port_tri_r[1] = r; port_tri_g[1] = g; port_tri_b[1] = b;
                port_tri_r[2] = r; port_tri_g[2] = g; port_tri_b[2] = b;

                port_tri_u[0] = u0; port_tri_v[0] = v0;
                port_tri_u[1] = u1; port_tri_v[1] = v1;
                port_tri_u[2] = u2; port_tri_v[2] = v2;
                draw_flat_tri(x0, y0, x1, y1, x2, y2, 0);

                port_tex_enabled = 0;
                prim_count++;
                break;
            }
            case 0x2C: /* POLY_FT4 — textured flat-shaded quad */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4),  y0 = *(short *)(data + 6);
                unsigned char u0 = data[8], v0 = data[9];
                uint16_t clut = *(uint16_t *)(data + 10);
                short x1 = *(short *)(data + 12), y1 = *(short *)(data + 14);
                unsigned char u1 = data[16], v1 = data[17];
                uint16_t tpage = *(uint16_t *)(data + 18);
                short x2 = *(short *)(data + 20), y2 = *(short *)(data + 22);
                unsigned char u2 = data[24], v2 = data[25];
                short x3 = *(short *)(data + 28), y3 = *(short *)(data + 30);
                unsigned char u3 = data[32], v3 = data[33];

                port_tex_tpage = tpage;
                port_tex_clut = clut;
                port_tex_enabled = 1;
                port_tex_semi_trans = (code & 0x02) ? 1 : 0;
                if (port_tex_semi_trans) port_tex_abr = (tpage >> 5) & 0x3;

                port_tri_r[0] = r; port_tri_g[0] = g; port_tri_b[0] = b;
                port_tri_r[1] = r; port_tri_g[1] = g; port_tri_b[1] = b;
                port_tri_r[2] = r; port_tri_g[2] = g; port_tri_b[2] = b;

                port_tri_u[0] = u0; port_tri_v[0] = v0;
                port_tri_u[1] = u1; port_tri_v[1] = v1;
                port_tri_u[2] = u2; port_tri_v[2] = v2;
                draw_flat_tri(x0, y0, x1, y1, x2, y2, 0);

                port_tri_u[0] = u1; port_tri_v[0] = v1;
                port_tri_u[1] = u3; port_tri_v[1] = v3;
                port_tri_u[2] = u2; port_tri_v[2] = v2;
                draw_flat_tri(x1, y1, x3, y3, x2, y2, 0);

                port_tex_enabled = 0;
                prim_count++;
                break;
            }
            case 0x34: /* POLY_GT3 — gouraud textured triangle */
            {
                unsigned char r0 = data[0], g0 = data[1], b0 = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                unsigned char u0 = data[8], v0 = data[9];
                uint16_t clut = *(uint16_t *)(data + 10);
                unsigned char r1 = data[12], g1 = data[13], b1 = data[14];
                short x1 = *(short *)(data + 16), y1 = *(short *)(data + 18);
                unsigned char u1 = data[20], v1 = data[21];
                uint16_t tpage = *(uint16_t *)(data + 22);
                unsigned char r2 = data[24], g2 = data[25], b2 = data[26];
                short x2 = *(short *)(data + 28), y2 = *(short *)(data + 30);
                unsigned char u2 = data[32], v2 = data[33];

                port_tex_tpage = tpage;
                port_tex_clut = clut;
                port_tex_enabled = 1;
                port_tex_semi_trans = (code & 0x02) ? 1 : 0;
                if (port_tex_semi_trans) port_tex_abr = (tpage >> 5) & 0x3;

                port_tri_r[0] = r0; port_tri_g[0] = g0; port_tri_b[0] = b0;
                port_tri_r[1] = r1; port_tri_g[1] = g1; port_tri_b[1] = b1;
                port_tri_r[2] = r2; port_tri_g[2] = g2; port_tri_b[2] = b2;
                port_tri_u[0] = u0; port_tri_v[0] = v0;
                port_tri_u[1] = u1; port_tri_v[1] = v1;
                port_tri_u[2] = u2; port_tri_v[2] = v2;
                draw_flat_tri(x0, y0, x1, y1, x2, y2, 0);

                port_tex_enabled = 0;
                prim_count++;
                break;
            }
            case 0x3C: /* POLY_GT4 — gouraud textured quad */
            {
                unsigned char r0 = data[0], g0 = data[1], b0 = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                unsigned char u0 = data[8], v0 = data[9];
                uint16_t clut = *(uint16_t *)(data + 10);
                unsigned char r1 = data[12], g1 = data[13], b1 = data[14];
                short x1 = *(short *)(data + 16), y1 = *(short *)(data + 18);
                unsigned char u1 = data[20], v1 = data[21];
                uint16_t tpage = *(uint16_t *)(data + 22);
                unsigned char r2 = data[24], g2 = data[25], b2 = data[26];
                short x2 = *(short *)(data + 28), y2 = *(short *)(data + 30);
                unsigned char u2 = data[32], v2 = data[33];
                unsigned char r3 = data[36], g3 = data[37], b3 = data[38];
                short x3 = *(short *)(data + 40), y3 = *(short *)(data + 42);
                unsigned char u3 = data[44], v3 = data[45];

                port_tex_tpage = tpage;
                port_tex_clut = clut;
                port_tex_enabled = 1;
                port_tex_semi_trans = (code & 0x02) ? 1 : 0;
                if (port_tex_semi_trans) port_tex_abr = (tpage >> 5) & 0x3;

                /* Tri 1: v0, v1, v2 */
                port_tri_r[0] = r0; port_tri_g[0] = g0; port_tri_b[0] = b0;
                port_tri_r[1] = r1; port_tri_g[1] = g1; port_tri_b[1] = b1;
                port_tri_r[2] = r2; port_tri_g[2] = g2; port_tri_b[2] = b2;
                port_tri_u[0] = u0; port_tri_v[0] = v0;
                port_tri_u[1] = u1; port_tri_v[1] = v1;
                port_tri_u[2] = u2; port_tri_v[2] = v2;
                draw_flat_tri(x0, y0, x1, y1, x2, y2, 0);

                /* Tri 2: v1, v3, v2 */
                port_tri_r[0] = r1; port_tri_g[0] = g1; port_tri_b[0] = b1;
                port_tri_r[1] = r3; port_tri_g[1] = g3; port_tri_b[1] = b3;
                port_tri_r[2] = r2; port_tri_g[2] = g2; port_tri_b[2] = b2;
                port_tri_u[0] = u1; port_tri_v[0] = v1;
                port_tri_u[1] = u3; port_tri_v[1] = v3;
                port_tri_u[2] = u2; port_tri_v[2] = v2;
                draw_flat_tri(x1, y1, x3, y3, x2, y2, 0);

                port_tex_enabled = 0;
                prim_count++;
                break;
            }
            case 0x40: /* LINE_F2 — flat-colored line (used by radar) */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                short x1 = *(short *)(data + 8), y1 = *(short *)(data + 10);
                uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
                /* Bresenham line with draw area clipping */
                int dx = abs(x1 - x0), dy = abs(y1 - y0);
                int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
                int err = dx - dy;
                for (int steps = 0; steps < 1024; steps++) {
                    int vx = x0 + draw_x, vy = y0 + draw_y;
                    if (vx >= clip_x0 && vx <= clip_x1 && vy >= clip_y0 && vy <= clip_y1)
                        vram[vy][vx] = color;
                    if (x0 == x1 && y0 == y1) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; x0 += sx; }
                    if (e2 < dx)  { err += dx; y0 += sy; }
                }
                prim_count++;
                break;
            }
            case 0x4C: /* LINE_F4 — 4-vertex POLYLINE (3 connected line segments:
                          v0→v1, v1→v2, v2→v3). Used for codec UI outlines and digit
                          segments (game traces segment-bar outlines using this). */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
                int px[4], py[4];
                for (int p = 0; p < 4; p++) {
                    px[p] = *(short *)(data + 4 + p*4);
                    py[p] = *(short *)(data + 6 + p*4);
                }
                /* Draw 3 connected line segments via Bresenham */
                for (int seg = 0; seg < 3; seg++) {
                    int x0 = px[seg], y0 = py[seg];
                    int x1 = px[seg+1], y1 = py[seg+1];
                    int dx = abs(x1 - x0), dy = abs(y1 - y0);
                    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
                    int err = dx - dy;
                    for (int steps = 0; steps < 2048; steps++) {
                        int vx = x0 + draw_x, vy = y0 + draw_y;
                        if (vx >= clip_x0 && vx <= clip_x1 && vy >= clip_y0 && vy <= clip_y1)
                            vram[vy][vx] = color;
                        if (x0 == x1 && y0 == y1) break;
                        int e2 = 2 * err;
                        if (e2 > -dy) { err -= dy; x0 += sx; }
                        if (e2 < dx)  { err += dx; y0 += sy; }
                    }
                }
                prim_count++;
                break;
            }
            case 0x48: /* LINE_G2 — gouraud-colored line */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x0 = *(short *)(data + 4), y0 = *(short *)(data + 6);
                /* second vertex color at data+8, coords at data+12 */
                short x1 = *(short *)(data + 12), y1 = *(short *)(data + 14);
                uint16_t color = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
                int dx = abs(x1 - x0), dy = abs(y1 - y0);
                int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
                int err = dx - dy;
                for (int steps = 0; steps < 1024; steps++) {
                    int vx = x0 + draw_x, vy = y0 + draw_y;
                    if (vx >= clip_x0 && vx <= clip_x1 && vy >= clip_y0 && vy <= clip_y1)
                        vram[vy][vx] = color;
                    if (x0 == x1 && y0 == y1) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; x0 += sx; }
                    if (e2 < dx)  { err += dx; y0 += sy; }
                }
                prim_count++;
                break;
            }
            case 0x64: /* SPRT (textured sprite — used by font system) */
            {
                unsigned char r = data[0], g = data[1], b = data[2];
                short x = *(short *)(data + 4) + draw_x;
                short y = *(short *)(data + 6) + draw_y;
                unsigned char u0 = data[8];
                unsigned char v0 = data[9];
                uint16_t clut = *(uint16_t *)(data + 10);
                short w = *(short *)(data + 12);
                short h = *(short *)(data + 14);
                /* Semi-transparent SPRT: code bit 1 set (0x66 vs 0x64). Blend pixels where
                   TEXEL has STP bit set, using current tpage ABR mode. */
                int sprt_semi = (code & 0x02) ? 1 : 0;
                int sprt_abr = (port_current_tpage >> 5) & 0x3;
                /* Render sprite from VRAM texture, clipped to draw area.
                   Apply color modulation: PSX GPU multiplies texel by (r,g,b)/128. */
                for (int sy = 0; sy < h && (y+sy) <= clip_y1; sy++) {
                    if ((y+sy) < clip_y0) continue;
                    for (int sx = 0; sx < w && (x+sx) <= clip_x1; sx++) {
                        if ((x+sx) < clip_x0) continue;
                        uint16_t c = sample_vram_texel(port_current_tpage, clut, u0+sx, v0+sy);
                        if (c == 0) continue; /* color 0 = transparent */
                        uint16_t stp = c & 0x8000;
                        /* Apply color modulation (r,g,b are 0-255, neutral=128) */
                        int fr, fg, fb;
                        if (r != 128 || g != 128 || b != 128) {
                            int cr = (c & 0x1F);
                            int cg = (c >> 5) & 0x1F;
                            int cb = (c >> 10) & 0x1F;
                            cr = (cr * r) >> 7; if (cr > 31) cr = 31;
                            cg = (cg * g) >> 7; if (cg > 31) cg = 31;
                            cb = (cb * b) >> 7; if (cb > 31) cb = 31;
                            fr = cr; fg = cg; fb = cb;
                        } else {
                            fr = c & 0x1F;
                            fg = (c >> 5) & 0x1F;
                            fb = (c >> 10) & 0x1F;
                        }
                        int nr, ng, nb;
                        if (sprt_semi && stp) {
                            /* Semi-trans blend with current ABR mode */
                            uint16_t bg = vram[y+sy][x+sx];
                            int br = bg & 0x1F, bgr = (bg >> 5) & 0x1F, bb = (bg >> 10) & 0x1F;
                            switch (sprt_abr) {
                            case 0: nr = (br + fr) >> 1;  ng = (bgr + fg) >> 1;  nb = (bb + fb) >> 1;  break;
                            case 1: nr = br + fr;         ng = bgr + fg;         nb = bb + fb;         break;
                            case 2: nr = br - fr;         ng = bgr - fg;         nb = bb - fb;         break;
                            case 3: nr = br + (fr >> 2);  ng = bgr + (fg >> 2);  nb = bb + (fb >> 2);  break;
                            default: nr = fr; ng = fg; nb = fb; break;
                            }
                            if (nr < 0) nr = 0; if (nr > 31) nr = 31;
                            if (ng < 0) ng = 0; if (ng > 31) ng = 31;
                            if (nb < 0) nb = 0; if (nb > 31) nb = 31;
                        } else {
                            nr = fr; ng = fg; nb = fb;
                        }
                        vram[y+sy][x+sx] = (uint16_t)(nr | (ng << 5) | (nb << 10) | stp);
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
            case 0xE0: /* GPU environment commands (E1-E6) */
            case 0xE4:
            {
                /* DR_ENV nodes pack multiple GPU commands (E1-E6) into one
                   OT entry with len words. Process ALL words, not just the first. */
                for (int wi = 0; wi < len; wi++) {
                    uint32_t cmd = ((uint32_t *)data)[wi];
                    uint8_t cmd_code = (cmd >> 24) & 0xFF;
                    switch (cmd_code) {
                    case 0xE1: /* Draw Mode / TPage */
                        port_current_tpage = (uint16_t)(cmd & 0xFFFF);
                        break;
                    case 0xE5: /* Set Drawing Offset */
                    {
                        int ox = (cmd & 0x7FF);
                        int oy = (cmd >> 11) & 0x7FF;
                        if (ox & 0x400) ox |= ~0x7FF;
                        if (oy & 0x400) oy |= ~0x7FF;
                        /* Subtract PSX double-buffer base for this OT.
                           PSX uses either X-based (320,0)/(0,0) or Y-based
                           (0,0)/(0,224) double buffering. The port always
                           displays from (0,0), so subtract the buffer offset. */
                        draw_x = ox;
                        draw_y = oy;
                        /* Remove double-buffer offset */
                        if (draw_x >= 320) draw_x -= 320;
                        if (draw_y >= 224) draw_y -= 224;
                        break;
                    }
                    case 0xE3: /* Set Drawing Area Top-Left */
                    {
                        int ax = (cmd & 0x3FF);
                        int ay = (cmd >> 10) & 0x1FF;
                        /* Remove double-buffer offset */
                        if (ax >= 320) ax -= 320;
                        if (ay >= 224) ay -= 224;
                        clip_x0 = ax;
                        clip_y0 = ay;
                        if (clip_x0 < 0) clip_x0 = 0;
                        if (clip_y0 < 0) clip_y0 = 0;
                        break;
                    }
                    case 0xE4: /* Set Drawing Area Bottom-Right */
                    {
                        int ax = (cmd & 0x3FF);
                        int ay = (cmd >> 10) & 0x1FF;
                        if (ax >= 320) ax -= 320;
                        if (ay >= 224) ay -= 224;
                        clip_x1 = ax;
                        clip_y1 = ay;
                        if (clip_x1 > 319) clip_x1 = 319;
                        if (clip_y1 > 223) clip_y1 = 223;
                        break;
                    }
                    /* E2=tex window, E6=mask — ignored */
                    }
                }
                prim_count++;
                break;
            }
            default:
                break;
            }
        }

        p = next;
        if (prim_count > 10000) break; /* safety limit */
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
        /* DR_TPAGE stores an E1 GPU command word. Extract tpage from low 16 bits. */
        uint32_t cmd = *(uint32_t *)(data);
        port_current_tpage = (uint16_t)(cmd & 0xFFFF);
        break;
    }
    default:
        break;
    }
}
