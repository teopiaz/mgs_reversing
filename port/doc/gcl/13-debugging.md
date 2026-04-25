# Debugging GCL Scripts

How to inspect a running script, what log messages mean, common
failure modes, and tools for round-tripping changes.

---

## Where to look first

When something misbehaves:

1. **`port/log.txt`** — captured stdout/stderr from the most recent
   run. Search for `[gcl]`, `[gcl-overlay]`, `[gcl-init]`,
   `[gcl-cmd]` markers.
2. **`port/overlays/<stage>/scenerio.gcl`** — your overlay source.
   Decompile it back from the compiled `.gcx` (`gcx2gcl.py`) if you
   want to confirm what the runtime is actually about to read.
3. **Per-stage `.gcl` in the corpus** — `port/gcl/decompiled/<stage>/`
   is the **vanilla** decompile. Use it as a known-good reference.

---

## Runtime log markers

| Marker                    | Where emitted                           | Meaning                          |
|---------------------------|------------------------------------------|----------------------------------|
| `LoadReq`                 | gamed.c                                 | a stage transition was requested |
| `[fs] Loading stage 'X'`  | libfs                                   | DATACNF read started for stage X |
| `[fs]   Stage processing complete` | libfs                          | all DATACNF tags loaded          |
| `[gcl-init] id=… match=…` | port/libgcl_fix/gcl_init.c              | which `.gcx` was offered         |
| `[gcl-overlay] called: stage=… id=…` | port/gcl_overlay.c           | overlay loader entered           |
| `[gcl-overlay] hit: …`    | port/gcl_overlay.c                      | overlay file replaced disc copy  |
| `[gcl-overlay] miss: …`   | port/gcl_overlay.c                      | no overlay; using disc           |
| `[gcl-overlay] skip: …`   | port/gcl_overlay.c                      | preconditions failed (empty stage etc.) |
| `LoadEnd`                 | gamed.c                                 | post-load setup finished         |
| `exec scenario`           | gamed.c                                 | `GCL_ExecScript()` about to run  |
| `[gcl] chara: name=0xXXXX func=0x… binds=0x…` | game/script.c            | a chara command spawned an actor |
| `[gcl-cmd] 0xXXXX (??) func=0x…` | port/libgcl_fix/command.c       | dispatching a command (only first 50) |
| `BIND XXXXXXXX`           | game/script.c                           | trap/ntrap registered            |
| `end scenario`            | gamed.c                                 | scenerio block finished          |
| `command not found`       | command.c                               | hash didn't match any registered command |
| `PROC X NOT FOUND`        | command.c                               | `call(sub_X)` referenced unknown proc |
| `NOT SCRIPT DATA !!`      | command.c                               | header byte at script_body wasn't 0x40 |
| `id conflict`             | cache.c                                 | two scripts loaded with same hash |
| `[cache] MISS: id=0xXXX`  | port/cache.c                            | code asked for an uncached file  |

