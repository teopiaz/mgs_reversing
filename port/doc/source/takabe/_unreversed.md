---
file: source/takabe/ — opaque areas
---

# `takabe/` — opaque areas

Most files in `takabe/` are 100% decompiled but their gameplay
balance / animation curves use unnamed magic constants.

## `unknown4.c`

Literal name. ~? lines, registered as a level-5 actor. Function
signature is decompiled but the *purpose* is opaque. Possibly an
unused feature.

## `dymc_seg.c` (takabe vs enemy)

Two `dymc_seg.c` exist:

- `enemy/dymc_seg.c` (used by meryl7 for bathroom can).
- `takabe/dymc_seg.c` (used by takabe-authored stages).

They have similar APIs but slightly different signatures. Why two
parallel implementations isn't documented; possibly a code-style
disagreement between authors.

## `rasen.c` / `rasen_el.c`

"Rasen" (螺旋 = spiral). Some kind of spiral / spring actor. Used
in unidentified stage. Reverse-engineered code is decompiled but
the gameplay role is unclear.

## `wt_area.c` / `wt_area2.c`

Two water-area implementations side by side; difference is unclear.
Both register triggers; both modify `control->attribute` for
underwater swimming. Possibly version 2 with bug fixes.

## `panel.c` / `prim.c` / `put_obj.c`

Generic helpers — wrappers around DG_PRIM submission. Their
specialisation over the libdg primitives isn't documented.

## See also

- [README.md](README.md), [effects.md](effects.md) — documented
  surfaces.
