# Rebasing the port onto master

Upstream `master` is active decomp work by other contributors, so the port
has to be rebased onto it periodically. This has been done twice so far:

| When | Branch | Base → onto | Commits replayed | source/-touching | Actually conflicted |
|---|---|---|---|---|---|
| Jun 2026 | `editor` | ? → Jun master | — | — | see per-commit log below |
| Aug 2026 | `port-title` | `b14e83941` → `7964de7fd` (416 upstream commits) | 350 | 117 | **27** |

Read the **Playbook** first; the **Recurring fixes** catalogue is the part
that actually saves time. The per-commit log at the bottom is history — useful
for auditing a specific resolution, not for planning.

---

## Playbook

A rebase is **two phases**, and the second is the bigger one. Budget for it.

### Phase 0 — before you start

1. `git tag <branch>-prerebase` and **build + run the pre-rebase tip**. You
   need a known-good reference; without one you cannot tell a regression from
   a latent bug. (In Aug 2026 the pre-rebase tip turned out to stall at
   stage-load on the dev machine, which made the comparison useless — better
   to discover that up front.)
2. Check for other port branches first (`git branch -a`). In Aug 2026 a whole
   rebase was redone on an abandoned branch before anyone noticed `port-title`
   was the live line.

### Phase 1 — the replay

Most commits touch only `port/`, which upstream never touches, so they apply
clean. Conflicts cluster in the early "initial port" commits and in `libdg`.

Loop: resolve → `git add -A source port` → `git rebase --continue`, and keep
continuing while no conflict appears. Roughly 1 in 4 source-touching commits
actually stops you.

Two shapes worth recognising immediately:

- **Mis-anchored hunk.** Upstream extracted the code into a helper, so git
  anchors the port's block onto whatever text still matches — often a
  different function. Take HEAD, then re-apply the port's change at its real
  new home. Do not try to merge the hunk in place.
- **Upstream decompiled it too.** If the port commit replaced a
  `#pragma INCLUDE_ASM` with C and master now has its own C version, master's
  wins — it's the matching decomp.

### Phase 2 — the build repair

Expect ~1800 errors on the first build. They collapse fast if you work in this
order; do not start fixing individual call sites before doing 1 and 2.

1. **Kill stale header forks first.** `port/libdg/libdg.h` was a stale copy of
   `source/libdg/libdg.h` that `-I .` made shadow the real one: **1615 of 1856
   errors**. Delete it and remove the Makefile's `-include` of it.
2. **Re-sync the port copies** (the `*_PORT_COPIES` lists) with a 3-way merge
   against their base version — 21 of 26 merged clean:

   ```sh
   BASE=<old base sha>
   for f in port/<dir>/<file>.c ...; do
     rel="source/${f#port/}"
     git show "$BASE:$rel" > /tmp/old.c
     git merge-file --diff3 -L "port copy" -L "base" -L "upstream" \
        "$f" /tmp/old.c "$rel"
   done
   ```

   For the ones that conflict, there is usually a **better source**: the
   commit that promoted the file to a port copy (`git log --diff-filter=A --
   port/<f>`) — its **parent** holds `source/<f>` with the port guards already
   applied to upstream's shape. `git show <promote>^:source/<f> > port/<f>`.
3. Then work the remaining errors by file, largest first.

### Phase 3 — verification

```sh
make clean && make -j8                     # header deps are NOT tracked (see below)
PORT_GL=1 PORT_AUTOLOAD_STAGE=s00a ./mgs <disc>.cue
```

`PORT_AUTOLOAD_STAGE` also skips the pre-game menu, which otherwise blocks a
headless run. Healthy output: stage loads, `exec scenario`, then `[tick N]`
lines with **Snake at non-zero coordinates** and `hp=256/256`. Snake at
`(0,0,0)` with ticks stopping means it stalled, not that it is idle.

`port/psx/struct_assertions.c` is a free canary — if the layout asserts still
pass after a rebase, the struct work is right.

---

## Traps that cost real time

- **The Makefile has no header dependency tracking.** Editing a header does
  *not* trigger recompiles. Twice a "fix" looked like it failed when the object
  was simply stale. After touching any header: `make clean`. (Adding
  `-MMD -MP` + `-include $(OBJ:.o=.d)` would fix this permanently.)
- **`git` paths are relative to your shell's cwd.** After `cd port`, a
  `git log -- port/x.c` silently returns nothing. Run git from the repo root.
- **`grep -c error` on a build log counts `-Wno-error` in the compiler flags.**
  Grep for `error:` with the colon.

---

## Recurring fixes

These recurred across both rebases and will very likely recur again.

### Upstream renamed a struct field or type