The chara/cmd dispatch logs are sampled (first 50) to avoid log bloat.
Bump the `gcl_cmd_debug` counter ceiling in
[command.c:57](../../../source/libgcl/command.c#L57) for verbose
runs.

---

## Common errors and what they really mean

### "Where Is Snake ????"

Spam from
[source/enemy/command.c:551](../../../source/enemy/command.c#L551).
Enemy AI is asking the HZD for Snake's zone and getting nothing back.

- **Pre-existing port bug** when running vanilla — see
  [doc/11-known-issues.md](../11-known-issues.md). Few occurrences
  are normal.
- **Made worse by overlay buffer** outside the 4 GB pool — *fixed*
  in commit `0788c92fc` by allocating overlays via `port_malloc`.
  If you see hundreds of these per second WITH an overlay loaded,
  something has regressed.

### "PROC X NOT FOUND"

```
PROC 7F00007D NOT FOUND
```

`call(sub_XXXX, ...)` referenced a proc that doesn't exist in the
current script. Check:

- Spelling of the `sub_XXXX` hash (must match the proc's table entry).
- Whether you stripped a proc by accident in your overlay.
- Whether the call is in `scenerio.gcx` but the proc is in
  `demo.gcx` (separate compilation units — see
  [10-procs-and-args.md](10-procs-and-args.md#proc-visibility-scope)).

The high byte `0x7F` is the port's pointer-table marker — the hex
`0x7F00007D` is actually a magicked pointer-as-int that didn't
resolve. Check your encoding if you see this for a hash you didn't
write.

### "command not found"

A command hash didn't match any of the 28 registered commands.
Possibilities:

- You're targeting a command that doesn't exist (e.g., misspelt name
  produces a wrong hash).
- The file got truncated during a write — re-decompile and recompile.
- The bytecode was tampered with.

### Stage loads but Snake doesn't appear

Likely causes:

- No `chara $s:21ca $s:21ca` in the scenerio.
- Snake spawned but `pad -s` was never called → input disabled.
- `$w:800010..14` (spawn coords) are 0,0,0 → Snake is at origin
  (often outside the map / under the floor).
- Scenerio called `pad -r` and never re-enabled (look for `pad -s`
  somewhere in the chain).

### Codec call doesn't trigger

```gcl
radio -c 14085 t:01010366 0     # nothing happens
```

- The contact ID (14085) must already be unlocked. Some are gated by
  story progress and only enabled after specific scenes.
- The dialogue table reference (`t:...`) must point into a valid
  offset in `RADIO.DAT`. Wrong offset → silent failure.
- `radio -c` requires the codec system loaded — `start -m` must have
  run earlier in the boot chain.

### Trap doesn't fire

```gcl
trap $s:foo $s:21ca $s:0dd2 { ... }
```

- The zone hash `$s:foo` must exist in the stage's HZD file. If the
  HZD doesn't define a zone named `"foo"` (`GV_StrCode → 0xfoo`), the
  trap registers but never matches.
- The subject hash `$s:21ca` (snake) must be the actually-spawned
  CHARAID. If you removed the `chara $s:21ca` line, no subject = no
  match.
- Verbose-mode the bind list by adding a printf at
  [event.c](../../../source/libhzd/event.c) where matches are checked.

---

## Round-trip workflow

The fastest debugging loop:

```
┌──────────────────────────────────────────────────────────────┐
│ 1. Decompile vanilla:   gcx2gcl.py disc.gcx -o foo.gcl      │
│                          --trailing foo.tail                │
│                                                              │
│ 2. Edit foo.gcl                                              │
│                                                              │
│ 3. Recompile:            gcl2gcx.py foo.gcl -o foo.gcx       │
│                          --trailing foo.tail --no-align4     │
│                                                              │
│ 4. Drop into:            port/overlays/<stage>/scenerio.gcx │
│                                                              │
│ 5. Run:                  cd port && ./mgs ./ISO/mgs.cue     │
│                                                              │
│ 6. Inspect:              tail port/log.txt | grep gcl       │
└──────────────────────────────────────────────────────────────┘
```

`port/overlays/compile.sh` automates steps 3–4 — drop your `.gcl`
sibling next to the existing `.tail` in `overlays/<stage>/` and run
`bash compile.sh`.

---

## Useful instrumentation snippets

### Confirm which proc fires when

```gcl
proc sub_handler {
    print "sub_handler reached"
    print arg1                # Note: print only handles strings; arg1 won't show
    ...
}
```

`print` takes only string literals (see
[script.c:1109](../../../source/game/script.c#L1109)). To log a
variable's value, write a one-shot trap that prints a unique tag:

```gcl
if ($w:80007A == 1) { print "diff = easy" }
elseif ($w:80007A == 2) { print "diff = normal" }
else { print "diff = hard" }
```

### Confirm a trap fires at all

```gcl
trap $s:foo $s:21ca $s:0dd2 {
    print "trap foo fired"
    ...
}
```

Then grep `port/log.txt` for `trap foo fired`.

### Confirm `mesg` reception

You can't print from inside the receiver in GCL (it's C code), but
you can confirm a chain by sending a uniquely-tagged mesg and
chaining a known visible side-effect:

```gcl
mesg $s:21ca $s:937a 99       # send a sentinel motion id
delay -t 30 -e sub_after_99    # if the motion runs, this fires later
```

### Compare your overlay vs vanilla bytes

```bash
cmp port/overlays/s01a/scenerio.gcx /path/to/disc/s01a/00ea54.gcx
```

Or diff at the GCL level:

```bash
python3 port/gcl_tools/gcx2gcl.py port/overlays/s01a/scenerio.gcx > /tmp/over.gcl
python3 port/gcl_tools/gcx2gcl.py /path/to/disc/s01a/00ea54.gcx     > /tmp/disc.gcl
diff -u /tmp/disc.gcl /tmp/over.gcl
```

---

## Glyph debugging (for `m"…"` strings)

Custom dialogue with `m"..."` markers needs the right per-stage font
trailing blob. If text comes out as squares or random Japanese:

- Inspect glyphs: `python3 port/gcl_tools/show_glyphs.py <stage.gcx>`
- Sheet view: `--sheet glyphs.pgm` then open in any image viewer
- Make sure your `.tail` file is the same one paired with the `.gcl`
  during decompile — `compile.sh` auto-handles this when files are
  side-by-side

---

## Performance / sanity checks

- A scenerio that registers >127 binds prints `binds over` from
  [script.c:307](../../../source/game/script.c#L307). Vanilla stays
  well under; if you exceed, prune redundant traps.
- Each delay actor lives at `EXEC_LEVEL = LEVEL3` in the actor
  hierarchy; spamming `delay -t 1` rapidly fills the actor pool.
- Memory pool exhaustion shows up as `[port_malloc] pool exhausted!`
  — usually means an overlay leaked across many transitions; restart
  the game to reset the bump allocator.

---

## When to give up and read C

For options not documented in [04-commands.md](04-commands.md):

```bash
# What option letters does GM_Command_xxx parse?
grep "GCL_GetOption" source/game/script.c -A 1
```

Pull the handler for any command you're stuck on. The handler always
walks options via `GCL_GetOption('x')` — every letter is a separate
branch you can read.

For HZD trap matching specifics:

```bash
# How does event.c decide whether to fire?
$EDITOR source/libhzd/event.c
```

Same for actor recv handlers — look in
`source/chara/`, `source/enemy/`, `source/takabe/`.
