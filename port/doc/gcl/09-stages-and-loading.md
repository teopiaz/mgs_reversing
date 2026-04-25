# Stages, Loading, and Lifecycle

How the engine actually loads a stage, when scripts run, and what the
`load` / `restart` commands really do.

Ground truth:
- [source/game/gamed.c](../../../source/game/gamed.c) — game daemon (top-level orchestrator)
- [source/game/script.c:539 GM_Command_load](../../../source/game/script.c#L539)
- [source/game/script.c:498 GM_Command_start](../../../source/game/script.c#L498)
- [source/libfs/stageld.c](../../../source/libfs/stageld.c) — FS stage loader
- [source/libgcl/gcl_init.c](../../../source/libgcl/gcl_init.c) — script init
- [port/libgcl_fix/gcl_init.c](../../libgcl_fix/gcl_init.c) — overlay-aware init

---

## The stage lifecycle

```
                        ┌──────────────────────┐
                        │ GM_LoadRequest = 1   │   (something asked for a load)
                        └─────────┬────────────┘
                                  │
                        ┌─────────▼────────────┐
                        │ Pause all actors      │   (GV_SetPauseLevel(0xFF))
                        │ Clear normal memory   │   (GV_ClearMemorySystem)
                        │ Save resident cache   │
                        └─────────┬────────────┘
                                  │
                        ┌─────────▼────────────┐
                        │ FS_LoadStageRequest   │   (./STAGE.DIR / ISO read)
                        │   reads DATACNF tags  │
                        │   loads each tag (.r,│
                        │     .e, .wvx, .mdx, │
                        │     .gcx, .hzd, .kmd)│
                        └─────────┬────────────┘
                                  │
                        ┌─────────▼────────────┐
                        │ GCL_InitFunc('g')     │   ← scenerio.gcx loader
                        │   GCL_LoadScript()    │   ← parses bytecode
                        └─────────┬────────────┘
                                  │
                        ┌─────────▼────────────┐
                        │ printf "exec scenario"│
                        │ GCL_ExecScript()      │   ← runs `script { ... }`
                        │   - script registers  │
                        │     charas, traps,    │
                        │     mapdef, lights    │
                        └─────────┬────────────┘
                                  │
                        ┌─────────▼────────────┐
                        │ printf "end scenario" │
                        │ GV_SetPauseLevel(0)   │   ← unpause
                        │ work->status = WORKING│
                        └─────────┬────────────┘
                                  │
                                  ▼
                              gameplay frame loop
```

Match the printf markers against your `port/log.txt` to time-stamp
each step.

---

## What `load` actually does

```gcl
load "s01a"               # request a stage transition
load "s01a" -m $s:7df9    # specify which area to enter
load "s01a" -s 1          # camera/spawn slot
load "" -r 0              # hard restart of the current stage
load "" -r 1              # soft restart
```

Inside [GM_Command_load](../../../source/game/script.c#L539):

```c
scriptStageName = GCL_ReadString(GCL_GetParamResult());
if (*scriptStageName == '\0')
{
    GM_LoadRequest = 1;     // empty string → re-enter current stage
    return 0;
}
if (GCL_GetOption('r'))
{
    if (!GCL_GetNextParamValue())
    {
        // Hard restart — reset resident cache + memory pools
        GV_InitResidentMemory();
        GV_InitCacheSystem();
        DG_ClearResidentTexture();
        GM_SetArea(GV_StrCode(scriptStageName), scriptStageName);
    }
    else
    {
        // Soft restart — keep resident cache, just rerun scenerio
    }
    GM_LoadRequest = 1;
    return 0;
}
```

Key takeaways:

- **`load "name"`** transitions to a stage. The current scenerio
  finishes its current frame, then on the next tick the game daemon
  picks up `GM_LoadRequest` and starts the lifecycle above.
- **`load ""`** is a re-entry of the current stage (e.g. used as a
  redirect after the title screen).
- **`load "" -r 0`** is a *hard* restart — full memory clear, resident
  cache rebuild. Used on game-over.
- **`load "" -r 1`** is a *soft* restart — keep cached models, just
  rerun the scenerio.

## Pre-load variable conventions

The script you're transitioning **to** reads its initial state from
the game-state vars. Set them before calling `load`:

```gcl
proc go_to_s00a {
    eval($f:000001 = false)         # not in a demo
    eval($w:800010 = -3952)         # snake X
    eval($w:800012 = 104)           # snake Y
    eval($w:800014 = -534)          # snake Z
    eval($w:000002 = 1024)          # camera yaw
    eval($w:000004 = 0)             # camera pitch
    load "s00a" -m $s:7df9 -s 1
}
```

Common pre-load handoff vars (from
[02-variables.md](02-variables.md#common-well-known-variables)):

| Variable    | Purpose                                |
|-------------|----------------------------------------|
| `$w:800010` | Snake spawn X                          |
| `$w:800012` | Snake spawn Y                          |
| `$w:800014` | Snake spawn Z                          |
| `$w:000002` | Initial camera yaw (PSX-units, 4096 = full turn) |
| `$w:000004` | Initial camera pitch                   |
| `$w:000044` | Player face direction seed             |
| `$f:000001` | "We're in a cutscene" flag             |
| `$s:80000E` | Current map ID (set by the receiving stage)|

---

## What the `start` command does

```gcl
script {
    start -m -f -v        # menu, fonts, vars
    ...
}
```

Each option toggles an init step inside
[GM_Command_start](../../../source/game/script.c#L498):

| Option | Effect                                                                |
|--------|-----------------------------------------------------------------------|
| `-s`   | `GM_InitReadError()` — reset disc-error counter                       |
| `-m`   | `menuman_init_80038954()` — bring up the menu engine                  |
| `-f`   | `font_load()` — load the active font                                  |
| `-v`   | `GCL_InitVar()` + `MENU_InitRadioMemory()` + reset frame counter      |
| `-d`   | `GCL_ChangeSenerioCode(N)` — switch between scenerio.gcx and demo.gcx |
| `-c`   | `GCL_InitClearVar()` — clear stage flags (no-questions-asked)         |

Stages compose these freely; `init/scenerio.gcl` uses `start -m -v -f`,
gameplay stages typically use just `start -s` or skip `start` entirely.

---

## Scenerio vs Demo scripts

A `.gcx` file is referred to by the runtime as either the **scenerio**
or the **demo**:

- `scenerio.gcx` (`GCX_scenerio`, hash `0xea54`) — the main per-stage
  script: spawns charas, sets up traps, runs gameplay logic.
- `demo.gcx` (`GCX_demo`, hash `0xa242`) — the cutscene script: drives
  camera angles, demo dolls, dialogue triggers for in-engine cinematics.

Which one runs is controlled by the global `scenerio_code` (set via
`GCL_ChangeSenerioCode`):

```gcl
start -d 1     # tell the runtime "next exec is demo.gcx"
load "s01a" -m $s:7df9 -s 1   # → s01a/demo.gcx will execute
```

Both files always coexist for a stage; only one runs per `load`.

---

## Stage data formats (what the FS pulls in)

The `mapdef` command references files by hash + extension. Here's
what each loader does (from
[port/extern_stubs.c](../../extern_stubs.c) and the GV cache extension
table):

| Ext | Loader              | Content                                  |
|-----|---------------------|------------------------------------------|
| `g` | `GCL_InitFunc`      | GCL bytecode (this doc's subject)       |
| `k` | `DG_LoadInit`       | KMD 3D model                             |
| `l` | `DG_LoadLitInit`    | Light data per model                     |
| `h` | `HZD_LoadInitHzd`   | HZD collision / zone map                 |
| `r` | `(generic)`         | RES generic data                         |
| `c` | `(generic)`         | C-something (cinematic config?)          |

`mapdef` ties them all together for one map id:

```gcl
mapdef $s:7df9 \
    -k $s:c681 $s:6da4    # KMDs to compose
    -l $s:c681            # light data for the KMD
    -h $s:c681 0          # HZD + area index 0
```

---

## Resident vs per-stage cache

Two cache regions, controlled by the FS DATACNF tags:

- **Resident** — survives stage transitions. Snake's KMD, common
  fonts, etc. Cleared only on hard restart (`load -r 0`).
- **Normal** — cleared on every stage load. Stage-specific KMDs, the
  scenerio.gcx itself, HZDs.

The runtime keeps "resident-cache dirty" state and rewrites the
resident region only when transitions need it. See
[gamed.c:408](../../../source/game/gamed.c#L408).

This matters for overlays: your custom `scenerio.gcx` lives in the
**normal** region and gets rebuilt on every stage entry. If you write
to memory at a fixed offset of `top` (the cached buffer) inside an
overlay, your write doesn't survive a stage exit/return.

---

## Game-over and restart

On `restart` (the bare command):

```gcl
restart
```

This is a soft-restart back to the current stage's last-saved state,
re-running the scenerio with `start -c` (clear-flags) semantics so
flag state is fresh. Used heavily in game-over chains:

```gcl
trap $s:14c9 $s:21ca $s:3223 {       # subject=snake, event=kill, any zone
    if ($w:80007A == 1) {
        # Easy mode: prompt continue
        chara $s:eced $s:eced         # COUNTDOWN actor
        delay -t 600 -e sub_game_over
    } else {
        # Normal+: just restart
        restart
    }
}
```

`$s:3223` is the kill event. The CHARAID for `COUNTDOWN` is `0xeced` —
spawning it drops the timed prompt overlay.

---

## Stage transitions inside cutscenes (`demo.gcx` chains)

Many cutscenes end by calling `load` to drop the player into the next
gameplay stage. From [d00a/demo.gcl](../../gcl/decompiled/d00a/demo.gcl):

```gcl
proc sub_after_intro {                # called after intro cutscene
    eval($w:800010 = -3952)
    eval($w:800012 = 104)
    eval($w:800014 = -534)
    load "s00a" -m $s:7df9 -s 1
}
```

The `s00a` scenerio then takes over for actual gameplay. So a typical
session traverses:

```
init → title → select → demo (d00a) → s00a → demo (d01a) → s01a → ...
```

Each transition is a fresh `LoadReq` cycle.

---

## Overlay considerations

If you're modifying a stage via [port/overlays/](../../overlays/):

1. Your `.gcx` replaces the disc copy at script-load time.
2. The runtime still goes through the same lifecycle — your overlay
   isn't a hot-patch, it runs from `GCL_LoadScript` like any normal
   script.
3. Any pre-load `eval(...)` your overlay does to set
   `$w:800010..14` etc. happens in the **previous** stage's
   scenerio (where `load` is called), not in the destination — so
   modifying the destination stage's spawn pos requires editing the
   stage that *loads* it.

For example, to change Snake's spawn in `s01a`, you'd edit
`d00a/demo.gcx` (the cutscene that loads s01a) — not `s01a/scenerio.gcx`.
