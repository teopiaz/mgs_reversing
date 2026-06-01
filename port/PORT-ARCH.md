# Port architecture — keeping `source/` pristine

This file describes how the macOS port keeps its glue isolated from the
decompiled PSX code under `source/`. The decomp is still in progress and gets
upstream rebases frequently; the goal of every mechanism here is to **minimise
the surface area of port edits in `source/`** so rebases mostly just work.

## The rule

> **Don't add `#ifdef PORT_BUILD` to `source/` files.**
>
> The decomp under `source/` should compile to PSX-matching output and stay
> upstream-clean. Whenever you need port-specific behaviour, reach for one of
> the four mechanisms below instead.

If you find yourself wanting to write `#ifdef PORT_BUILD ...` in `source/`, use
the decision tree at the bottom of this doc instead.

## The four mechanisms

### 1. Port-copy files (`*_PORT_COPIES` lists in `port/Makefile`)

For a `source/<subdir>/<file>.c` that diverges substantially on the port (≥3
ifdefs, struct-layout / pointer-width tweaks, or sits in a rebase-hot zone):
copy it to `port/<subdir>/<file>.c`, strip the ifdefs so the port branch is
unconditional, and add the stem to the matching `*_PORT_COPIES` list in
`port/Makefile`. Make's filter-out + precedence-rule machinery picks the port
copy automatically.

Lists currently in `port/Makefile`:

| List | Source-side subdir | Active port copies |
|---|---|---|
| `LIBGV_PORT_COPIES` | `source/libgv/` | `gvd actor` |
| `LIBHZD_PORT_COPIES` | `source/libhzd/` | `online near vector trap level hzdd bind` |
| `LIBDG_PORT_COPIES` | `source/libdg/` | `opack bound trans obj prim` |
| `TAKABE_PORT_COPIES` | `source/takabe/` | `gas_efct` |
| `EQUIP_PORT_COPIES` | `source/equip/` | `jpegcam` |
| `KOJO_PORT_COPIES` | `source/kojo/` | `demothrd demoexec` |
| `MENU_PORT_COPIES` | `source/menu/` | `radio datasave` |
| `THING_PORT_COPIES` | `source/thing/` | `sight` |
| `SOUND_PORT_COPIES` | `source/sound/` | `sd_main sd_file` |
| `GAME_PORT_COPIES` | `source/game/` | `control gamed jimctrl` |

Plus `port/libgcl_fix/` — its own subdir, six files patched for 64-bit
pointer arithmetic, compiled instead of `source/libgcl/`.

**How to promote a new file** — three steps:

```bash
# 1. Copy & strip ifdefs (port branch unconditional)
cp source/<subdir>/<file>.c port/<subdir>/<file>.c
unifdef -DPORT_BUILD -o port/<subdir>/<file>.c.new port/<subdir>/<file>.c
mv port/<subdir>/<file>.c.new port/<subdir>/<file>.c

# 2. Add the stem to the <SUBDIR>_PORT_COPIES list in port/Makefile.
#    If the subdir doesn't have a list yet, copy the existing pattern
#    (TAKABE_PORT_COPIES is a small canonical example).

# 3. Build & smoke-test. source/<subdir>/<file>.c is left untouched —
#    future upstream rebases still see it pristine.
```

Notes:
- Port copies use `-iquote $(SRCDIR)/<subdir>` so they can `#include "private.h"`
  etc. and resolve to the decomp headers.
- The pre-existing `source/<subdir>/<file>.c` is **not** modified — its ifdefs
  remain but become dormant (the Makefile filter-out skips that file). Future
  rebases of those ifdef-laden source files can be resolved trivially since
  the ifdef branches no longer matter for the port build.

### 2. CHARA override table (`port/chara_overrides.c`)

For "disable / replace an actor on the port" cases, there's a single port-side
table keyed by CHARA_ID:

```c
/* port/chara_overrides.c */
static const PortCharaOverride k_overrides[] = {
    { CHARAID_0025_BLUR,  NULL },   /* disable */
    { CHARAID_0044_GHOST, NULL },
    /* { CHARAID_X, NewPortReplacementY }, // replace with a port fn */
    { 0, NULL }
};
```

Hooked at `source/game/chara.c:GM_GetCharaID`. Adding a future override is a
**one-line edit** in `chara_overrides.c` — no `source/` touch.

