---
file: source/libdg/obj.c + opack.c
---

# `libdg/obj.c` + `opack.c` — DG_OBJS / DG_DEF / DG_MDL

The data structures the renderer eats. `obj.c` builds the runtime
representation; `opack.c` builds GPU packets.

## Static (file) hierarchy

A `.kmd` file (loaded by `DG_LoadInitKmd` in `loader.c`) defines a
**model** — one or more meshes that share textures + skeletal
relationships:

```
DG_DEF                       // model header
├── n_models
├── n_visible
├── min/max (AABB)
└── DG_MDL[n_models]         // per-mesh
    ├── flags
    ├── n_faces
    ├── min/max (AABB)
    ├── pos                   // local pivot
    ├── parent                // index of parent mesh (or self)
    ├── extend                // index of "extend" mesh (LOD?)
    ├── n_verts / vertices    // SVECTOR[]
    ├── vindices              // index buffer
    ├── n_normals / normals
    ├── nindices
    ├── texcoords             // u8[n_faces*4]
    └── materials             // u16[] hashed texture names
```

Multiple actors can share the same `DG_DEF` — it's read-only data
loaded once into the file cache, then *instanced* into runtime
`DG_OBJS` per actor.

## Runtime hierarchy

```
DG_OBJS                     // one per visible character/object
├── world                   // root MATRIX
├── root                    // MATRIX *root  (parent matrix or NULL)
├── def                     // pointer to shared DG_DEF
├── flag                    // INVISIBLE | SHADE | BOUND | ...
├── group_id                // shared by all OBJS in same logical group
├── n_models                // == def->n_visible at construction
├── chanl                   // index 0..2
├── light                   // MATRIX *light
├── rots, adjust, waist_rot // SVECTOR* — joint Euler angles
├── movs                    // SVECTOR* — joint translations
└── DG_OBJ objs[n_models]   // per-mesh runtime state
    ├── world               // computed: parent * local
    ├── screen              // computed: world * eye
    ├── model               // pointer back into shared DG_MDL
    ├── rgbs                // CVECTOR* (preshade results)
    ├── extend              // pointer to "extend" sibling OBJ
    ├── bound_mode
    ├── free_count, raise
    ├── n_packs             // POLY_GT4 count
    └── packs[2]            // current and last frame's packet pointers
```

