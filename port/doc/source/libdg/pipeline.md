---
file: source/libdg/{dgd,chanl,screen,bound,trans,shade,prim,divide,sort}.c
---

# `libdg/` — the render pipeline

The MGS rendering library implements a **fixed 7-stage pipeline**
that runs once per frame for each visible *channel*. Each stage is a
function pointer; each channel runs the full pipeline in order.

```
   per channel, per frame:
   ┌───────────┐    ┌───────────┐    ┌───────────┐    ┌───────────┐
   │ 0 Screen  │ →  │ 1 Bound   │ →  │ 2 Trans   │ →  │ 3 Shade   │
   │ project   │    │ cull      │    │ transform │    │ light     │
   └───────────┘    └───────────┘    └───────────┘    └───────────┘
                                                            │
                          ┌─────────────────────────────────┘
                          ▼
   ┌───────────┐    ┌───────────┐    ┌───────────┐
   │ 4 Prim    │ →  │ 5 Divide  │ →  │ 6 Sort    │ → submit to GPU
   │ packets   │    │ subdivide │    │ insert OT │
   └───────────┘    └───────────┘    └───────────┘
```

## Channel system — `chanl.c`

```c
typedef struct DG_CHANL {
    u_long   *ot[2];          // ordering tables, double-buffered
    short     ot_size;        // log2(OT depth)
    short     link;           // index of the channel to link OUT-tag into
    short     dblbuf;
    short     dirty;
    MATRIX    eye_inv, eye;   // view/inverse matrices
    short     clip_distance;
    short     queue_size;
    short     prim_index;     // queue counter — primitives count down
    short     objs_index;     // queue counter — objs count up
    DG_OBJS **queue;          // shared queue: objs at [0..objs_index],
                              //               prims at [queue_size..prim_index]
    RECT      clip_rect, new_clip_rect;
    DR_ENV    env1[2], env2[2], new_env[2];
} DG_CHANL;
```

`DG_Chanls[3]` (background / main / overlay) — most code refers to
them as `DG_Chanl(0)` / `DG_Chanl(1)` / `DG_Chanl(2)` via the inline
helper.

| Channel | Typical content |
| ------- | --------------- |
| 0 — bg / world | Stage geometry, characters, world-space effects |
| 1 — overlay | HUD, life bar, radar |
| 2 — extra | Codec portrait, debug |

Each frame `DG_RenderPipeline(idx)` runs the 7 stage functions
back-to-back on `DG_Chanl(idx)`.

### Queue: shared array, both directions

`chanl->queue` is one array used as **two stacks** that grow
toward each other:

```
[0]               objs_index    prim_index           queue_size
 ↓                ↓             ↓                    ↓
 [DG_OBJS*][DG_OBJS*]...        [DG_PRIM*][DG_PRIM*]...
 ↑                              ↑
 grows up                       grows down
```

`DG_QueueObjs` increments `objs_index`; `DG_QueuePrim` decrements
`prim_index`. Out-of-room when they meet.

## Stage 0 — `DG_ScreenChanl` (screen.c)

For each `DG_OBJS` in the queue:

1. Compose `world * objs->root` into `screen` matrix.
2. For each child `DG_OBJ`:
   - Compose `world * obj->parent.world * obj->local` into
     `obj->screen`.
   - Apply `objs->rots` (joint angles) and `objs->movs` (joint
     translations) if non-NULL.
3. Apply `light` matrix for next stage.

This is where MOTION_CONTROL's `rots` array drives joint
animation: the array indexes per-joint Euler triples that get
multiplied into `obj->screen` here.

For DG_PRIMs, only the prim's `world` matrix is set up — they have
no children.

## Stage 1 — `DG_BoundChanl` (bound.c)

Frustum + screen-rectangle culling. For each `DG_OBJS`:

- If `flag & DG_FLAG_INVISIBLE`: skip.
- Project the AABB (`def->min` / `def->max`) into screen space using
  the screen matrix.
- If projected rect is fully outside `chanl->clip_rect`: mark
  invisible for this frame, skip remaining stages.

Per-OBJ bounding (`flag & DG_FLAG_BOUND`) is finer-grained — each
`DG_OBJ`'s AABB is tested individually (used by skeletal characters
where the body is large but a single arm might be off-screen).

## Stage 2 — `DG_TransChanl` (trans.c)

Vertex transformation pass. For each visible OBJ:

- Load model's vertex array.
- Apply screen matrix via GTE `gte_rtv0/1/2` (vertex transform).
- Z-test for backface culling (skip back-facing if not BOTHFACE).
- Write transformed verts to scratch (`DG_RVECTOR` array).

Trans uses inline GTE macros — see `DG_MulRotMatrix0` /
`DG_CompMatrix` macros in libdg.h. These do matrix composition
without updating the GTE's "current" matrix register, which is
needed because the per-OBJ matrices are queued up.

