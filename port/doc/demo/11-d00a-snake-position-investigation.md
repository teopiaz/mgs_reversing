# d00a Snake Position Investigation — Status & Notes

**Status as of 2026-05-31:** Unresolved. All exploratory instrumentation reverted to leave the tree clean. This document captures what was tried, what was ruled out, and where the next attempt should pick up.

## 1. Symptom

In d00a (briefing cutscene) at roughly frame 847, the cinematic shows snake climbing onto a platform/ledge. Visually:

- **PSX (PCSX-Redux, dev_exe build):** wide-establishing-shot framing. Snake is small at bottom-left of the screen; a corridor / walkway with light fixtures and stairs spans the rest of the frame.
- **Port:** much tighter / "zoomed-in" framing. Snake's back fills the bottom-left; a single wall dominates the center of the frame; only the right edge shows the stairs.

The visual gulf is large — apparent zoom roughly 3× tighter in the port. The user perceives snake as "near a wall" in the port vs. "near the stairs" in PSX.

This came up after [10-dmo-format.md](10-dmo-format.md) work; previously the editor was being used as the reference of truth and the port disagreed with the editor's inspector-marker render path. That turned out to be the editor's own bug (see §3 below), which compounded the confusion.

## 2. Data-Confirmed Ground Truth at d00a Frame 847

Both PSX (via DEV_EXE instrumentation in `source/kojo/demoexec.c::FrameRunDemo` and `ShowScene`) and port (via dump in `port/libdg/libdg_stub.c::port_RenderChanl`) print the same values at the same DMO frame:

| Field | Value (PSX & port agree within rounding) |
|---|---|
| `adjust->pos_*` (DMO_ADJ type=9 snake demodoll) | `(-4529, -1055, 94)` |
| `model->control.mov` (post `ShowScene` write) | `(-4529, -1055, 94)` |
| `objs->world.t` (after `GM_ActObject`) | `(-4529, -1055, 94)` |
| `data->eye_*` (camera world pos) | `(-5183, -170, 1063)` |
| `data->center_*` (camera target) | `(-3054, -1092, -601)` |
| `data->roll` | `0` |
| `data->clip_dist` | `269` |
| `chanl->clip_distance` (after DG_LookAt) | `269` |
| `chanl->eye_inv.t` | PSX `(2350, -1697, 4428)` / port `(2348, -1689, 4431)` — diff ≤ 8 units (fixed-point rounding) |
| `chanl->eye_inv.m[0..2]` | match within ≤ 6 units per element |

