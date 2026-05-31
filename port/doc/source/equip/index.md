# `source/equip/` — wearable equipment

The items Snake *wears* or *holds in his off hand*, as distinct from
the weapons in `source/weapon/` (which are *fired*) and the inventory
items in `source/menu/item.c` (which are *picked or used*). Each
equipment file implements one actor that comes alive while the
matching item is equipped, modifies Snake's behaviour or rendering,
and dies cleanly when the player un-equips.

## File map

| File | Item | Notes |
| ---- | ---- | ----- |
| [`bandana.c`](../../../../source/equip/bandana.c) | Bandana | Post-clear unlock; infinite ammo while worn |
| [`bodyarm.c`](../../../../source/equip/bodyarm.c) | Body armor | Halves incoming damage |
| [`box.c`](../../../../source/equip/box.c) | Cardboard box | Stealth + truck-fast-travel; five labelled variants (BOX_01…05) |
| [`gasmask.c`](../../../../source/equip/gasmask.c) | Gas mask | Negates gas-room damage; spawns the sight overlay |
| [`gmsight.c`](../../../../source/equip/gmsight.c) | Gas-mask sight | 1st-person vignette while the gas mask is up |
| [`gglmng.c`](../../../../source/equip/gglmng.c) | Goggles dispatcher | Watches `GM_CurrentItemId`, spawns NVG or thermal child |
| [`gglsight.c`](../../../../source/equip/gglsight.c) | Goggle sight | 1st-person tint + palette swap; shared by NVG and thermal |
| [`scope.c`](../../../../source/equip/scope.c) | Binoculars | Plain magnified-view sight |
| [`kogaku2.c`](../../../../source/equip/kogaku2.c) | Stealth (Optical Camo) | Boss-fight bonus; uses framebuffer-readback to refract the scene through Snake's silhouette |
| [`jpegcam.c`](../../../../source/equip/jpegcam.c) | Photo camera | The disc-2 unlock; captures the framebuffer to RAM, exports JPEG |
| [`tabako.c`](../../../../source/equip/tabako.c) | Cigarette | Cosmetic — IR-beam reveal, slow HP drain |
| [`effect.c`](../../../../source/equip/effect.c) | Shared VFX | Splash/glare helpers reused across the above |

## The "equip actor" pattern

Every entry above is one actor that follows the same shape:

```c
typedef struct {
    GV_ACT actor;      /* libgv actor header */
    OBJECT *parent;    /* Snake's CONTROL/OBJECT chain — read-only */
    /* …per-item state — timers, child actors, palette backup… */
} Work;

void *NewGasMask(CONTROL *control, OBJECT *parent, int num_parent)
{
    Work *w = GV_NewActor(GV_ACTOR_LEVEL, sizeof(Work));
    GV_SetNamedActor(&w->actor, Act, Die, "gasmask.c");
    /* …attach to Snake, set initial state… */
    return w;
}

static void Act(Work *w) {
    /* …per-frame: read messages from Snake, update state, render… */
}

static void Die(Work *w) {
    /* …unwind: restore palette, kill child sight actor, release model… */
}
```

The spawning happens from `chara/snake/sna_init.c`'s `CurrentItem`
function-pointer table — each `IT_*` ID maps to a `New*` constructor.
When the player un-equips (via the menu) or `chara/snake/sna_*.c`
detects a state change, Snake sends `HASH_KILL` to the equip actor
and it tears down through `Die`.

## Notable members

### Cardboard box (`box.c`)

The most loved item gets the most code (~600 lines). While the box is
equipped:

- Snake's body KMD is hidden, the box KMD is parented in its place.
- Movement is gated to a slow crawl; the box "wobbles" via a sine on
  rotation Z.
- `TARGET` semantics flip — guards' vision cones see "a box", not
  "Snake", and the player's gun is unusable.
- Picking up the box on a flatbed truck route triggers a fast-travel
  cutscene: the box variants (BOX_01…05) each carry a destination
  label, and walking onto the truck flatbed posts a stage-load
  request keyed by that label.

The implementation runs in two layers — `box.c` itself handles
visuals and animation; the truck-pickup hook lives in
`source/stage/<truck-stage>/` because each truck stage knows which
boxes it accepts.

### Gas mask (`gasmask.c` + `gmsight.c`)

