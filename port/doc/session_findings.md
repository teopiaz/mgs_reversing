# Session findings — skybox (sphere) renders on top of 3D meshes

This document captures the state of investigation into a rendering bug on
stage `s01a` where the celestial sphere ("skybox") draws in front of the
stage's 3D geometry. It also records two fixes that landed this session
and a large amount of context that was paid for in debugging cycles —
meant for a future agent picking this up cold.

---

## TL;DR

Two separate bugs were landed this session:

- `f0eb4025e` — **GAME OVER screen was pure black**. Caused by two
  unrelated problems: (a) `LINE_G2` opcode (PSX `0x50`) was dispatched
  as `0x48` in the OT walker (dead case, 0x48 is `LINE_F3`), so every
  game-over line primitive silently fell through; (b) the OT handle
  table `port_ot_table[]` was only 2^18 entries AND was reset every
  frame, which meant handles stored in long-lived OT link slots
  resolved to unrelated objects within seconds of gameplay. Fix:
  point-label correct in `vram.c`, grow table to 2^24 entries, stop
  resetting `port_ot_next`.

- (confirmed earlier): door slide direction for `source/thing/door.c`
  was broken by 64-bit struct layout. Fix landed in `c298c8341`.

**Still broken**: on `s01a`, the sphere skybox renders on top of the
3D stage geometry. Visible symptom: a large, camera-locked textured
quad overlay that moves with camera rotation; nearby 3D meshes still
show through, but distant meshes are entirely hidden by the sphere.
An **in-progress fix** is in the working tree (bg/fg 2D buffer split)
but is not yet verified. Strong suspicion that what looks like
"sphere on top" is actually a 3D-projection / far-clip issue that
culls distant meshes, making the sphere visible where they should be.
See "Open questions" for the triage plan.

---

## 1. Repro

From `port/`:

```sh
PORT_GL=1 PORT_GL_SCALE=8 ./mgs ./ISO/mgs.cue
```

The user uses a non-Integral MGS disc image at `port/ISO/mgs.bin`
(referenced via `port/ISO/mgs.cue`). The ISO mode was landed earlier
in commit `c09829a94`; that's how `s01a` is loaded.

Stage `s00a` is the small exercise room; `s01a` is the first outdoor
dock area which contains both the sphere skydome and extensive 3D
geometry. `s00a` has no sphere → no symptom. `s01a` shows it.

Snake position at the moment the bug is most obvious:
`snake=(-4605, ~1100, 20452)`.

---

## 2. What the sphere actor actually does

Source: [source/thing/sphere.c](../../source/thing/sphere.c)

- Executes at `GV_ACTOR_AFTER2` (late in the frame).
- In `GetResources`, allocates a single `DG_PRIM` of type
  `DG_PRIM_SORTONLY | DG_PRIM_POLY_FT4` holding `sphere_visible_tiles_x
  × sphere_visible_tiles_y` textured quads. For a 320×224 screen and
  32×32 tiles that's ~11×8 = 88 quads (per frame buffer; it's double-
  buffered so the packs array has two copies).