Derived projection (using port_RenderChanl's screen_mat math with row 1 × 58/64 overscan):

| Quantity | PSX | Port |
|---|---|---|
| Snake bone[0] eye-space `(ex, ey, ez)` | `(-363, 431, 1337)` (computed) | `(-362, 434, 1338)` |
| `ndc_x = ex · clip / (160 · ez)` | `-0.4565` | `-0.4549` |
| `ndc_y` (with 58/64) | `-0.7745` | `-0.7791` |

**Math agrees.** PSX and port place snake at essentially the same NDC position. The math cannot produce the visually different render that we see.

## 3. Editor-Side Confusion (resolved, do not re-investigate)

Earlier rounds compared the port to the **editor's DMO inspector marker** as ground truth. That marker rendered at `ndc_x = -0.5063` due to two confounding editor bugs in `port/editor/ed_render.c::ed_render_frame_demo`:

1. **Wrong chanl slot.** Reads `s_eye_inv = DG_Chanls[g_demo_active_chanl].eye_inv` using **raw** array indexing. `DG_Chanl(N)` is `&DG_Chanls[N+1]` per the `+1` macro in `libdg.h`, and engine code writes the cinematic camera via `DG_LookAt(DG_Chanl(0), ...)` → `DG_Chanls[1]`. The editor's `g_demo_active_chanl` drifts to 0 while paused (the snapshot tracker `update_camera_snapshot` only runs while `state == PLAYING`), so it reads chanl[0] (empty / stale).
2. **Clip-distance fallback.** `s_clip_dist = ch->clip_distance ? ch->clip_distance : 300` — when chanl[0] is empty its `clip_distance` is 0, falling through to a hardcoded `300`. PSX/port both use `269` for this frame from the DMO file.

Both bugs combined make the editor's inspector marker render at a ~5% wider FOV than the engine doll (clip=300 vs 269) using a stale or zero `eye_inv`. The editor's inspector marker is **not authoritative**. Port/PSX engine doll IS authoritative — and they agree numerically.

The editor's engine-doll render (via `port_RenderObjects` in the demo path) is fine; it shares the port's libdg_stub.c. Only the inspector marker drawn by `ed_dmo_render_actors → render_kmd_posed` is broken.

A one-line fix exists (change `ed_render_frame_demo` to read from `DG_Chanl(0)` aka `DG_Chanls[1]` instead of raw indexing), but the user did not want it applied during this investigation. Save it for a separate editor-correctness pass.

## 4. The Real Mystery (open)

If port and PSX have identical:
- DMO data,
- camera matrices,
- clip distance,
- snake world coords and bone matrices,

…then the rendered frames *should* be pixel-equivalent (modulo aspect-correct blit). They are not. The port renders the scene visually ~3× zoomed compared to PSX.

Candidate explanations (none verified):

1. **Stage geometry at wrong world coords.** The user's strongest gut feeling. If d00a's wall / floor KMDs are loaded at different world positions in the port vs PSX, snake (at correct coords) appears in the wrong place relative to walls. A whole-stage shift of ~4000 in X was tested via a debug slider but the user could not visually dial in a clean match.
2. **Stage geometry at wrong scale.** A 3× scale factor on map KMDs would produce the visual we see. KMD loader is `port/libdg/kmd_loader.c` — passes vertex coords through unchanged, but check `mdl->pos` and the wall-spawn `ScaleMatrix(&world, &scale)` path in `source/enemy/wall.c::GetResources`.
3. **Aspect-ratio / viewport mishandling.** Port's `gl_renderer.c::blit_fbo_to_window` does aspect-correct letterbox using `target = render_w / 224.0` = `1.428`. PCSX-Redux may display the 320×224 framebuffer differently. But aspect alone is at most ~10% off, not 3×.
4. **Frame-counter misalignment.** Both runs were paused at "frame 847" but maybe the port's `port_demo_seek_target` doesn't index the same DMO_DAT.frame as PSX's `data->frame`. Worth double-checking next time — the port's `feed_paused_frame_direct` walks DEMO.DAT and matches on `port_dat.frame` (read from byte offset 4 of each DMO block), which *should* equal PSX's `data->frame`.

## 5. Tools Built During This Session (all reverted)

These were committed nowhere; this list is for the next attempt's reference so they don't have to be re-invented.

### PSX dev_exe instrumentation (`source/kojo/demoexec.c`, gated `#ifdef DEV_EXE`):
- `[PSX-CAM frame=N]` per-frame line at end of `FrameRunDemo`: `eye`, `ctr`, `roll`, `clip`, and the final `chanl->eye_inv` matrix.
- `[PSX-DOLL t=N clock=N]` inside `ShowScene` when `adjust->type == 9`: prints `adjust->pos/rot/visible/n_rots` and `control.mov` after the actor-pipeline write.
- `[PSX-DOLL-WORLD t=N]` after `GM_ActObject`: prints `objs->world.t` and `world.m[0..2]`.

Builds via `cd build && source venv/bin/activate && python3 build.py --variant=dev_exe --psyq_path=../../psyq_sdk`. Output exe: `obj_dev/_mgsi.exe`. Load in PCSX-Redux via `-exe`; TTY console captures `printf`.

### Wall-spawn dump (`source/enemy/wall.c::GetResources`, gated `#if defined(DEV_EXE) || defined(PORT_BUILD)`):
- `[WALL-SPAWN model=0xN map=N def_model=N] pos=(X,Y,Z) scale=(X,Y,Z)` and the resolved `world.t/m[0..2]`. Same format on PSX and port — directly diffable. Intended to localize per-wall world-coord mismatches.

### Port-side debug knobs (in `port/kojo/demothrd.c` globals + `port/libdg/libdg_stub.c::port_RenderChanl` apply + `port/imgui_debug.cpp` UI):
- `port_dbg_clip_override` — overrides `chanl->clip_distance` (slider 0..600, default 0 = off).
- `port_dbg_cam_eye_off[3]` — added to `chanl->eye_inv.t` for the duration of one `port_RenderChanl` call. Restored before return so other passes see the engine's authoritative state. Useful for finding camera-position-shift bugs.
- `port_dbg_doll_world_off[3]` — applied to screen_mat.t only when `is_snake = (Y in [-2000,-100]) && n_models == 16`. Equivalent to shifting snake in world space.
- `port_dbg_stage_world_off[3]` — applied to screen_mat.t for every objs **except** the snake filter. Equivalent to shifting the whole stage. Range ±10000 (because the user reported a ~4000-unit shift was needed to make snake "look right" — that result was probably a mis-comparison, see §3).
- `port_dbg_yscale_num / _den` — overrides the 58/64 overscan Y scaling. Default 58/64.

### Snake render dump (`port/libdg/libdg_stub.c::port_RenderChanl`, fires once when `port_demo_dump_request != 0`):
- Walks snake demodoll (filter: 16 bones + Y in climbing band).
- Prints `chanl`, `clip_distance`, `eye_inv`, `objs->world`, all 16 bone world matrices.
- `[PORT-PROJ] bone0.world / eye / clip / ndc_x / ndc_y` — projection of bone[0].t through the same math the renderer uses.
- `[PORT-CHANLS]` — `einv.t` and `clip_distance` of all three chanls, plus which array slot snake is in.

### AABB wireframe overlay (`port/libdg/libdg_stub.c::port_DrawActorBboxes` + imgui checkbox):
- Tick "Show AABB wireframes" in the Demo tab.
- Submits 12 line3d edges per `DG_OBJS` in any chanl, spanning `def->min..def->max` + `objs->world.t`. Cyan = walls/static; magenta = characters (n_models == 16).
- Tracked `port_dbg_doll_world_off` and `port_dbg_stage_world_off` so the wireframe moved with the offset.

### Demo scrubber (already in tree from prior session, not built here):
- `port/kojo/demothrd.c::feed_paused_frame_direct` reads DMO_DAT records directly off DEMO.DAT (sector 0x1441 for d00a), bypassing the streaming parser. Driven by `port_demo_seek_target` from the imgui Demo tab.
- Engages on `port_demo_seek_active && lpAct->frame >= 0` (after `CreateDemo` has initialized the actor).

### Editor inspector dump (`port/editor/ed_dmo.c::ed_dmo_render_actors`, fires on `g_dmo_dump_request`):
- Prints per-frame `[EDT-MARKER-PROJ]` with bone0 eye-space, clip, computed NDC, plus the cached `s_eye_inv.t` and `g_demo_active_chanl`.
- `[EDT-CHANLS]` — same chanl-slot dump as the port side, but read from inside the editor's render pass.

## 6. Test Harness (already in place)

`autoplay.lua` in `/Users/matteo/workspace/psx/mgs_reversing/` drives the PSX main menu to land on the d00a cutscene automatically (Down→Down→Circle→Circle). PCSX-Redux loads it via Scripting → Show Lua console. Combined with the `printf`-based instrumentation, this is the cheapest path to ground-truth comparisons.

## 7. Next Time — Suggested Approach

1. Don't re-debate snake's coords or the camera — they're proven identical between PSX and port. Skip to stage-geometry verification.
2. Re-apply just the **wall-spawn dump** (one change in `source/enemy/wall.c`, builds in both PSX and port from the shared source) and diff the two logs for d00a. Each wall has a known `model` strcode; a wall whose `world.t` differs between PSX and port is the bug.
3. If wall world coords match — investigate the KMD vertex data path. Maybe the model itself (vertex coords inside the KMD) is being loaded wrong on 64-bit. `port/libdg/kmd_loader.c` rebuilds the struct from 32-bit on-disc offsets; verify with a few known vertex values from the d00a wall KMDs.
4. If neither matches — look at projection-matrix or shader-side bugs. Try forcing the port to render with no 58/64 overscan, a different `uHalfScreen`, etc., and see if the scene matches PSX. The `port_dbg_yscale_num/_den` slider was built for exactly this.
5. **Always pause both PSX and port at the same `data->frame` before comparing screenshots.** The PSX-CAM `frame=N` line is the canonical signal. Use it as the truth, pause PCSX-Redux immediately after the right line prints (PSX prints come ~1 frame ahead of the rendered output).

## 8. Files to Re-apply (paths only — see git history for the diffs)

If picking this up later, look at this commit / branch for the instrumentation diffs before they were reverted:

- `/Users/matteo/workspace/mgs_reversing` (port repo, branch `editor`)
- `/Users/matteo/workspace/psx/mgs_reversing` (PSX repo, branch `master`)

Both repos had reflog entries for the revert at the end of the 2026-05-31 session; `git reflog` will surface the pre-revert state of each modified file if needed.