Two-actor split. `gasmask.c` is the gameplay actor: it sits in the
priority-3 actor queue, polls Snake's stage area against a "gas
room" flag, and absorbs the per-frame damage tick that would
otherwise be applied to HP. `gmsight.c` is a separate priority-0
actor that owns the 1st-person binocular vignette — it sets a global
`word_800BDCC0` while alive (which the port renderer reads to draw
the widescreen black bars), and tears down when the gas mask is
holstered.

### Goggle dispatcher (`gglmng.c`)

NVG and thermal share one front-end. `gglmng.c` is the dispatch
actor — it watches `GM_CurrentItemId` and `GM_GameStatus`, spawns the
right `gglsight.c` child for the active mode, and handles the
"goggles auto-on at dawn" cinematic trigger. The actual palette swap
that produces the green-NVG or red-thermal tint lives in
`gglsight.c`, which fires `LoadImage2` / `StoreImage2` callbacks on
the CLUT region to remap the scene's colors per-frame.

### Stealth (`kogaku2.c`)

The Optical Camo cheat (post-clear unlock). It's a 3D actor parented
to Snake that draws Snake's silhouette polys with their tpage
pointing into the *displayed framebuffer* region of VRAM — the GPU
samples the framebuffer through a `(3/4)*x + 160` horizontal lensing
to produce the "you can see the world through Snake" refraction. The
port detects this pattern in `gl_renderer.c` via
`port_is_fb_readback_tpage` and routes the prims to a dedicated
shader path that captures the FBO mid-frame (before Snake draws) and
samples it with the same lensing.

### Photo camera (`jpegcam.c`)

A disc-2 unlock — Snake takes pictures and brings them to Otacon for
unlock content. The actor swaps Snake's idle pose to the camera-up
pose, runs a 1st-person view with a viewfinder overlay (from
`gglsight.c`'s primitive table), and on shutter button reads the
framebuffer back into a heap buffer. The release game then encodes
JPEG via `source/koba/jpeg_*.c` (not yet reverse-engineered in
detail).

## Lifecycle

```
                          inventory cursor on item
                                 │
                                 ▼
         menu/item.c::Use ── posts message to Snake
                                 │
                                 ▼
                 chara/snake/sna_init.c::CurrentItem table
                                 │
                            New*(…)
                                 │
                                 ▼
                           equip actor live
                                 │
                  ┌─────────────┴──────────────┐
       per-frame: mod Snake state    per-frame: render
                                 │
                            HASH_KILL   ◄── un-equip / stage change
                                 │
                            Die() → free
```

## See also

- [chara/snake.md](../chara/snake.md) — `SnaInitWork.current_item`
  drives which `New*` runs.
- [`source/menu/item.c`](../../../../source/menu/item.c) — the
  inventory UI that fires the message.
- [`source/weapon/`](../weapon/index.md) — *fired* items (guns,
  grenades) live there; the line between "equip" and "weapon" is
  whether the press of `△` (FPV) keeps Snake's left hand on it.
- [_unreversed.md](_unreversed.md) — per-equip stat tables, box
  truck-destination labels, jpegcam encoder internals.

---

## Port notes

The port runs every equip actor unmodified — the engine doesn't need
porting for these. Two places where the renderer plumbing matters:

- **`gmsight.c` → `word_800BDCC0`** is the signal `gl_renderer.c`
  uses to draw the widescreen black bars over the pillarbox extras
  while the gas mask is up. Other sights (scope, NVG, rifle scope,
  stinger, binoculars, cardboard box 1st-person view) set
  `dword_8009F604` to the active sight type instead — the renderer
  could use that for the same treatment if you want them all blacked
  out. See [`04-rendering.md`](../../04-rendering.md) §7.4.
- **`kogaku2.c` → tpage-into-display-region** triggers the
  fb-readback flag bit in `gl_submit_tri3d`; the 3D fragment shader
  branches into the Optical Camo sample path. See
  [`04-rendering.md`](../../04-rendering.md) §7.3.

`jpegcam.c` is partly stubbed — the JPEG encoder is unported, but the
shutter does write the captured framebuffer to a heap RGBA buffer,
and the photo-export hook lives in `port/photo_export.c` so the user
can still trigger a real PNG dump from the same button if the encoder
is bypassed.