- In `Act`, each frame it:
  1. Reads the camera eye matrix from `DG_Chanl(0)->eye` and computes a
     screen-space (u, v) scroll based on yaw and pitch.
  2. For every visible tile, writes screen-space `(x0, y0, x1, y1)` and
     tpage/clut/uv into the `POLY_FT4` pack.
  3. **Writes a fixed sort "Z" of 63000 into the low 16 bits of
     `poly->tag`** — [sphere.c:182-183](../../source/thing/sphere.c#L182-L183):

     ```c
     poly_tag = (short *)poly;
     *poly_tag = tag;            // tag = 63000 from line 135
     ```

     On PSX this is a classic "write low halfword of the OT tag" hack:
     the `DG_PRIM_SORTONLY` flag bypasses the normal transform stage
     (`DG_PrimChanl` skips it, see
     [prim.c:540](../../source/libdg/prim.c#L540)), and `DG_SortChanl`
     later reads this halfword as the Z key and inserts the pack into
     that OT slot.

Sort math in `DG_SortChanl`
([source/libdg/sort.c:105-120](../../source/libdg/sort.c#L105)):

```c
int z = *(unsigned short *)pack;   // 63000 for sphere
if (z > 0) {
    int ot_idx = (z - raise) >> 8; // ~246 for sphere
    addPrim(&ot[ot_idx], pack);
}
```

Channels in source indexing (note the +1 shift; see
[libdg.h:620-623](../../source/libdg/libdg.h#L620-L623)):

```c
DG_Chanl(idx) = &DG_Chanls[idx + 1]
```

So:

- `DG_Chanls[0]` = root/carrier chanl (the one `DG_DrawOTag` walks).
- `DG_Chanls[1]` = `DG_Chanl(0)` = "world" chanl (where sphere lives).
- `DG_Chanls[2]` = `DG_Chanl(1)` = overlay chanl (HUD, menu, subtitles).

The sphere's `GM_MakePrim(..., NULL, NULL)` defaults to
`GM_MakePrimChanl(..., 0)` ([game.h:217-220](../../source/game/game.h#L217-L220))
→ `DG_MakePrim(type, n, 0, ...)`, which queues into `DG_Chanls[1]`.

At `DG_ClearChanlSystem` time ([chanl.c:220-228](../../source/libdg/chanl.c#L220-L228)),
`DG_Chanls[1]` and `DG_Chanls[2]` are linked into `DG_Chanls[0]`'s OT
via their `chanl->link` slot (16 for chanl 1, 8 for chanl 2 — observed
live in the debug probes earlier this session).

---

## 3. How PSX handles sphere ordering correctly

On PSX everything — 3D world geometry, the sphere, HUD — goes into a
single linked ordering-table. The OT is walked once per frame and the
GPU consumes primitives in walk order. Painter's algorithm:

- World 3D prims land at slots whose index corresponds to their eye-
  space Z (deep Z → high slot). They are drawn first.
- The sphere explicitly writes itself to slot ~246 (Z=63000 >> 8),
  which is deeper than any visible 3D geometry. It therefore draws
  **before** the 3D world, and the world paints over it naturally.

The sphere never needs to understand what 3D is in the way — the
sort key guarantees it's the first thing the GPU draws.

---

## 4. How the port splits 3D vs 2D — and why the skybox breaks

The port does **not** run 3D through the OT. Instead:

- `port_RenderObjects`
  ([libdg_stub.c:766](../libdg/libdg_stub.c#L766)) walks each chanl's
  **object queue** (`DG_OBJS`, the 3D mesh list) and submits
  triangles to GL via `gl_submit_tri3d`. These triangles go through
  the real 3D vertex shader
  ([gl_renderer.c around line 195](../libdg/gl_renderer.c#L195))
  with perspective projection and depth test.
- `port_DrawOTag`
  ([vram.c:532](../libdg/vram.c#L532)) walks the 2D OT — everything
  that went through `DG_SortChanl` or was `addPrim`'d by actors. It
  dispatches on the PSX GPU opcode (`0x2C` for `POLY_FT4`, `0x60` for
  `TILE`, etc.) and submits to `gl_submit_tri2d` which uses a
  separate 2D shader with no perspective and `gl_Position.z = 0`.

The 2D shader originally had depth test **disabled**
([gl_renderer.c flush_2d_buf](../libdg/gl_renderer.c#L953-L957) —
was `glDisable(GL_DEPTH_TEST);` before this session's experiments).

Net effect: the sphere goes through the 2D path (prim, not object),
is submitted to `gl_submit_tri2d` at `z=0`, drawn with depth test off
AFTER the 3D pass → always on top of 3D.

The PSX sort key (slot ~246) is computed and used to insert into the
OT, but since the 2D walker draws everything with the same effective
depth/state, the slot information is lost at render time.

---

## 5. OT walk — buffer layout (confirmed live this session)

With a fresh `s01a` load:

```
DG_Chanls[0].ot[which] = 0x102be9xxx  (root, 257-cell OT)
DG_Chanls[1].ot[which] = 0x102be9604  (world, 257-cell OT, sphere here)
DG_Chanls[2].ot[which] = 0x102be95f4  (overlay, 257-cell OT, HUD/menu)
DG_Chanls[2].env1[which] = 0x10260bb28
chanl[1].link = 16
chanl[2].link = 8
```

`DG_DrawOTag(which)` calls
`DrawOTag(&DG_Chanls[0].env1[which].tag)` → port `DrawOTag` →
`port_DrawOTag`. Walker traverses:

```
env1[0]  →  ot[0][max] → … → ot[0][16] → env1[1] (chanl 1's env1)
                                                  → ot[1][max] → … (SPHERE here)
                                                  → env2[1] → ot[0][15] → …
                                       → ot[0][8]  → env1[2] (chanl 2's env1)
                                                  → ot[2][max] → … (HUD here)
                                                  → env2[2] → ot[0][7] → …
                                       → ot[0][0] → env2[0]
```

All three OT ranges are traversed in one walk. This is what lets the
port know "which chanl" a primitive belongs to at walk time: the
walker's pointer is inside one of the three `chanl[i].ot[which]`
ranges at every link step (it briefly leaves the range when following
a prim pointer, but comes right back on the next hop).

---

## 6. Fixes landed this session (for context)

### 6a. LINE_G2 dispatch (commit `f0eb4025e` part 1)

`source/game/over.c`'s `DrawAnimation` builds the GAME OVER logo from
`LINE_G2` primitives via `setLineG2(line)`, which sets opcode `0x50`
([libgpu.h:329](../psx/libgpu.h#L329)). The port walker in
`vram.c` had a `case 0x48: /* LINE_G2 ... */` — but `0x48` is
`LINE_F3`. So every game-over line was unhandled. Fix: relabel to
`case 0x50`.

### 6b. OT handle table lifetime (commit `f0eb4025e` part 2)

`port_ot_table[]` is a 24-bit-index handle table that maps OT tag
values to real 64-bit pointers ([libgpu.h:28](../psx/libgpu.h#L28)).
Historical values:

```c
#define PORT_OT_TABLE_SIZE (1 << 18)   // 256K slots → 2 MB at 8 B each
int port_ot_next = 0;                   // reset at top of every frame
```

Problem: OT link tags are written at `DG_SwapFrame` time and must
remain resolvable for **many frames** (until that buffer's
`DG_ClearChanlSystem` runs again). The per-frame reset made handles
collide within seconds; the 2^18 capacity wrapped in ~22 seconds.
GAME OVER reliably took >22 s to fade in, so its addPrim-ed link
targets resolved to random objects, silently unlinking chanl 2.

Fix: `PORT_OT_TABLE_SIZE = 1 << 24` (128 MB), stop resetting
`port_ot_next`. This is important general infrastructure — **any**
cross-frame OT tag now works. Needed a full rebuild of `gpu_stubs.o`
because the BSS allocation size is baked in at compile time (common
symbol via `-fcommon`).

---

## 7. The open bug: sphere on top of 3D

### 7a. First attempt — per-vertex z + depth test (failed)

Hypothesis: make the 2D shader emit a variable z, enable
`GL_DEPTH_TEST` with `GL_LEQUAL` in the 2D pass, and push sphere
vertices to z=0.999 (foreground 2D stays at z=0.0).

Implementation:

- `GL2DVert::_pad` repurposed to `depth_flag` (unsigned short).
- Added `unsigned short port_2d_depth_flag` global. `pack_vert2d`
  copies it into the vertex.
- 2D vertex shader: `float z = (aTex.w != 0u) ? 0.999 : 0.0;`.
- `flush_2d_buf` flips to `glEnable(GL_DEPTH_TEST) + GL_LEQUAL +
  glDepthMask(GL_FALSE)` (no depth write so successive 2D don't mask
  each other).
- `port_DrawOTag`: set `port_2d_depth_flag = 1` while walker is
  inside `DG_Chanls[1].ot[which]` range (world chanl); clear to 0 in
  chanl 2's range. Prim pointers inherit the last OT-cell's state.

Verified at runtime: `[poly_ft4] depth_flag=1` fires consistently on
sphere tiles. So the routing works; the sphere's 2D verts really do
get z=0.999.

Result: **no visible change**. User confirmed the sphere still
covers meshes. Reasoning: 3D meshes near the far clip plane get
depth values very close to 1.0 (the standard GL perspective depth is
nonlinear; anything past ~half the far distance is > 0.9). With
`GL_LEQUAL`, `0.999 <= 0.9996` passes → sphere overdraws the far 3D
mesh anyway. Only the closest meshes get proper occlusion.

### 7b. Second attempt — z=1.0 (failed)

Move the sphere to exactly z=1.0 so only cleared-to-1.0 framebuffer
pixels pass the test. With `GL_LEQUAL`, `1.0 <= 1.0` is TRUE, so
sphere draws on empty pixels (good). For 3D at z<1.0, `1.0 <= 0.99`
is FALSE, so sphere stays masked (good on paper).

Result: **same failure mode**. At z=1.0 the sphere ties with
anything at the far plane. In OpenGL, ties favor the later-drawn
fragment (sphere was last), so the sphere still wins over far 3D.

`GL_LESS` isn't a clean option either because cleared regions also
sit at 1.0, and `1.0 < 1.0` would mask the sphere from cleared areas
too, leaving a black sky.

### 7c. Third attempt — split bg/fg 2D, flush bg before 3D (IN PROGRESS, UNCOMMITTED)

This is the "real skybox" architecture. Key idea: render the sphere
BEFORE the 3D pass. 3D fragments then paint over it naturally, using
their normal depth test against each other — and the sphere needs no
depth trickery, just to be drawn first.

Code changes (working tree, `port/libdg/gl_renderer.c` +
`port/libdg/vram.c`):

- New buffer `g_tri2d_bg_buf` parallel to `g_tri2d_buf`. Same vertex
  layout, same VAO/VBO.
- `tri2d_bg_reserve` helper (mirrors `tri2d_reserve`).
- `gl_submit_tri2d` checks `port_2d_depth_flag`; if set, routes the
  three verts into `g_tri2d_bg_buf` and returns. Foreground 2D keeps
  going to `g_tri2d_buf` as before.
- New `flush_tri2d_bg` that calls `flush_2d_buf` with the bg buffer.
- `gl_renderer_begin_2d` resets `g_tri2d_bg_count` as well.
- `gl_renderer_present`: calls `flush_tri2d_bg()` **before** the 3D
  pass. The existing `flush_tri2d` for foreground runs after 3D as
  before.
- Reverted the shader z hack (`gl_Position.z` back to 0.0).
- Reverted the 2D depth-test enable; `flush_2d_buf` is
  `glDisable(GL_DEPTH_TEST)` again.
- `pack_vert2d` now writes `depth_flag = 0` unconditionally (the
  field is effectively unused again; left in place to keep vertex
  layout stable).

This is built but not thoroughly tested. User reported the symptom
persists — but that report may have predated the full rebuild (an
earlier sub-attempt had the shader change committed but
`gl_renderer.o` was not rebuilt due to missing header dependency in
the Makefile; `strings mgs | grep "z = (aTex"` actually showed the
old literal until I forced the rebuild).

**Priority for next session**: verify the split-pass is actually
active, THEN decide whether the remaining symptom is really ordering
or something else.

---

## 8. Strong hypothesis: it's (partly) a 3D projection problem

The user's exact words near the end: *"seems more a problem of
projection"*. I agree. Two pieces of evidence:

1. When the split-pass is active and working, the sphere is
   guaranteed to be drawn first, and 3D painter's algorithm (via the
   3D depth test against OTHER 3D) handles the rest. If distant
   meshes still appear absent, the sphere can't be the cause — 3D
   simply didn't draw anything for those pixels.

2. The 3D vertex shader clips anything with perspective z ∉ [-1, 1].
   `TRI3D_VS` uses:
   - `uNearFar = (4.0, 32768.0)` — the far plane is 32768 world units
     from the camera eye.
   - `fz` (face Z, post eye-space transform) > 32768 → z_ndc > 1 →
     clipped.
   - Near clip clamp: `if (cz < 4.0) cz = 4.0;`.

   Snake's world positions on `s01a` are around `(−4605, 1100,
   20452)` (logged each frame). Cameras in MGS can sit 10–30k units
   from the action. Far ~32768 is tight but usually enough. However,
   a stage edifice 20k units in front of the camera is already at
   `fz ≈ 20000`, depth ≈ `(32768+4)/(32768−4) + B/20000 ≈ 0.9996` in
   NDC. That's into "indistinguishable from sphere's 0.999" territory,
   which is why attempts A and B couldn't help.

So even if the split-pass is applied cleanly, distant stage geometry
may already be close to the 3D clip boundary and prone to other
issues (precision loss, partial clip) that make it vanish.

---

## 9. Triage plan for next session

### Step 1 — verify the split-pass is live

Rebuild from a clean state:

```sh
cd port && rm -f obj/gl_renderer.o obj/vram.o mgs && make -j8
strings mgs | grep -c "flush_tri2d_bg"     # should be > 0 in symbols
strings mgs | grep "z = (aTex"             # should return nothing
                                             (means shader reverted OK)
```

Run and confirm with a 1-shot debug:

- Temporarily log `g_tri2d_bg_count` at the top of
  `gl_renderer_present` (before the clear) for one frame after
  `s01a` loads.
- Expect `g_tri2d_bg_count ≈ 3 * 88 ≈ 264` per frame once the sphere
  is active.

If `g_tri2d_bg_count` is 0, the routing isn't firing → check that
`port_2d_depth_flag` transitions to 1 inside
`DG_Chanls[1].ot[...]` range. The previous `[poly_ft4]` trace showed
it working; if it regresses, re-add the trace (see the bottom of
this doc for the exact fprintf).

### Step 2 — test cheap far-plane extension

Even a temporary flip of `uNearFar` from `(4.0, 32768.0)` to
`(4.0, 131072.0)` should push distant meshes further from the clip
and away from the "z=0.9996" precision zone:

- [gl_renderer.c:1118](../libdg/gl_renderer.c#L1118):
  `glUniform2f(g_tri3d_u_near_far, 4.0f, 32768.0f);` ← bump the
  second arg.

If distant meshes now show up where the sphere previously covered
them, the original bug is really the projection / far clip.

### Step 3 — instrument `aDist`

3D vertex shader uses a per-vertex `aDist` ("camera distance"), fed
by the PSX GTE emulation pipeline. If the emulator computes a
wrong or clipped `aDist` for far faces, they'd project outside the
NDC cube and get culled:

- Look in `port/psx/gte_math.c` and `port/libdg/libdg_stub.c`'s
  `port_RenderChanl` for where `aDist` is produced and fed into the
  `GL3DVert` struct (`gl_submit_tri3d` call site, around
  [libdg_stub.c:690](../libdg/libdg_stub.c#L690)).
- Add a one-shot debug print dumping `fz`, `cz`, `aDist`, and the
  final `gl_Position` for a known distant face in s01a — e.g. the
  furthest map-object quads. Compare to a visible near quad.

### Step 4 — confirm world chanl contents

If all three steps above don't land it, check whether sphere prims
genuinely go to `DG_Chanls[1]` (world) and not to some other chanl:

- `DG_PRIM.chanl` field is set inside `DG_MakePrim` with the `chanl`
  arg. `GM_MakePrim` passes 0, which becomes `DG_Chanl(0) =
  DG_Chanls[1]` via the +1 offset in `DG_Chanl()`.
- `DG_QueuePrim` in [prim.c](../../source/libdg/prim.c) pushes
  into the chanl's prim queue.
- `DG_SortChanl` reads the queue for its chanl and inserts packs into
  that chanl's OT.

This should be a no-op verification — but worth a sanity check.

---

## 10. Key file:line references (for grep speed)

- Sphere prim write of Z=63000:
  [source/thing/sphere.c:182-183](../../source/thing/sphere.c#L182-L183)
- `DG_PRIM_SORTONLY` skip in transform stage:
  [source/libdg/prim.c:540](../../source/libdg/prim.c#L540)
- Sort key read + insert:
  [source/libdg/sort.c:105](../../source/libdg/sort.c#L105)
- Port-side sort implementation:
  [port/libdg/libdg_stub.c:824](../libdg/libdg_stub.c#L824)
- `DG_Chanl()` +1 offset:
  [source/libdg/libdg.h:620](../../source/libdg/libdg.h#L620)
- Chanl linking into chanl 0 OT:
  [source/libdg/chanl.c:220](../../source/libdg/chanl.c#L220)
- `port_DrawOTag` (2D walker):
  [port/libdg/vram.c:532](../libdg/vram.c#L532)
- `port_RenderObjects` (3D walker):
  [port/libdg/libdg_stub.c:766](../libdg/libdg_stub.c#L766)
- `port_RenderChanl` (submits 3D tris via `gl_submit_tri3d`):
  [port/libdg/libdg_stub.c:391](../libdg/libdg_stub.c#L391)
- GL 3D vertex shader:
  [port/libdg/gl_renderer.c around line 195](../libdg/gl_renderer.c#L195)
- GL 2D shader:
  [port/libdg/gl_renderer.c around line 325](../libdg/gl_renderer.c#L325)
- `flush_2d_buf` (depth test state):
  [port/libdg/gl_renderer.c around line 940](../libdg/gl_renderer.c#L940)
- `gl_renderer_present` clear + 3D + 2D pass ordering:
  [port/libdg/gl_renderer.c around line 1050](../libdg/gl_renderer.c#L1050)
- `PORT_OT_TABLE_SIZE` (post-fix):
  [port/psx/libgpu.h:23](../psx/libgpu.h#L23)

---

## 11. Working-tree diff summary (uncommitted)

Running `git diff` in the repo root should show roughly:

- `port/libdg/gl_renderer.c`:
  - New `g_tri2d_bg_buf` / `g_tri2d_bg_count` / `g_tri2d_bg_cap` statics.
  - New `tri2d_bg_reserve` helper.
  - `gl_submit_tri2d` branches on `port_2d_depth_flag` to route bg.
  - New `flush_tri2d_bg` helper.
  - `gl_renderer_begin_2d` resets bg count.
  - `gl_renderer_present` flushes bg before the 3D pass.
  - 2D shader z reverted to 0.0.
  - 2D `flush_2d_buf` `GL_DEPTH_TEST` back to `glDisable`.
  - `GL2DVert::_pad` renamed to `depth_flag` (informational; no
    behavioral difference anymore).
  - Global `unsigned short port_2d_depth_flag` declared/initialized.

- `port/libdg/vram.c`:
  - `port_DrawOTag` caches `DG_Chanls[1].ot[which]` and
    `DG_Chanls[2].ot[which]` pointers once per call.
  - Walk loop sets `port_2d_depth_flag` based on whether `p` is
    inside world-chanl vs overlay-chanl OT range.
  - `#include "libdg/libdg.h"` added to get `DG_Chanls` type.

## 12. Debug snippets to reattach if needed

If you need to trace things again, these are the fprintf hooks I
used — copy them back in, rebuild (force-rebuild the relevant .o if
editing headers), run, grep:

- Sphere routing trace (inside `case 0x2C` in `vram.c`):

  ```c
  {
      extern unsigned short port_2d_depth_flag;
      static int last_t = -1;
      extern int GV_Time;
      if (GV_Time - last_t >= 60) {
          last_t = GV_Time;
          fprintf(stderr, "[poly_ft4] t=%d depth_flag=%u\n",
                  GV_Time, port_2d_depth_flag);
      }
  }
  ```

- Walker chanl visit summary (end of `port_DrawOTag`, needs the
  local `walk_visits_world / _ovly` counters reintroduced):

  ```c
  fprintf(stderr, "[walk-summary] t=%d world_visits=%d ovly_visits=%d prims=%d\n",
          GV_Time, walk_visits_world, walk_visits_ovly, prim_count);
  ```

- Chanl-0 link slot resolve (inside `port_DrawOTag`, after range
  caches):

  ```c
  extern int port_ot_next;
  u_long *ch0_ot = DG_Chanls[0].ot[port_ot_buffer_index];
  u_long *slot = ch0_ot + DG_Chanls[2].link;
  u_long tag = *slot;
  unsigned handle = tag & 0x00ffffff;
  void *resolved = (handle == 0xffffff) ? NULL : port_ot_table[handle];
  fprintf(stderr, "[go-probe] slot_tag=0x%08lx handle=%u resolves_to=%p"
          " env1[w]=%p port_ot_next=%d\n",
          (unsigned long)tag, handle, resolved,
          (void *)&DG_Chanls[2].env1[port_ot_buffer_index], port_ot_next);
  ```

---

## 13. Meta: things that bit me repeatedly this session

- **Header → source dep tracking is absent.** Changing
  `PORT_OT_TABLE_SIZE` in `port/psx/libgpu.h` does NOT rebuild
  `gpu_stubs.o` on a plain `make`. The static array size is baked in
  at compile time of `gpu_stubs.c`. Diagnostic:
  `nm -m obj/gpu_stubs.o | grep port_ot_table` — the alignment
  column shows the real byte size.
- **Stale binary in `mgs`.** `make -j8` often said "Nothing to be
  done for `all'`" even after editing a `.c` because the .o was
  older than the header. `strings mgs | grep <literal>` is a
  reliable check for "did this change actually land?".
- **Common symbols.** With `-fcommon`, arrays declared `void *foo[N]`
  in a .c file produce "common" symbols whose size becomes the max
  across TUs. If any TU sees the old `N`, you get the smaller
  allocation. Always clean-rebuild the defining TU after header
  size changes.
- **PSX sort key is a 16-bit write with a signed `short *`.** The
  `tag = 63000` line in sphere.c performs narrowing to `short`
  (i.e. stores `0xF618`). `DG_SortChanl` reads `unsigned short`,
  so it reads `63000`. Straightforward but worth remembering when
  hand-instrumenting.
