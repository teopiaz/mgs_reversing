# Rebase Conflict Log: editor → master

This file records how conflicts were resolved while rebasing the `editor`
branch onto `master`. Use it as reference if a similar rebase is needed in the
future, or to audit a resolution.

## Strategy

- **General rule:** master's renames and refactors win for identifier names
  and refactored shapes (master is upstream decomp work that other contributors
  are pushing forward). Editor's port-specific logic (NULL guards, early
  returns for missing resources, `#ifdef PORT_BUILD` blocks, helper calls) is
  re-applied on top of master's current code.
- The port should remain as separate as possible — prefer to keep port logic
  in `port/` and minimize edits to `source/`. When editor has unavoidable
  edits inside `source/`, preserve them but adapt to master's new names/types.

---

## 447737664 — "initial port to mac"

### `source/chara/snake/sna_init.c`

- **Conflict:** master renamed several `SnaInitWork` fields:
  - `field_88C` (struct) → `enable_shadow` (now `int *`, a pointer)
  - `field_848_lighting_mtx` (MATRIX) → `light[2]` (MATRIX array, decays to ptr)
  Editor's commit added a port-defensive `if (work->field_88C)` NULL guard
  around the dereference, plus an early-return guard at function entry for
  missing resources.
- **Resolution:** kept master's new names. Re-applied editor's NULL guard
  using the new name: `if (work->enable_shadow) *work->enable_shadow = ...`.
  Updated the early-return guard's reference from `work->field_88C` to
  `work->enable_shadow` (same field, new name).
- **Strategy applied:** master's renames win; editor's NULL/resource guards
  preserved with renamed identifiers.

---

## 9bbff0698 — "Replace hardcoded scratchpad addresses, fix texture cache and HZD routes"

### `source/kojo/demo.c` and `source/libhzd/event.c`

- **Conflict:** master split `demo.c` (commit 253c24072) into `demoscrn.c`,
  `unknown.c`, and `anime/effect/m1e1.c`; master also moved trap-related
  functions from `event.c` into a new `trap.c`. Editor's commit replaced
  `0x1F800XXX` scratchpad addresses with `(SCRPAD_ADDR + 0xXXX)` in the
  original locations. The conflicting blocks no longer belong in `demo.c` /
  `event.c` on master.
- **Resolution:** took master's version for `demo.c` and `event.c` (drop the
  conflicted blocks — those functions/data live elsewhere now). Re-applied
  the same `0x1F800XXX → (SCRPAD_ADDR + 0xXXX)` substitution mechanically to
  the new homes: `demoscrn.c`, `libhzd/trap.c`, `libhzd/line.c`,
  `libhzd/point.c`, `libhzd/surface.c`, `libhzd/private.h`, and
  `overlays/s12c/takabe/libdg2.c`. 268 substitutions total.
- **Strategy applied:** when master refactors code into new files, follow the
  code — re-apply the port's mechanical substitution at the new location.
  `SCRPAD_ADDR` is defined as `0x1f800000` on PSX (`source/include/psxdefs.h`)
  and as a port-allocated buffer on macOS, so the macro is portable in both
  builds.

---

## 553f3e3b8 — "Enable real collision system (collide.c) — gameplay now functional"

### `source/libhzd/collide.c` (modify/delete)

- **Conflict:** master split `collide.c` into `line.c`, `point.c`, `surface.c`,
  `trap.c` (commit `e6599abcd`). Editor's commit modified `collide.c` (which
  no longer exists on master) with port-specific fixes.
- **Resolution:** `git rm source/libhzd/collide.c` (accept master's deletion).
  Re-apply editor's three port-specific intents to `line.c`:
  1. **`UNTAG_PTR` for 64-bit pointers**: wrap the existing PSX define
     (`& 0x7fffffff`) in `#else` and add a PORT_BUILD variant
     (`& ~0x80000000UL` — 64-bit-safe). Identical PSX asm preserved.
  2. **MIPS register binding** (`register long *t0 asm("t0");`): wrap in
     `#ifdef PORT_BUILD`/`#else` so PORT_BUILD uses a plain `long *t0;`.
     The asm clobber `asm("" :: "r"(t0));` likewise wrapped, with
     `(void)t0;` in the PORT_BUILD branch.
  3. **Lowercase scratchpad address**: `gte_SetRotMatrix(0x1f800090);`
     → `gte_SetRotMatrix((SCRPAD_ADDR + 0x090));` (my earlier
     uppercase-only regex missed this one).
- **Strategy applied:** preserve master's PSX matching binary by wrapping
  every port-specific code path in `#ifdef PORT_BUILD`. Port-side files in
  this commit (`port/Makefile`, `port/psx/inline_n.h`, `port/psx/inline_x.h`,
  `port/link_stubs.c`) auto-merged with no intervention.

---

## 533422097 — "Fix Snake Act blocked by NULL bullets pointer"

### `source/chara/snake/sna_init.c`

- **Conflict:** editor evolved its own port-defensive guard (added in
  `447737664`) — early-return now only checks `objs` and `enable_shadow`,
  and `field_918_n_bullets` is filled with a dummy pointer instead of
  triggering an early return. Conflict was against the previous rebased
  resolution, not against pristine master.
- **Resolution:** applied editor's new guard with master's renamed
  identifier (`field_88C` → `enable_shadow`).
- **Strategy applied:** identifier renames win for the underlying field;
  editor's new port-defensive logic is preserved.

> ⚠️ **Pending cleanup (post-rebase):** the resolved guards in `sna_init.c`
> are port-only — they alter the function's PSX asm and break binary
> matching. After the rebase completes, sweep these into `#ifdef PORT_BUILD`
> blocks so the PSX build is unaffected. Same applies to all other
> in-source port adaptations introduced during this rebase.

---

## 462f507ae — "Fix 64-bit scratchpad pointer corruption, GCL pointer truncation, and rendering performance"

### `source/libhzd/collide.c` (modify/delete; rename → line.c)

- **Conflict:** git detected the rename of `collide.c` → `line.c` and applied
  most of editor's PORT_BUILD hunks automatically. The bottom section
  (lines 849–1294) couldn't apply because that content lives in `point.c`
  on master, not `line.c`. The auto-merged side-channel struct
  (`collide_ptrs`, all 7 members) landed in `line.c`.
- **Resolution:**
  1. `line.c`: dropped the conflicting "editor" block (its content is
     `point.c` territory now) — kept HEAD/empty branch. Trimmed
     `collide_ptrs` to the 2 members `line.c` actually uses (`seg_064`,
     `flags_end_070`).
  2. `point.c`: added a parallel `static struct ... collide_ptrs;` with
     the 5 members that `point.c` functions touch (`wall_054`, `wall_070`,
     `wall_08C`, `pt_flags_base`, `pt_flags_ptr`). Both files keep the
     name `collide_ptrs` so the editor's diff applies cleanly; `static`
     keeps each side-channel local to its file.
  3. Manually applied the editor hunks for `PointTestSegment`,
     `HZD_PointCheck`, and `HZD_PointNearSurface` to `point.c`.
- **Strategy applied:** when master's split moved code across files, split
  the port's per-file globals along the same lines. Disjoint-by-design:
  `line.c` and `point.c` reference different members in different
  functions, so two independent statics behave identically to editor's
  single struct.
