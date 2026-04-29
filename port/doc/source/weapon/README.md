# `source/weapon/` — weapon actors

Each weapon Snake can fire is one actor here. Spawned when Snake
equips the weapon; lives until he switches; runs the per-frame
firing logic + visual effects.

## Files

| File | Weapon | Spawns |
| ---- | ------ | ------ |
| [`socom.c`](../../../../source/weapon/socom.c) | SOCOM pistol | hitscan + `AN_Unknown_800D6898` muzzle flash |
| [`famas.c`](../../../../source/weapon/famas.c) | FAMAS rifle | hitscan + `kojo/famaslit.c` muzzle effect |
| [`rifle.c`](../../../../source/weapon/rifle.c) | Rifle (PSG-1 sniper) | hitscan + sniper-scope mode |
| [`grenade.c`](../../../../source/weapon/grenade.c) | Hand grenade | spawns `bullet/tenage.c` |
| [`bomb.c`](../../../../source/weapon/bomb.c) | C4 explosive | spawns `bullet/bakudan.c` |
| [`mine.c`](../../../../source/weapon/mine.c) | Claymore mine | spawns `bullet/jirai.c` |
| [`rcm.c`](../../../../source/weapon/rcm.c) | Remote-controlled missile | spawns `bullet/rmissile.c`, suspends Snake |
| [`aam.c`](../../../../source/weapon/aam.c) | AAM (anti-aircraft missile, anti-Hind) | spawns guided missile |
| [`stnsight.c`](../../../../source/weapon/stnsight.c) | Stun sight (auxiliary aim mode) | overlay |
| [`rfsight.c`](../../../../source/weapon/rfsight.c) | Rifle sight (sniper scope overlay) | overlay |
| [`scope.c`](../../../../source/weapon/scope.c) | Scope (alternative or for goggles) | overlay |

## Pattern — every weapon is the same shape

```c
typedef struct WeaponWork {
    GV_ACT actor;
    OBJECT *body;       /* parent OBJECT (Snake) */
    int    state;
    int    fire_cooldown;
    int    ammo;
    /* …weapon-specific state… */
} WeaponWork;

static void Act(WeaponWork *work) {
    if (work->fire_cooldown > 0) {
        work->fire_cooldown--;
        return;
    }
    if (player_pulled_trigger) {
        if (work->ammo > 0) {
            spawn_projectile_or_hitscan();
            spawn_muzzle_flash();
            play_fire_sound();
            work->ammo--;
            work->fire_cooldown = FIRE_RATE;
        }
    }
}

void *NewSocom(int name, int where, int argc, char **argv) {
    WeaponWork *work = GV_NewActor(LEVEL, sizeof(WeaponWork));
    /* …init… */
    return work;
}
```

## Hitscan vs projectile

- **Hitscan weapons** (SOCOM, FAMAS, rifle): the bullet's path is
  computed instantly via `HZD_LineCheck`. No `bullet/` actor
  spawned. Visual is just a smoke trail + impact spark. Fast
  enough that "bullet travel time" isn't visible at typical
  ranges.

- **Projectile weapons** (grenade, bomb, mine, RCM, AAM): spawn a
  `bullet/<thing>.c` actor that runs a multi-tick trajectory.

## Sound + alert

Every fire sound calls `GM_AlertSound(snake_pos, sound_id)`. The
alert system records the noise, and any guard within hearing
range reacts on its next `Check` tick. This is the "guards hear
your gunshot" mechanic.

## Aim mode

Most weapons have an aim mode (separate state):

- **Hip-fire** — quick shot, no scope, default state.
- **Aim-stand** — Snake stops, raises weapon, smaller crosshair.
- **First-person** — for sniping; Snake disappears, view becomes
  scope.

The state transitions are driven by Snake's pad input mirror
(see [snake.md](../chara/snake.md)).

## Sniper scope

`rifle.c` + `scope.c` + `rfsight.c` cooperate for the PSG-1's
first-person scope view:

- `rifle.c` runs the weapon-actor logic.
- `scope.c` switches the camera to first-person.
- `rfsight.c` draws the scope overlay (crosshair, breath sway).

Special handling: while in scope mode, Snake takes input
differently (analog-like crosshair drift, breath holding via L1).

## Used by

- `chara/snake/sna_init.c` — Snake's `current_weapon` field
  triggers the right `New<Weapon>` factory.
- `chara/snake/sna_init.c::sna_swap_weapon` — switching mid-game
  destroys the old weapon actor and spawns the new.
- `animal/meryl72/meryl72.c` — Meryl spawns her own Desert Eagle
  via similar logic.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [weapons.md](weapons.md) | Per-weapon catalogue, fire/reload flow, hitscan vs projectile |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## See also

- [`source/bullet/`](../bullet/README.md) — projectile actors
  spawned by these weapons.
- [`source/equip/`](../equip/README.md) — equipment items
  (vs *weapons* — equip is worn, weapons are fired).
- [`source/menu/weapon.c`](../../../../source/menu/weapon.c) —
  weapon-tab inventory UI.
- [`source/anime/effect/socom.c`](../../../../source/anime/effect/socom.c)
  — SOCOM-specific muzzle / shell-eject effects.
