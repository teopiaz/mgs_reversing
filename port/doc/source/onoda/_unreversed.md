---
file: source/onoda/ — opaque areas
---

# `onoda/` — opaque areas

Onoda's per-author folder hosts the option screen, demo selector,
and pre-OPE (pre-opening) sequence. Decompiled.

## Pre-opening (preope)

The pre-OPE actor handles the boot sequence: company logos →
license screens → main menu. The state machine is decompiled but
the per-state durations and asset bindings are hard-coded.

## Option screen layout

The option screen has multiple sub-pages (controls / display /
sound / language). The cursor position + per-option ranges are
inferred from code; not formally tabulated.

## Demosel — demo selector

The "demo select" sub-screen lists available stages for VR /
demo mode. Layout and entry list are per-build (Integral has more
demos than original MGS). Filter logic isn't documented.

## See also

- [README.md](README.md) — file map.
