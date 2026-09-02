# Regression tests for the port

Rebasing the port onto upstream `master` breaks it in a small number of
*recurring, recognisable* ways. This is a design for a test suite aimed at
exactly those ways — not at coverage for its own sake.

Every tier below is justified by a bug that actually happened. Where a real
example exists it is named, so the test can be checked against a known failure
instead of against a guess.

Run everything with `port/runtests.sh` (tiers 0–2 need no disc image).

---

## Why the usual approach doesn't fit

`source/` is hash-matched to the PSX binary; it cannot be edited to be more
testable, and it is full of PSX globals, actor callbacks and `#pragma
INCLUDE_ASM`. Linking a conventional unit-test binary against it is not
practical.

What *is* practical, and what has repeatedly caught real defects:

- **compile-time invariants** — free, no linking, no disc;
- **pure leaf functions** — a handful of them carry most of the 64-bit risk;
- **semantic parity of port shims against their upstream originals** — this is
  where the worst bugs live;
- **boot-and-assert integration runs** — the harness already exists.

The ordering is deliberate: tier 0 catches a rebase break in seconds, tier 3
takes minutes and needs the disc.

---

## Tier 0 — compile & link invariants (no disc, no run)

### 0.1 Struct layout — extend `port/psx/struct_assertions.c`

Already present with 4 assertions. Extend to every struct the port memcpys,
indexes at a known size, or mirrors in the imgui panes.

**Real failure:** `source/overlays/s11d/chara/hind/hind.c:11` —
`char pad_object[0x180 - 0x9C - sizeof(OBJECT)]`. `OBJECT` grew when its
pointers became 64-bit, the expression went negative, and the compiler
reported `array is too large (18446744073709551596 elements)`. Any struct
using this hardcoded-offset padding idiom is a latent instance.

### 0.2 No trivial stub shadows a real implementation

**Real failure:** `GM_ResetScript` was `{ return 0; }` in `port/link_stubs.c`
while the real body existed in `port/game_script_fix.c` under the name
`GM_InitBinds`. Upstream calls it from `GM_ActInit` on *every* stage start, so
bind counts accumulated until `trap`/`ntrap` wrote past
`gBindsArray_800b58e0[128]` and corrupted the globals behind it.

The check: for every function defined in a `*stub*.c` with a trivial body
(empty, or `return <constant>`), fail if a non-`static` definition of the same
symbol exists anywhere in `source/`. Allowlist the deliberate ones with a
comment convention (`/* STUB-OK: reason */`).

### 0.3 No raw PSX addresses in dispatch tables

**Real failure:** `source/stage/s04b.c:9` has
`{ 0xb99f, (NEWCHARA *)0x800dcdbc }`. `port_chara_override()` drops any
constructor below `0x100000000`, so the actor silently never spawns — the
symptom is a missing character, not an error.

The check: grep `source/stage/*.c` and `source/stagevr/*.c` for
`(NEWCHARA *)0x8` and report a count. Pin the count; a rebase that adds more
is fine, one that *changes* the set should be looked at. Better: emit the list
so `port/chara_overrides.c` can be kept in sync.

### 0.4 Symbols the port needs still exist

Assert that every symbol `port/` references from `source/` resolves. This is
what a rebase breaks most often (upstream renames or splits a file). `make`
already catches it — the value is naming *which* upstream change did it, so
put the check in the harness output rather than leaving it as a wall of
linker errors.

---

## Tier 1 — pure unit tests (no disc, no run)

Small host-native binary linking only leaf functions with no PSX state.

