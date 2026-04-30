---
file: source/weapon/{aam,bomb,famas,grenade,mine,rcm,rfsight,rifle,socom,stnsight}.c + weapon.h
---

# `source/weapon/` — weapon actors

Per-weapon actor implementations. Each weapon is its own `.c` file
following the same template: hold state struct, actor callback,
fire, reload, equip/unequip.

## File map

| File | Weapon | Notes |
| ---- | ------ | ----- |
| `socom.c` | SOCOM (silenced pistol) | The starter weapon |
| `famas.c` | FAMAS (assault rifle) | Mid-game pickup |
| `rifle.c` | PSG-1 (sniper rifle) | Sniper used in s14a |
| `rfsight.c` | (Rifle sight) | Sniper-scope overlay |
| `stnsight.c` | (Stun sight) | Stun-scope overlay |
| `grenade.c` | Grenade | Lobbed projectile |
| `bomb.c` | C4 | Plant + remote-detonate |
| `rcm.c` | RC-M (remote-controlled missile) | Requires control mode |
| `aam.c` | (Air-to-air missile?) | Boss-fight tool |
| `mine.c` | Claymore mine | Pickup-only (Snake doesn't deploy) |

## Weapon work struct

```c
typedef struct WeaponWork {
    GV_ACT  actor;
    OBJECT  obj;             // weapon model attached to player's hand
    short   ammo;
    short   capacity;
    short   firing;          // 1 if actively firing
    short   recoil_t;        // recoil animation timer
    SVECTOR muzzle_pos;
    // weapon-specific fields
} WeaponWork;
```

Each weapon has its own variant; the prefix is shared.

## Lifecycle

```
Equip:   GM_EquipWeapon(SOCOM) → spawn SOCOM actor + attach to hand
Fire:    pad press B → actor's act() spawns bullet/blast
Reload:  ammo == 0 + B held → reload animation, ammo = capacity
Unequip: actor.die() → remove from hand
```

## Player → weapon dispatch

Player's `chara/snake/` reads `GM_EquippedWeapon` and routes:

- Pad B (fire button) → call `WEAPON_Fire(eq_id)` → that weapon's
  actor consumes input next frame.
- Pad reload → `WEAPON_Reload(eq_id)`.

Each weapon actor is "always on" while equipped; switch unequipping
destroys it.

## Common patterns

### Firing

Each fire spawns a projectile actor:

| Weapon | Spawned actor |
| ------ | ------------- |
| SOCOM | `bullet/blast.c` (silenced) |
| FAMAS | `bullet/blast.c` (loud) |
| PSG-1 | `bullet/blast.c` (long-range) |
| Grenade | `bullet/bakudan.c` |
| C4 | `bullet/bakudan.c` (no fuse — remote) |
| RC-M | `bullet/rmissile.c` |

### Recoil

A small `recoil_t` countdown timer drives the back-tilt animation
on the player. Each fire sets `recoil_t = N`; per-frame decrement;
recoil stops when 0.

### Muzzle flash

`enemy/glight.c::NewGunLight` is called per-fire with the muzzle
position matrix; spawns a 3-frame yellow quad at the muzzle.

## Special weapons

### Sniper rifle (rifle.c + rfsight.c)

`rfsight.c` is a separate sub-actor: when the player aims the
PSG-1, `rfsight` activates and renders a circular scope overlay
in `DG_Chanl(1)`. Movement input becomes scope-pan.

### Stun gun

`stnsight.c` is the stun-rifle scope — same sub-actor pattern but
red-tinted scope overlay.

### C4 (bomb.c)

C4 has two states:

1. *Planted*: spawn a `bullet/bakudan.c` with `fuse = -1` (manual).
2. *Detonate*: `mesg DETONATE` → all planted C4 receives → fuse=0
   → explode.

So C4 is a remote network triggered by a single mesg.

### RC-M (rcm.c)

When fired, the player switches to *control-mode* — the player's
chara is paused, RC-M actor consumes pad input. Fly the missile
into target. Crash or detonate ends control-mode.

## Pitfalls

- **Don't equip two weapons at once.** Most weapons assume sole
  ownership of the hand bone. Equipping a second corrupts the
  attachment.
- **Ammo persists across stages.** The weapon's `ammo` is saved
  separately from inventory; reloads/refills go through
  `weapon.h` API.
- **C4 detonate broadcasts to *all* planted C4 in the stage.** No
  way to selectively detonate one.

## See also

- [`source/bullet/`](../bullet/index.md) — projectile actors.
- [`source/menu/weapon.c`](../menu/menuman.md) — weapon-tab UI.
- [`source/chara/snake.md`](../chara/snake.md) — player + ACTPACK.
- [`source/equip/`](../equip/index.md) — wearable items
  (different from weapons).
