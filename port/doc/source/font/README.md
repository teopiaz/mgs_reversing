# `source/font/` — font tables and glyph rasteriser

Two files. Compact subsystem responsible for converting text
strings into rendered polygons.

| File | Purpose |
| ---- | ------- |
| [`font.c`](../../../../source/font/font.c) | The rasteriser — takes a string + a glyph table and emits PSX prims to a chanl OT. |
| [`font.h`](../../../../source/font/font.h) | Glyph table struct + public API. |

## The job

PSX has no built-in text rendering. Every character on screen
(menu labels, codec subtitles, JIMAKU dialogue, debug overlays)
is a polygon — typically a textured quad with UV coordinates
into a font texture.

`font.c` provides:

- `Font_Init(font_handle)` — pick which font to use.
- `Font_DrawString(x, y, str, ot)` — render a string to the OT
  at screen coords.
- `Font_StringWidth(str)` — compute pixel width.
- Multi-byte support for Japanese kanji via the stage-font
  glyph table (each stage's font.tail file holds the glyph
  shapes for kanji used in that stage's dialogue).

## Font sources

Three font systems coexist:

1. **Always-resident system font** — small ASCII / hiragana /
   katakana set bundled into `init.dar`. Used for menus, debug.
2. **Per-stage font** — each stage's DATACNF includes a `tail`
   file with the kanji glyphs needed for that stage's dialogue.
   The runtime swaps fonts on stage load.
3. **Codec font** — the codec call has its own retro-style font
   rendered via the radio system, not this folder.

## See also

- [_unreversed.md](_unreversed.md) — opaque areas (glyph table
  format, multi-font selection, fallback character).
- [`source/menu/jimaku.c`](../../../../source/menu/jimaku.c) —
  the subtitle actor uses this rasteriser.
- [`tools/mgs_tools/gcl/glyphs/`](../../../../tools/mgs_tools/gcl/glyphs)
  — Python tooling for editing stage-font glyphs.
- [doc/demo/06-authoring.md](../../demo/06-authoring.md) —
  how stage fonts are referenced in GCL.