### 3. Force-included `port/psx/port_overrides.h`

Always prepended via `-include psx/port_overrides.h` in `port/Makefile` CFLAGS.
Holds three categories of overrides:

- **Type aliases**: `u_long` → `uint32_t`, etc. — fixes PSX 32-bit assumptions
  against the macOS 64-bit ABI without per-file ifdefs.
- **Missing-prototype declarations**: e.g., `char *GCL_NextStr(void)` — fixes
  the implicit-int truncation bug that ate the title menu before commit
  df78800b7.
- **PSX-address remaps**: `SCRPAD_ADDR` → `port_scratchpad[]`, etc.

If you need a global tunable (like the gas-effect colour scale we landed in
gas_efct's port copy), define a macro here too.

### 4. Named hook functions (`port_*()` in source/)

For one-off port-specific intrusions in a `source/` file that doesn't warrant
a full port copy (typically 1 ifdef block, < 5 lines of port code, single call
site): extract the port branch into a `port_xxx()` function. The source/ file
calls it unconditionally; a weak default (e.g., return 1, do nothing) lives in
source/ for the PSX matching build, and the port implementation lives in
`port/`.

Existing examples to copy from:
- `port_apply_deferred_clear()` — called from `source/libdg/divide.c`, port
  body in `port/psx/gpu_stubs.c`.
- `port_gcl_overlay_load()` — called from `source/libgcl/parse.c`, port body in
  `port/gcl_overlay.c`.

## Decision tree — "I need port-specific behaviour"

```
Want to disable or replace an actor (a New*() constructor) on the port?
    -> Add one line to port/chara_overrides.c. STOP.

Need a numeric tunable (clamp, scale, threshold)?
    -> #define PORT_TUNE_X in port/psx/port_overrides.h, use the macro at the
       call site. Default in source/ should make the macro a no-op for PSX.

Need to change a single small site, called from < 3 places?
    -> Extract into port_xxx() with a weak no-op default in source/. Real
       implementation lives in port/<something>.c.

Need to change ≥ 4 ifdef blocks in one file, or the file is in a rebase-hot
zone, or the file has struct-layout / pointer-width tweaks?
    -> Promote to a port copy (see "Port-copy files" above).

Got a struct/typedef/header type-width fix?
    -> Either redefine in port_overrides.h via a typedef alias, OR (last
       resort) leave the existing ifdef in the header. Headers transitively
       included everywhere are highest-risk to touch.
```

## Verification after a rebase

1. `cd port && make -j8` — the main `mgs` binary builds clean.
2. `cd port/editor && make -j8` — the editor binary builds clean.
3. For each port copy whose upstream `source/` file was touched by the rebase,
   look at the diff: `git diff <last-sync-sha>..HEAD -- source/<path>`. Re-apply
   any new upstream changes to the port copy by hand (semantic merge — the
   ifdef branches don't matter because the port copy already strips them).

   The `port/scripts/port-sync.py` tool automates the discovery half of step 3:

   ```bash
   port/scripts/port-sync.py status        # which copies drifted, and how
   # ...semantic-merge each drifted copy by hand...
   port/scripts/port-sync.py mark port/<subdir>/<file>.c
   ```

   State lives in `.port-sync-state.json` at the repo root. After adding a
   new entry to a `*_PORT_COPIES` list in the Makefile, run
   `port/scripts/port-sync.py discover` to register it.

## What's NOT covered by this scheme

- **Headers** (`source/menu/radio.h`, `source/include/fmt_hzd.h`,
  `source/menu/menuman.h`): a few still have type-width ifdefs. These are
  transitively included everywhere, so a port copy of a header would require
  also overriding all the includers. For now they stay as ifdefs; revisit only
  if a rebase actively breaks one.

- **Single-ifdef leaf files** (`source/game/{map,motion,item,delay,chara}.c`,
  `source/libdg/{shade,display,chanl}.c`, `source/menu/{radar,jimaku,...}.c`):
  one-line guards. Converting each to a port copy is overhead; leaving the
  ifdef is fine. If a rebase makes one of them painful, promote it.

- **`source/chara/snake/sna_init.c`** (4 ifdefs) and `source/chara/snake/snake.c`
  (2): nested `chara/snake/` subdir. The Makefile mechanism is straightforward
  to extend with `CHARA_SNAKE_PORT_COPIES` if needed; not done yet.
