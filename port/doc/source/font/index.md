# `source/font/` — text rasteriser

Two files implement every glyph that appears on screen in MGS — from
the main menu through codec dialog through pause-screen weapon names.

| File | Purpose |
| ---- | ------- |
| [`font.c`](../../../../source/font/font.c) | The rasteriser — converts a string + a glyph table into a textured prim chain submitted to a chanl OT. |
| [`font.h`](../../../../source/font/font.h) | Public API + the `KCB` (Kanji Control Block) handle struct. |

## Why a custom rasteriser

The PSX GPU has no text primitive. Every character on screen is a
textured quad (POLY_FT4) with UVs cut from a glyph atlas in VRAM —
which means the engine needs a layout engine to walk the string, look
each codepoint up in a glyph table, slice out the right rect, advance
the cursor by the glyph's pixel width, and emit a packet to the
ordering table.

MGS speaks Japanese natively. That makes the layout engine harder
than ASCII — JIS X 0208 codepoints are two bytes wide ("multi-byte
characters"), `font.c` has to recognise the lead-byte ranges
(`0x81…0x9F`, `0xE0…0xFC`) and pair them with the trail byte before
indexing the glyph table. Half-width katakana and ASCII fall back to
the single-byte path.

## The KCB handle

Every text consumer (menu, jimaku, debug, codec) allocates a `KCB`
that holds its layout state — clip rect, line/character spacing, tab
width, color, and a *backing buffer* the rasteriser writes its
emitted polys into. This indirection lets the radar update its
caption without re-laying-out the heliport menu's text.

```c
KCB kcb;
font_init_kcb(&kcb, &screen_rect, x, y);
font_set_kcb(&kcb, width, height, c_skip, l_skip, t_skip, flag);
font_set_color(&kcb, code, foreground, background);
font_set_buffer(&kcb, malloc(font_get_buffer_size(&kcb)));
font_draw_string(&kcb, x, y, "BIG BOSS の おもいで", FONT_COLOR_WHITE);
font_update(&kcb);   /* commits the buffer to the active OT */
```

`c_skip` (character skip) / `l_skip` (line skip) / `t_skip` (tab
skip) control kerning and line height in PSX texels. `flag` enables
features like *rubi* annotation (small phonetic guides over a kanji)
and centred/right-aligned drawing.

The buffer trick — sizing first with `font_get_buffer_size`, then
allocating, then setting it — exists because the maximum number of
glyphs the KCB ever needs to emit at once is known at layout time
(`width × height / glyph_size`), so the engine can avoid dynamic
allocation in the per-frame `font_print_string` path.

## Font sources

Three font systems coexist:

1. **Always-resident system font** (`init.dar`) — small ASCII +
   hiragana + katakana set used by every menu and the debug
   overlay. Loaded once at boot.
2. **Per-stage kanji font** — each stage's DATACNF includes a `tail`
   file with only the kanji glyphs referenced by that stage's
   dialogue. The runtime calls `font_set_font_addr(FONT_KANJI, …)`
   on stage load and `font_free` on stage exit.
3. **Codec font** — the codec radio system uses its own retro-pixel
   font rendered through `source/menu/radio.c`. Not this folder.

`font_set_font_addr(type, addr)` is the seam between the loader and
the rasteriser — it lets the same code render text whether the kanji
glyphs came from the stage tail, the always-resident font, or a
debug-only set.

## Public API surface

```c
/* Lifecycle */
void  font_load(void);
void  font_set_font_addr(int type, void *addr);
void  font_free(void);

/* Per-control-block */
int   font_init_kcb(KCB *kcb, RECT *clip, int x, int y);
int   font_set_kcb(KCB *kcb, int width, int height,
                   int c_skip, int l_skip, int t_skip, int flag);
void  font_set_color(KCB *kcb, int code, int fore, int back);
int   font_get_buffer_size(KCB *kcb);
void  font_set_buffer(KCB *kcb, void *buf);
void *font_get_buffer_ptr(KCB *kcb);
void  font_set_rubi_display_mode(int flag);

/* Rendering */
long  font_draw_string(KCB *kcb, long x, long y,
                       const char *str, long color);
void  font_print_string(KCB *kcb, const char *str);  /* uses kcb's cursor */
void  font_clear(KCB *kcb);
void  font_update(KCB *kcb);        /* flush KCB buffer into the active OT */
void  font_clut_update(KCB *kcb);   /* re-upload palette (color animation) */
```

`font_draw_string` accepts arbitrary (x, y) ignoring the KCB's
internal cursor; `font_print_string` uses the cursor and advances it
(supports `\n`). Both write into the KCB's backing buffer; nothing
hits VRAM until `font_update` runs.

## Used by

- [`source/menu/jimaku.c`](../../../../source/menu/jimaku.c) —
  cutscene subtitles. The biggest consumer.
- [`source/menu/menuman.c`](../../../../source/menu/menuman.c) —
  HUD: weapon name, item name, ammo count.
- [`source/menu/debug.c`](../../../../source/menu/debug.c) —
  framerate, position, address dumps.
- [`source/menu/radiomes.c`](../../../../source/menu/radiomes.c) —
  codec dialogue (uses font primarily for caller-name labels; the
  body text uses radio's own font).
- [`source/onoda/option/opt.c`](../../../../source/onoda/option/opt.c) —
  the in-game Option screen.
- Stage-specific actors that show on-screen prompts.

## Pitfalls

- The KCB does **not** own its buffer. If a caller forgets to
  `font_set_buffer` after `font_init_kcb`, the next `font_update`
  scribbles past whatever happens to be at the uninitialised pointer.
- `font_update` commits to the *currently active OT* (i.e., the
  caller is responsible for having called `addOTPrim` to link
  before/after the text). Two text systems updating the same KCB in
  one frame will produce overlapping prims.
- Color values are 16-bit RGB555 packed; the helper enums in `font.h`
  cover the common cases (WHITE, YELLOW, RED).
- Multi-byte sequences must not be split across `font_print_string`
  calls — the layout engine recognises lead bytes only when the next
  byte is in the same string. Codec dialogue lines that line-wrap
  mid-kanji manage this by *never* breaking inside a multi-byte
  character.

## See also

- [`source/menu/jimaku.c`](../../../../source/menu/jimaku.c) — the
  biggest example of font use; lays out cutscene subtitle text with
  fade-in / fade-out timing.
- [`tools/mgs_tools/gcl/glyphs/`](../../../../tools/mgs_tools/gcl/glyphs/)
  — Python tooling for editing the stage kanji glyph tables.
- [doc/demo/06-authoring.md](../../demo/06-authoring.md) — how stage
  fonts are referenced in GCL (`-tail font.tail`).
- [_unreversed.md](_unreversed.md) — glyph table on-disk format,
  rubi positioning algorithm, antialiasing flags.

---

## Port notes

The port runs the rasteriser unmodified — it emits standard
POLY_FT4 prims that the OT walker handles. The glyph atlas lives in
VRAM (uploaded by `font_load` / `font_set_font_addr`) and is sampled
by both the software rasterizer and the OpenGL fragment shader
through the normal CLUT path.

One detail worth noting: the system font's glyph atlas occupies the
tpage at `(640, 256)` in PSX VRAM coordinates, and the per-stage
kanji font uses `(640, 384)`. Both regions are 4-bit CLUT textures.
If you're inspecting VRAM in the debug overlay (`MGS_VRAM_DEBUG=1`)
and wondering why the bottom-right of the screen has dense pixel
patterns, those are the glyph atlases.
