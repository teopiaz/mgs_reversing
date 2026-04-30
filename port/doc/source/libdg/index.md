# `source/libdg/` — render pipeline

The PSX-replica rendering library. Owns:

- KMD model loading + the `DG_DEF` / `DG_MDL` / `DG_OBJS` /
  `DG_OBJ` types.
- The PSX vertex pipeline: bound (cull) → trans (world→eye) →
  shade (per-vertex lighting).
- Display channel system (3 channels: main 3D / HUD / radar).
- Texture cache + VRAM management.
- The Ordering Table (OT) primitive sorter.
- Display environment / clip rectangle management.

## Files

### Model + scene graph
| File | Role |
| ---- | ---- |
| [`obj.c`](../../../../source/libdg/obj.c) | `DG_OBJS` allocation / configuration. The "scene-graph node". |
| [`opack.c`](../../../../source/libdg/opack.c) | Packet builder — converts a `DG_OBJ`'s vertices into PSX GPU primitives (POLY_GT4 etc.). |
| [`dgd.c`](../../../../source/libdg/dgd.c) | DG cache loader — handles `'k'` (KMD) cache entries. |
| [`loader.c`](../../../../source/libdg/loader.c) | Cross-cutting load helpers. |

### Vertex pipeline
| File | Role |
| ---- | ---- |
| [`bound.c`](../../../../source/libdg/bound.c) | Frustum culling — quick-reject objs outside view. |
| [`trans.c`](../../../../source/libdg/trans.c) (?) | World→eye vertex transform via GTE. |
| [`shade.c`](../../../../source/libdg/shade.c) | Per-vertex lighting (directional + ambient). |
| [`pshade.c`](../../../../source/libdg/pshade.c) | Pre-shade — alternative path for static-lit objects. |
| [`light.c`](../../../../source/libdg/light.c) | Light source management — `DG_LightMatrix`, light vectors. |
| [`divide.c`](../../../../source/libdg/divide.c) | Polygon subdivision for far-plane handling. |
| [`prim.c`](../../../../source/libdg/prim.c) | DG_PRIM — direct primitives (lines, sprites) outside the model system. |

### Display + screen
| File | Role |
| ---- | ---- |
| [`chanl.c`](../../../../source/libdg/chanl.c) | Channel system — `DG_Chanls[3]`, `DG_LookAt`, view binding. |
| [`display.c`](../../../../source/libdg/display.c) | Display environment, double-buffer swap, V-sync. |
| [`screen.c`](../../../../source/libdg/screen.c) | OT-side per-frame routines — `DG_PutObjs`, `DG_ScreenObjs`. |
| [`matrix.c`](../../../../source/libdg/matrix.c) | Matrix helpers — wrap libgte's GTE matrix ops with DG conventions. |

### Resources
| File | Role |
| ---- | ---- |
| [`palette.c`](../../../../source/libdg/palette.c) | Palette / CLUT management. |
| [`text.c`](../../../../source/libdg/text.c) | Texture management — `DG_GetTexture`, `DG_TEX` table. |
| [`stub.c`](../../../../source/libdg/stub.c) | Tiny stubs (mostly debug). |

## Vocabulary

```
DG_DEF       — a KMD's parsed data (n_models + DG_MDL[] tail).
DG_MDL       — one bone in a KMD (vertices, faces, parent, pos).
DG_OBJS      — render instance for a DG_DEF (world matrix, group,
                 OT linkage, channel binding).
DG_OBJ       — one bone-instance under a DG_OBJS (per-bone xform).
DG_CHANL     — viewport (eye matrix, OT, draw env, clip).
DG_PRIM      — direct primitive (sprite / line / quad — for HUD).
DG_TEX       — texture entry (VRAM coords, CLUT, dimensions).
```

## The render pipeline

End-to-end per frame:

1. Each spatial actor calls `DG_SetPos2(&mov, &rot)` to establish
   its world matrix in the GTE, then `GM_ActObject` (which calls
   `DG_PutObjs` → reads the matrix into `objs->world`).
2. After all actors tick, the engine processes each channel:
   - `DG_BoundChanl` — frustum cull every queued object.
   - `DG_TransChanl` — transform vertices to eye-space.
   - `DG_ShadeChanl` — compute per-vertex RGB.
3. `DG_OPACK` builds POLY_GT4 / POLY_GT3 packets from the
   shaded vertices, queues them in the channel's OT.
4. `DG_PutOTag(GV_Clock)` walks the OT in z-order and calls the
   GPU.
5. `DG_PutDispEnv` swaps to the next double-buffer page; render
   is visible.

## Channels

```
DG_Chanls[0]   main 3D scene — Snake, guards, stage geometry
DG_Chanls[1]   HUD overlays — cinema bars, fade rectangles, JIMAKU
DG_Chanls[2]   radar / map insets
```

Each channel has its own `eye_inv` matrix → independent camera.
The 3D pane uses chanl 0; the HUD draws to chanl 1 with an
identity-ish projection so screen pixels land where expected.

## Per-component deep dives

| Doc | Topic |
| --- | ----- |
| [pipeline.md](pipeline.md) | The 7-stage pipeline — chanl / screen / bound / trans / shade / prim / divide / sort |
| [obj.md](obj.md) | DG_OBJS / DG_DEF / DG_MDL / DG_OBJ data shape, runtime construction, packet building |
| [matrix.md](matrix.md) | Matrix helpers — YXZ vs ZYX, shadow / reflect, GTE inlines |
| [text.md](text.md) | Texture table, tpage/CLUT format, palette FX |
| [display.md](display.md) | Top-of-pipeline driver, render daemon, file-format loaders |
| [_unreversed.md](_unreversed.md) | What's still by-address |

## Used by every renderable thing

Every actor that draws something:

- Allocates a `DG_OBJS` via `DG_MakeObjs(def, flag, chanl)`.
- Queues it via `DG_QueueObjs`.
- Per frame: `DG_SetPos2` + `DG_PutObjs` to update its world
  matrix.

Free path: `DG_DequeueObjs` + `DG_FreeObjs`.

---

## Port notes

The disc's libdg compiles a *PSX GPU command list* that the
hardware GPU consumes. The port replaces this with
[`port/libdg/gl_renderer.c`](../../../../port/libdg/gl_renderer.c)
which:

- Re-implements `DG_BoundChanl` / `DG_TransChanl` /
  `DG_ShadeChanl` in software-on-CPU.
- Submits the resulting eye-space triangles to OpenGL via
  `gl_submit_tri3d`.
- Manages a GL framebuffer + texture cache backed by VRAM.

So most `source/libdg/*.c` files are **bypassed at runtime** in
the port — the GL path takes over after the bound/trans/shade
stage. The headers and API contracts still apply; just the
end-of-pipeline GPU submission differs.

## See also

- [03-control-and-motion.md](../03-control-and-motion.md) — how
  CONTROL.mov + .rot translate into the OBJS world matrix.
- [`port/libdg/gl_renderer.h`](../../../../port/libdg/gl_renderer.h)
  — the port's GL replacement.
- [`source/include/libdg/libdg.h`](../../../../source/libdg/libdg.h)
  — the canonical type definitions.