| Unit | Assert | Justified by |
| --- | --- | --- |
| `port_ptr_to_int` / `port_int_to_ptr` | round-trip for pool pointers; `NULL → 0 → NULL`; the **negated-offset** convention `delay.c` uses for block-vs-proc | `HZD_ExecBind` passed a raw 64-bit pointer where the sign distinguishes a proc id from a block, so timed traps silently ran the wrong branch |
| `GV_StrCode` | the `CMD_*` constants in `game_script_fix.c`, each documented in-source as `GV_StrCode("<name>")` | note `port/strcode_lookup.c` is **not** an oracle here — it maps GCL string ids, a different hash emitted by the GCL compiler (`GV_StrCode("cape")` is `0x1298`, not `0xb99f`) |
| GCL var decode | `0x11800002` → linkvarbuf, offset 2, type short; `$w:00000A` → var bank, byte 10 = `var[5]` | mis-decoding this cost a whole debugging session |
| `gte_math` fixed-point | reference vectors for the ops the renderer depends on | MIPS-vs-host divergence is a named recurring class |
| FS stream ref count | `Open`/`Close` nest; `IsEnd` iff count 0; `Init` resets; `Close` clamps at 0 | `FS_StreamIsEnd` was an EOF heuristic; short codec lines deadlocked `strctrl` |

Keep this tier honest: only functions with no I/O and no globals belong here.
Everything else is tier 2.

---

## Tier 2 — port shim parity

The highest-value tier, because this is where every stub bug lived. For each
port shim that stands in for an upstream function, assert the **behaviour
upstream callers rely on** — not the implementation.

Real failures, each of which is one assertion:

- `FS_StreamIsForceStop()` must reflect stop state, not return a constant.
  Upstream returns `fs_stream_stop`; the port returned `0`, so
  `sd_str.c` case 5 never left playback and `strctrl` never tore down.
- `CdControl` / `CdControlB` / `CdReady` / `CdSync` must **write** `result[0]`.
  They left the caller's buffer untouched, so `Safety_800C45F8` read
  shell-open bits out of uninitialised stack and looped forever printing
  `TRY`/`OPEN`.
- `GCL_Command` must not dereference a NULL from `FindCommand`.
- `DG_LoadInitLit` must return 1 (upstream does; the port returns 0, and
  `GV_LoadInit` treats `<= 0` as failure). **Still open.**

Mechanically these are table-driven: `{ shim, setup, call, expected }`.

---

## Tier 3 — boot-and-assert integration (needs disc)

Built on `port/porttest.sh`, which already boots to a stage, captures frames
and exits on its own.

Per stage in a list (start with `s00a`, `s01a`, `s04c`, `d01a`):

**Must not appear in the log**
`=== CRASH`, `Signal caught`, `[ot] ABORT`, `binds over`, `Double Pcm`,
`Stream:File Pos Error`, `FATAL: no game data`, `PROC .* NOT FOUND`

**Must appear**
`LoadReq <stage>`, `exec scenario`, `end scenario`

**Must hold on the last tick**
`hp=256/256`, Snake coordinates non-zero (the playbook already calls out
`(0,0,0)` as the stall signature), tick counter advanced past a threshold

**Bounded**
`[gcl] chara: func not found` count within an allowlist per stage — this is
how 0.3's dropped actors show up at runtime.

This tier would have caught, in one run each: the bind overflow, the CD
`TRY`/`OPEN` loop, the codec stream deadlock, and the ordering-table cycle.

---

## Tier 4 — golden frames

`porttest.sh` already writes deterministic PNGs. Commit a small baseline set
and compare with a perceptual hash (exact match is too brittle — timing
varies with disk cache, so bracket each moment with several frames and accept
a best match).

**Real failure this catches and nothing else does:** the codec subtitle drawn
twice. Every log line was healthy; the only evidence was pixels. Root cause
was `port_is_fb_readback_tpage()` classifying the codec's own 15-bit panel
textures as framebuffer readbacks.

Keep the baseline set small (one frame per stage, plus one codec frame) or it
becomes a maintenance tax that gets disabled.

---

## Suggested order of work

1. **0.2 and 0.3** — both are ~30-line scripts, need no disc, and each maps to
   a bug that shipped. Highest value per hour by a wide margin.
2. **Tier 3** — the harness exists; this is mostly a list of grep assertions.
3. **Tier 1** — real unit tests, but the leaf functions are already fairly
   stable.
4. **0.1 extension**, then **tier 2**, then **tier 4**.

## Wiring into the rebase

Phase 3 of `port/REBASE_CONFLICTS.md` currently says "boot s00a and eyeball
the ticks". Replace with `port/runtests.sh`, and add tier 0 to Phase 2 — it
runs without a disc, so it can be used *during* build repair rather than after.