Per-OBJ has *its own* world matrix because skeletal animation needs
a hierarchical chain (parent's transform * local rotation/translation).
The `parent` index in DG_MDL determines the chain.

## `DG_MakeObjs(def, flag, chanl_idx)`

Allocates and links:

```c
DG_OBJS *DG_MakeObjs(DG_DEF *def, int flag, int chanl) {
    int size = sizeof(DG_OBJS) + sizeof(DG_OBJ) * def->n_models;
    DG_OBJS *objs = GV_Malloc(size);  // normal heap
    GV_ZeroMemory(objs, size);
    objs->world = DG_ZeroMatrix;
    objs->def = def;
    objs->n_models = def->n_visible;
    objs->flag = flag;
    objs->chanl = chanl;
    objs->light = &DG_LightMatrix;        // default scene light
    for each child mesh:
        obj->model = mdl;
        obj->extend = resolve(mdl->extend);   // link to sibling
        obj->raise = compute_raise(mdl);      // see below
        obj->n_packs = mdl->n_faces;
    return objs;
}
```

### `extend` — pointer-as-index

`mdl->extend` is an `intptr_t` cast to pointer. On PSX it was an
int. **On 64-bit the sign-extend is wrong**, so the port casts
back via `(int)(intptr_t)model->extend`. If the index is out of
range (≥ `n_models`) or self-referential, `obj->extend = NULL`
(meaning "no extension").

`extend` chains are used for LOD: a mesh's extend points to a
lower-poly variant; the renderer can swap them based on distance.

### `raise` — depth-bias offset

`DG_MakeObjs_helper(mdl)` computes a small Z bias from the model's
flags:

```c
flags & 0x300 != 0 → mask = 4 - (flags >> 12 & 3)
                     val  = mask * (flags & 0x100 ? +0xFA : -0xFA)
```

Used by sort stage as a per-model OT bias. Lets a UI element be
forced in front of the world even if its true Z is the same.

## `DG_FreeObjs(objs)`

Walks each OBJ, frees its `packs[0]` and `packs[1]` packets
(per-frame allocations), drops the preshade table, then `GV_Free`s
the OBJS.

In PORT_BUILD, before freeing, zeroes out the `objs->objs[]` array
and sets `n_models = 0` defensively. Stale queue references (PSX
behaviour: dangling pointer is fine until written) become
explicit-skip on the port.

## Object packets — `opack.c`

Each renderable face becomes a GPU primitive packet
(`POLY_GT4` for textured/shaded quad, `POLY_FT4` for textured/flat,
etc.). `opack.c` is the packet builder.

### Per-OBJ pipeline

```c
DG_MakeObjsPacket(objs, idx):
    for each child OBJ:
        DG_MakeObjPacket(obj, idx, flags)

DG_MakeObjPacket(obj, idx, flags):
    n = obj->n_packs
    obj->packs[idx] = GV_AllocMemory(PACKET_MEMORY[idx], n * sizeof(POLY_GT4))
    DG_WriteObjPacketUV(obj, idx)        // UV coords + CLUT + tpage
    DG_WriteObjPacketRGB(obj, idx)       // per-vertex colour
```

The `idx` is the active-frame index (0 or 1) — the engine
double-buffers packets so the GPU can be reading frame N's packets
while the CPU is filling frame N+1's.

### `DG_WriteObjPacketUV`

For each face:

- Look up `materials[face]` → texture hash
- Find DG_TEX in the global texture table
- Write `(u, v)` for each of 4 verts into the packet
- Write `tpage` and `clut` into the packet's first vert

### `DG_WriteObjPacketRGB`

Writes per-vertex colour from `obj->rgbs[]` (filled by shade /
preshade stage). For unlit models, fills with `0x80` neutral.

## `DG_FreeObjsPacket(objs, idx)`

Walks each OBJ, frees its `packs[idx]`. Used at frame end to wipe
the per-frame packet allocations.

## Helper inlines (libdg.h)

```c
DG_VisibleObjs(objs)     // flag &= ~INVISIBLE
DG_InvisibleObjs(objs)   // flag |=  INVISIBLE
DG_AmbientObjs(objs)     // flag |=  AMBIENT
DG_GBoundObjs(objs)      // flag |=  GBOUND   (group-level bound)
DG_GroupObjs(objs, gid)  // objs->group_id = gid
```

Group IDs are used to tag a logical set (e.g. all parts of the same
character) so the dispatcher can hide / fade an entire actor.

## Pitfalls

- **OBJ indexing is byte-fragile.** `DG_MDL.parent`, `.extend` are
  *indices* not pointers. If a copied DEF gets reordered, the
  indices break.
- **n_visible vs n_models.** `n_models` is the array size;
  `n_visible` is how many *initial* models to enable. Some KMDs
  hide parts (e.g. weapons not yet equipped) by setting
  `n_visible < n_models`.
- **Extend cycles aren't checked.** A bad KMD with `extend` →
  itself would loop forever; the port's bounds check (`extend_idx
  == cur_idx → NULL`) guards against the most common case.
- **Materials are u16 hashes, not raw indices.** Each DG_TEX is
  identified by a 16-bit ID; the texture table is keyed by ID.

## See also

- [pipeline.md](pipeline.md) — how DG_OBJS flow through the
  renderer.
- [text.md](text.md) — DG_TEX management.
- [`source/include/fmt_*.h`](../../../../source/include/) — KMD /
  PCX / OAR file-format headers.
