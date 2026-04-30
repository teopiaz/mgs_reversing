---
file: source/font/ — opaque areas
---

# `font/` — opaque areas

The font system is simple — rasteriser + glyph table. Decompiled.

## Glyph table format

Each font carries a glyph table mapping char-code → (atlas_x,
atlas_y, width, height). The byte layout is reverse-engineered;
the port's tooling
([`tools/mgs_tools/gcl/glyphs/extractor.py`](../../../../tools/))
mirrors it.

## Multi-font

Three font sets: ASCII Latin, Japanese (Shift-JIS subset), large
TELOP. The selection logic (which font draws when?) is per-context
and not formally documented.

## Tofu / fallback

When a glyph isn't in the table, the rasteriser draws "□" (tofu).
The fallback character is hard-coded.

## See also

- [index.md](index.md) — file map.
- [`tools/mgs_tools/gcl/glyphs/`](../../../../tools/) — glyph
  extraction tools.