| Was | Now | Where it bites |
|---|---|---|
| `DG_MDL.vertices / normals / texcoords / materials / flags` | `verts / norms / uvs / texids / flag` | `kmd_loader.c`, `libdg_stub.c`, `test_server.c` |
| `DG_MDL.min/max/pos`, `DG_DEF.min/max` | `lx..lz / ux..uz / tx..tz` | `kmd_loader.c` |
| `DG_DEF.n_visible / n_models` | `n_models / n_x_models` | `kmd_loader.c` — note the shift in meaning |
| `DG_CHANL.clip_distance` | `screen` | `libdg_stub.c`, `demoexec.c`, `imgui_debug.cpp` |
| `HZD_MAP` | `HZD_DEF` | `hzd_loader.c`, `hzdd.c` |
| `HZD_HDL.header / group` | `def / grp` | `snake.c`, `test_server.c` |
| `GM_TotalHours/Seconds/Saves`, `GM_OptionFlag`, `GM_DifficultyFlag`, `GM_CurrentStageFlag`, `GM_SnakePos*`, `GM_LastResultFlag` | `GM_PlayTimeHours/Seconds`, `GM_SaveCount`, `GM_Configuration`, `GM_GameLevel`, `GM_SaveArea`, `GM_PlayerPos*`, `GM_Result` | `linkvar.h` consumers |
| `CHARA_00xx_*` cutscene entries | `DEMO_*` | `extern_stubs.c` — **map by hash id, not by name** |

### Upstream moved a global into a per-file `static`

Two remedies, pick by who owns the data:

- The port **replaces** that file (it is a port copy or a `libgcl_fix` fork) →
  the port file takes ownership: turn its `extern` into a definition.
  Done for `linkvarbuf`, `sv_linkvarbuf`, `argbuffer`, `commandlines`,
  `current_script`, `gGcl_vars_*`, `gStageName_*`, `gBindsArray_*`,
  `FS_DiskNum`, `FS_ResidentCacheDirty`, `gMemCards`.
- Upstream still compiles it and the static should **stay** static → add a
  narrow `#ifdef PORT_BUILD` accessor next to it rather than un-static'ing.
  Done for the light tables (`light.c`), the actor list (`libgv/actor.c`) and
  the gas-mask sight flag (`gmsight.c`).

### Upstream renamed or split a file

`display.c` → `frame.c`; `collide.c` → `online.c`/`near.c`; `event.c` →
`bind.c`/`trap.c`; `demo.c` → `demoexec.c`/`demoscrn.c`; `zone.c` →
`navigate.c`.

Follow the code to its new home, and **check the Makefile object list** —
`libdg_display.o` had to become `libdg_frame.o`. Watch for port-only functions
that lived in the renamed file: `DG_LookAt` was port-only, lived in
`display.c`, and had no upstream counterpart; it moved to
`port/libdg/libdg_stub.c` with private copies of its two vectors.

### The port's PSX shims drift from psyq

- `P_TAG` must be psyq's 8-byte primitive header (`tag` + `r0,g0,b0,code`),
  not a bare `u_long` — `chanl.c:DG_SetBackgroundPrim` calls `setRGB0` on a
  `P_TAG *`.
- `setXYWH` targets **4-vertex prims** and fills the four corners. It does not
  write `w`/`h` (those exist only on SPRT/TILE, which use `setWH`).

### Memory-base macros

The single nastiest one, because it fails at *runtime*, not build time.
Upstream replaced `GV_NORMAL_MEMORY_TOP` / `GV_PACKET_MEMORY*_TOP` with
`MEM_ADDR` / `PACK_ADDR0` / `PACK_ADDR1` / `MEM_BOTTOM` / `MEM_SIZE` /
`PACK_SIZE`. `port/libgv/libgv.h` overrides them by name, so a rename silently
un-overrides them, `GV_Malloc` starts handing out **PSX** addresses, and the
first `GV_NewActor` segfaults at `0x80117000`.

If you see a fault at an `0x80xxxxxx` address, check this file first. Sizes
must stay in sync with `port/port_memory.c`.

### Link-time: duplicates and stubs

- **Duplicate symbols in `asm_stubs.c`** — upstream decompiled a function the
  port still stubs. Prune the stub. (Jun→Aug: 259 on `editor_mac`, 5 on
  `port-title`, which had already been pruned once.)
- **Undefined symbols** — usually a stage table referencing an actor whose file
  is excluded, or a function still `#pragma INCLUDE_ASM` upstream. Add a stub
  to `link_stubs.c`. Note that `link_stubs.c` stubs also carry **old names**
  and need renaming when upstream renames the actor (verify by hash in
  `charalst.h`).

### New upstream files the port cannot compile

Newly decompiled overlays sometimes size PSX padding as
`0xNNN - 0xMMM - sizeof(T)`, which goes **negative** once `T` holds 64-bit
pointers ("array is too large" with an absurd count). Others call functions
through declarations that disagree with their prototypes. Neither is fixable
port-side — add them to `OVERLAY_EXCLUDE` and stub whatever the stage tables
need. So far: `snake18.c`, `ninja.c`, `hind.c`, `rope.c`, `jeep_liq.c`,
`b_graph.c`.

### MIPS-vs-host semantic divergence

A class of bug the compiler cannot catch and that only shows up at runtime.
MIPS `div` leaves an **undefined result** for a zero divisor and keeps running;
x86-64 `idiv` raises **SIGFPE**. So a zero-divisor path is invisible on PSX and
fatal on the port — `takabe/fadeio.c` divides by a zero fade duration in the
s03d attract demo.

Guard under `PORT_BUILD`, pick the semantically-complete value rather than a
magic number, and **log once** so it stays visible if the zero is really a
parsing bug rather than genuine data. Expect more of these as the port reaches
code paths it never used to.

---

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

# Per-commit log — Jun 2026 rebase (`editor` → master)

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
