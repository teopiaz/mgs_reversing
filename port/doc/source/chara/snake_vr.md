# `chara/snake_vr/` — VR-mission Snake

Stripped-down Snake variant for VR missions (the bonus minigame
where Snake runs sequenced challenges in a polygon-grid void).
Same conceptual structure as `chara/snake/sna_init.c` but **vastly
simplified**:

- No HZD floor / wall queries (the VR room is a flat plane).
- No life / stamina / oxygen.
- No equipment menu (weapon hardcoded per VR mission).
- No cigs / box / bandana.
- No story-state flags.

Just the bare minimum: walk, aim, fire, die-and-restart.

## File map

| File | Lines | Role |
| ---- | ----- | ---- |
| [`sna_init.h`](../../../../source/chara/snake_vr/sna_init.h) | small | `SnaVRInitWork` struct + factory proto. |
| [`sna_init.c`](../../../../source/chara/snake_vr/sna_init.c) | ?? | Factory + Act + simplified action callbacks. |

The variant only ships in the VR-disc build (`SLPM_862.49`, the
Integral VR companion disc) — selected at compile time via the
`VR_EXE` define. The main game ignores this folder entirely; the
binary `--variant=vr_exe` build links it.

## Differences from `chara/snake/`

- `SnaVRInitWork` is much smaller — no `act_pack`, no equipment
  fields, no patrol-route state.
- The Act callback is ~100 lines vs Snake's 600.
- Action set is reduced to: idle / walk / aim / fire / hit /
  reset.
- Damage triggers an instant restart of the VR mission rather
  than a death animation.

## Why a separate folder

VR missions need slightly different physics (no momentum, no
stamina drain) and a slightly different rendering (the VR-disc
uses a wireframe-grid floor that the regular HZD code wouldn't
provide). Keeping this separate lets the team tune VR balance
without touching gameplay Snake.

## See also

- [snake.md](snake.md) — the full Snake.
- [`source/stagevr/`](../../../../source/stagevr/) — VR-specific
  stage code (stages with names like `vr01a`, `vr02a`, …).
- [`source/main/`](../../../../source/main/) — boot path,
  variant selection.