## Stage 3 — `DG_ShadeChanl` (shade.c) + `pshade.c` + `light.c`

Per-vertex lighting. Two sub-paths:

| Path | When | What |
| ---- | ---- | ---- |
| Real shade (`shade.c`) | `flag & DG_FLAG_SHADE` | GTE `gte_ncs` per vertex against `DG_LightMatrix` |
| Preshade (`pshade.c`) | computed once, reused | `DG_MakePreshade` builds a CVECTOR table; later just looks up |

Preshade is the optimisation path: characters with a static lighting
matrix call `DG_MakePreshade` once at construction; every render
just reads the precomputed colour. Real-shade is for moving lights.

Light-related globals:

```c
extern MATRIX DG_LightMatrix;       // 3x3 light direction matrix
extern MATRIX DG_ColorMatrix;       // light colour matrix
extern SVECTOR DG_Ambient;          // background colour
```

## Stage 4 — `DG_PrimChanl` (prim.c)

Prepares POLY_GT4 / POLY_FT4 / SPRT / TILE primitives. For each OBJ:

- Allocate packet from PACKET0/1 heap (`GV_AllocMemory(GV_PACKET_MEMORYx, n)`).
- Fill UV coords (`DG_WriteObjPacketUV`), CLUT, tpage.
- Apply per-vertex colour (from shade stage) into the packet.

For DG_PRIMs (sprites, lines, single quads), `prim->handler` is the
function that builds the PSX packet — different handler per
`DG_PRIM_TYPE`.

## Stage 5 — `DG_DivideChanl` (divide.c)

Triangle/quad subdivision for *near* polygons. PSX has affine
texture mapping (no perspective correction), so a large textured
poly close to the camera shows the warp. Divide subdivides such a
poly into smaller pieces where the warp is less visible.

Heuristic: poly's screen-space size > threshold → split until small
enough or recursion depth exceeded.

## Stage 6 — `DG_SortChanl` (sort.c)

The final stage — for each prepared primitive, compute its average
Z and `addPrim` into the channel's ordering table at the matching
slot. PSX GPU draws back-to-front by walking the OT.

```c
ot_index = (avg_z >> ot_size) clamped [0, 1<<ot_size - 1]
```

Smaller `ot_size` = coarser sort = faster but more z-fighting.
Channel 0 (world) uses depth ~10 (1024 slots); HUD channels use
much smaller.

## After all 7 stages: `DG_DrawOTag`

Once the OT is filled, `DG_DrawOTag(idx)` calls `DrawOTag2` (PSX BIOS)
on `chanl->ot[GV_Clock]` to begin GPU consumption. The GPU walks
the OT slot-by-slot, drawing primitives into the backbuffer.

## `DG_PutObjs` / `DG_PutPrim` — submitting work

The standard "draw this thing" pattern:

```c
void character_act(Work *w) {
    // ... animation update ...
    DG_PutObjs(&w->body);    // queue body for rendering
    DG_PutObjs(&w->weapon);
}
```

Internally:

```c
DG_PutObjs(objs):
    DG_SetCurrentMatrix(&objs->world);   // set GTE ROT+TR
    DG_QueueObjs(objs);                  // append to current channel queue
```

The channel index is read from `objs->chanl` — set at construction
via `DG_MakeObjs(def, flag, chanl_idx)`.

## Globals

| Symbol | Where | Role |
| ------ | ----- | ---- |
| `DG_Chanls[3]` | bss | The three channels |
| `DG_LightMatrix` | bss | Current scene-light matrix |
| `DG_ColorMatrix` | bss | Current scene-light colour |
| `DG_FrameRate` | bss | 30 or 60, depending on PAL/NTSC |
| `DG_HikituriFlag` | bss | "Hold previous frame" flag (during pause) |

## Pitfalls

- **Don't queue an OBJS on two channels.** The queue pointers in
  the OBJS aren't separated per-channel; queueing twice corrupts.
- **PACKET0/1 heaps are per-frame.** Don't hold pointers into them
  across V-blank; they get wiped.
- **OT depth tradeoff.** Channels with shallow OTs (radar, life
  bar) sort coarsely; pulling something complex into them causes
  flicker.
- **Bound stage skips children of skipped parents.** If you set
  `objs->flag |= INVISIBLE`, children of that OBJS never run any
  later stage — useful, but means per-OBJ visibility flags don't
  override.
- **Crash safety in port:** `DG_FreeObjs` zeroes `objs->n_models`
  in PORT_BUILD so a stale queue ref won't recurse into freed
  data.

## See also

- [obj.md](obj.md) — DG_OBJS / DG_DEF / DG_MDL data shapes.
- [matrix.md](matrix.md) — matrix helpers used through the
  pipeline.
- [text.md](text.md) — texture binding (filled into packets).
- [display.md](display.md) — `DG_SwapFrame` driver.
