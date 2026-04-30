---
file: source/libdg/text.c + palette.c
---

# `libdg/text.c` + `palette.c` — texture / palette management

## Texture table

```c
typedef struct DG_TEX {
    u_short  id;        // 16-bit hash of texture name
    u_char   used;      // 0 = free slot
    u_char   col;       // colour mode (4-bit / 8-bit / 15-bit)
    u_short  tpage;     // PSX tpage descriptor (already in GPU format)
    u_short  clut;      // CLUT descriptor
    u_char   off_x;     // offset within tpage (texels)
    u_char   off_y;
    u_char   w;         // width / height in texels
    u_char   h;
} DG_TEX;
```

`DG_MAX_TEXTURES = 512`. The full table is bss-allocated; entries
are reserved/freed as PCX files load and unload.

## API

```c
void    DG_InitTextureSystem(void);
DG_TEX *DG_GetTexture(int id);                     // find by hash
void    DG_SetTexture(int id, int tp, int abr,
                      RECT *img, RECT *pal, int col);
void    DG_GetTextureRect(DG_TEX *tex, RECT *rect);  // back to VRAM coords
void    DG_GetClutRect(DG_TEX *tex, RECT *rect);
void    DG_ClearResidentTexture(void);
void    DG_SaveResidentTextureCache(void);
void    DG_LoadResidentTextureCache(void);
```

`DG_GetTexture(id)` linear-scans for a matching id (no hash table) —
the texture set is small enough that linear scan is fine.

`DG_SetTexture` records a texture given:

- `id`: name hash
- `tp`: tpage value (pre-computed for GPU)
- `abr`: blend mode (0 = opaque, 1/2/3 = additive/subtractive variants)
- `img`: RECT in VRAM where pixels live
- `pal`: RECT in VRAM where CLUT lives
- `col`: colour-mode flag (4 / 8 / 15-bit)

The `tpage` and `clut` are *PSX GPU-format* values returned by
`getTPage()` and `getClut()` respectively — they pack VRAM
coordinates and modes into a single u_short so the GPU can consume
them directly via packet `tpage`/`clut` fields.

## Resident textures

Some textures (the font glyphs, the global radar icons) load once
and stay across stages. They live in a *resident* sub-region:

```c
DG_SaveResidentTextureCache();   // snapshot before stage transition
DG_ClearResidentTexture();        // wipe all
// resident entries restored from snapshot
DG_LoadResidentTextureCache();    // restore after transition
```

Same pattern as `GV_SaveResidentFileCache` in libgv/cache.c.

## Palette FX — `palette.c`

Tiny file (~19 lines) that handles palette swap effects:

- `DG_StorePalette()` — snapshot current palette to backup
- `DG_ReloadPalette()` — restore from backup
- `DG_StorePalette2()` / `DG_StorePaletteEffect()` — variants
- `DG_MakeEffectPalette(palette, count)` — generate a faded /
  tinted variant of an existing palette
- `DG_SetExtPaletteMakeFunc(make_fn, pixel_fn)` — install a
  custom palette transformer; called during fade-in / fade-out
- `DG_ResetPaletteEffect()` — drop the custom transformer

Used during cutscene fades, codec-call ring overlay, and the boss-
encounter colour shifts (e.g. Liquid's intro fade-to-red).

## Pitfalls

- **The DG_TEX::tpage is GPU-format already.** Don't try to OR
  bits onto it — use `getTPage()`-style helpers if you need to
  rebuild it.
- **CLUTs live in VRAM, not RAM.** Updating a CLUT means writing
  to VRAM; that's slow. Most palette FX rebuild a CLUT in RAM
  then DMA-blit it.
- **Resident textures must fit.** Resident region in VRAM is
  fixed-size; loading too many = collision with stage textures.

---

## Port notes

The port replaces VRAM with an OpenGL texture array; `DG_TEX::tpage`
becomes an index into a host texture pool. `port/libdg/gl_renderer.c`
intercepts `DG_SetTexture` to upload PCX bytes to the GPU.

## See also

- [pipeline.md](pipeline.md) — texture data flows into packet
  `tpage`/`clut` fields during prim stage.
- [`source/libdg/loader.c`](../../../../source/libdg/loader.c) —
  PCX loader registers textures via `DG_SetTexture`.
