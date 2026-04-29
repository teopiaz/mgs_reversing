# Engine architecture

A single-binary game with a clear horizontal split between portable
engine libraries (`lib*`) and game-policy code (`game/`, `chara/`,
`enemy/`, `weapon/`, …). Everything compiles into `SLPM_862.47`
(or `.48` / `.49` for variants) and runs in one MIPS R3000 process
on the PSX.

The dependency graph is roughly:

```
                ┌──────────────────────────────────────────────┐
                │  game/gamed.c  GameWork actor (state machine)│
                └──────────────────────────────────────────────┘
                                  │
                  ┌───────────────┼─────────────────┬───────────────┐
                  ▼               ▼                 ▼               ▼
        ┌──────────────┐  ┌────────────────┐ ┌──────────────┐ ┌──────────┐
        │ chara/snake  │  │ enemy/         │ │ menu/        │ │ weapon/  │
        │ chara/...    │  │ animal/...     │ │ jimaku/radio │ │ bullet/  │
        │ thing/       │  │ takabe/        │ │ font/        │ │ equip/   │
        └──────┬───────┘  └───────┬────────┘ └──────┬───────┘ └────┬─────┘
               │                  │                  │              │
               └──────────────────┴────────┬─────────┴──────────────┘
                                           ▼
                ┌──────────────────────────────────────────────┐
                │             game/  (policy layer)            │
                │  control.c  motion.c  camera.c  script.c     │
                │  item.c  alert.c  area.c  map.c  …           │
                └──────────────────────────────────────────────┘
                                           │
                  ┌────────────┬───────────┼────────────┬────────────┐
                  ▼            ▼           ▼            ▼            ▼
              ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
              │ libgv  │  │ libdg  │  │ libhzd │  │ libgcl │  │ libfs  │
              │ actors │  │ render │  │ collisn│  │ script │  │ disc   │
              │ memory │  │ KMD    │  │ HZD    │  │ bytecd │  │ stream │
              │ cache  │  │ OT/PRIM│  │ trap   │  │ vars   │  │        │
              └────────┘  └────────┘  └────────┘  └────────┘  └────────┘
                                           │
                                           ▼
              ┌────────┐  ┌─────────┐  ┌─────────┐  ┌──────────────────┐
              │  mts   │  │ libgte  │  │  sound/ │  │  PSX BIOS / GPU  │
              │  task  │  │ matrix  │  │  SPU    │  │  CD     SIO       │
              └────────┘  └─────────┘  └─────────┘  └──────────────────┘
```

## Tier 1 — engine libraries (`source/lib*`)

Portable, no game-state dependencies. Each one owns a single
concern.

### `libgv/` — actor / memory / cache

The runtime backbone. Three big pieces:

- **Actor system** ([`actor.c`](../../../source/libgv/actor.c)) —
  9-level priority list (`gActorsList_800ACC18[0..8]`). Each actor
  is a struct with `Act` (per-tick) and `Die` (destructor)
  callbacks. `GV_ExecActorSystem()` walks levels HIGH → LOW once
  per tick.
- **Memory pool** ([`memory.c`](../../../source/libgv/memory.c)) —
  three heaps (`PACKET0`, `PACKET1`, `NORMAL`) with bump + free
  list. `GV_Malloc` / `GV_Free`. Resident vs transient bookkeeping
  via `GV_SaveResidentFileCache`.
- **Cache** ([`cache.c`](../../../source/libgv/cache.c)) — keyed
  by `(name_hash, ext)` 32-bit ID via `GV_CacheID(name, 'k')`.
  Linear-probe table sized `MAX_CACHE_TAGS`. Loaders register `'k'`
  (KMD), `'h'` (HZD), `'p'` (PCX), `'g'` (GCL bytecode), `'o'`
  (motion), etc.

Also: `pad.c` (controller polling), `message.c` (`GV_SendMessage` /
`GV_ReceiveMessage`), `strcode.c` (the 5-bit-rotate name hash),
`math.c` (fixed-point helpers), `quat.c` (quaternion ops),
`debug.c` (`GV_DumpActorSystem`).

### `libdg/` — graphics

PSX GPU command builder + scene graph. Vocabulary:

- `DG_DEF` — a KMD's data: `n_models`, `min`/`max` bbox, then a
  trailing `DG_MDL[]`. Each `DG_MDL` holds vertices, materials,
  face indices, and a `parent` index for skeletal bone hierarchy.
- `DG_OBJS` — render instance for a `DG_DEF`. Wraps a `world`
  matrix, group id, channel binding, and a tail of `DG_OBJ[]`
  (one per model). `DG_MakeObjs(def)` allocates one;
  `DG_QueueObjs` adds it to the channel's render queue.
- `DG_CHANL` — viewport / camera. Three slots
  (`DG_Chanls[0..2]`): main 3D, HUD overlay, radar. Holds
  `eye`/`eye_inv` matrix, OT, draw env.
- `DG_LookAt(chanl, eye, center, clip_distance)` — set the view.
- The pipeline runs as Bound → Trans → Shade per channel: cull,
  world→eye, per-vertex lighting; then OPACK builds GPU primitives
  and queues them in the OT.

### `libhzd/` — collision + zones

HZD blob format: walls, floors, ceilings, zones, routes, traps,
cameras, all packed into one `.hzd` per stage.

- `HZD_HDL` — the loaded handle, owns the parsed structures.
- `HZD_GetAddress(hzd, &pos, level)` — find the bucket containing
  a world point.
- Trap/event system: `HZD_TRP` records bind a region to a script
  proc id; `HZD_SetEvent` registers a CONTROL with the trap
  dispatcher; the scripted character entering the trap fires its
  bound proc.

### `libgcl/` — bytecode scripting

Compiles `.gcl` text → `.gcx` bytecode (offline via
`tools/gcl2gcx.py`). Runtime walks the bytecode and dispatches
commands via a registered command table. See
[06-script-and-events.md](#) for details once it lands.

Key entry points:

- `GCL_StartDaemon` — register `'g'` cache loader.
- `GCL_LoadScript(blob)` — parse a `.gcx` into the global
  `current_script`.
- `GCL_ExecScript()` — walk the body, call command handlers.
- `GCL_ExecProc(proc_id, work)` — call one proc by id (used for
  trap callbacks, mesg responses).
- Variable spaces: `$w:` (16-bit world), `$b:` (8-bit byte), `$f:`
  (1-bit flag), `$s:` (string code = name hash), `t:` (literal),
  `d:` (define / constant).

### `libfs/` — disc + streaming

PSX CD-ROM access. Two paths:

- **Synchronous file** — `FS_LoadFileRequest` reads sectors from
  a known LBA into RAM. Used for one-shot KMD/HZD/PCX loads.
- **Stream** — `FS_StreamGetData(target_type)` pulls per-frame
  records out of a streaming buffer fed by the CD via interrupt.
  Used for cinematics (`DEMO.DAT`) and voice (`VOX.DAT`).

The port replaces both with a `PortFile` abstraction over either an
extracted directory or a disc image — see
[`port/libfs/libfs.c`](../../../port/libfs/libfs.c).

### `libgte/` — fixed-point math

Wraps the PSX GTE coprocessor: matrix multiply, vector rotate,
perspective divide, square root. Most game code uses
`RotMatrixZYX_gte`, `MulMatrix`, `ApplyMatrixLV`, `RotTransPers`.

### Other small libs

- `libsio/` — serial port. Stubbed in port; debug-only on disc.
- `libsn/` — PSYQ standard library. Replaced with libc on port.
- `mts/` — multi-tasking scheduler (sound, CD, pad). The port
  replaces this with single-threaded callbacks on the main loop.

## Tier 2 — game policy (`source/game/`)

Sits on top of the engine libraries. Holds *gameplay* invariants:

- **`gamed.c`** — the master `GameWork` actor. Drives stage
  loading, status flags, pause, GameOver. See
  [02-game-loop.md](02-game-loop.md).
- **`control.c` + `motion.c`** — every spatial actor wraps a
  `CONTROL` struct (position, rotation, collision step). Skeletal
  characters add a `MOTION_CONTROL` for animation playback. See
  [03-control-and-motion.md](03-control-and-motion.md).
- **`camera.c`** — game-aware camera that handles zone cameras,
  first-person mode, shake, interpolation. Writes
  `gUnkCameraStruct2_800B7868` which `DG_LookAt` consumes.
- **`script.c`** — registers the engine-side GCL commands
  (`mesg`, `chara`, `light`, `map`, `delay`, `pad`, `sound`,
  `radio`, `jimaku`, `pad`, `cinema`, …).
- **`item.c`** — pickups, inventory, item-effect dispatch.
- **`area.c` + `map.c`** — coordinates → map/region resolution,
  HZD lookup wrappers.
- **`alert.c`** — alert-state machine (off / caution / alert),
  noise tracking, alarm cooldown.
- **`pad.c`** — translates raw pad bits into game inputs
  (`GM_PadData`, `GM_PadMask`, `GM_OriginPad`).
- **`target.c`** — attack-target management for AI.

Each file in `game/` is small and policy-shaped. None of it
speaks directly to the GPU; rendering goes via `libdg`.

## Tier 3 — actors

Concrete game characters / objects, one per chara hash:

- **`chara/`** — the player and player-side actors. `snake/` is
  the bulk; `hind2/` is the helicopter; `others/` has
  `intr_cam.c` (intro camera), `motse.c`, etc.
- **`enemy/`** — guards. `WATCHER`, `COMMANDER`, `ZAKO11A` etc.
  Each follows the *think → command → action → check* pattern,
  with shared logic in `think.c` / `command.c` / `action.c` /
  `check.c`.
- **`animal/`** — `meryl72/`, `zako11e/`, `zako11f/`,
  `doll/demodoll.c` (cinematic puppet — see
  [doc/demo/03-key-actors.md](../demo/03-key-actors.md)).
- **`weapon/`** — weapon actors (FAMAS, SOCOM, GRENADE, etc.).
- **`bullet/`** — bullet / projectile actors. Lifetime tied to
  weapon firing.
- **`equip/`** — wearables (BODYARM, CIGS, GASMASK, BOX) —
  modifiers on the player rather than standalone characters.
- **`thing/`** — non-character actors (DOOR, EMITTER, SIGHT).
- **`anime/`** — particle / effect actors (smoke, breath, blood).

Each actor file has a `New<Name>` factory that the GCL `chara`
command resolves through `MainCharacterEntries[]` (registered
globally) or `StageCharacterEntries[]` (per-stage overlay).

## Tier 4 — overlays + per-stage

`source/stage/` and `source/overlays/` hold per-stage code that
isn't always linked. The disc swaps overlays in/out of an overlay
RAM region as the player moves between stages. Each overlay
contributes its own chara table (`StageCharacterEntries_<stage>[]`)
and its own scenerio.gcx + demo.gcx blobs in the DATACNF.

Overlays needed for cinematics live in `source/overlays/<stage>/`:

- `s11g/` — the Hind chase (boss + scripted camera).
- `s12a/`, `s12c/` — Wolf room (with `wolf2.c` driving cam).
- `s19b/` — Jeep escape (`democame.c` for the chase camera).

The port links *all* overlays statically — the editor and live
game both have everything available simultaneously, so the overlay
swapping is a no-op (`gcl_overlay.c::GCL_SwapOverlay` short-circuits
when the requested overlay is already resident).

## Where to look when…

| Symptom | Place to start |
| ------- | -------------- |
| Player won't move | `chara/snake/sna_init.c` (init) → `sna_act.c` (Act) → `control.c::GM_ActControl` (collision) |
| Camera in wrong place | [05-camera-system.md](#) once it lands; until then: `game/camera.c::Act` + watch `GM_GameStatus` and `GV_PauseLevel` |
| GCL command does nothing | `game/script.c::GM_ScriptCmds[]` table — is the command registered? |
| Trap fires twice | `libhzd/event.c` and `evpanel.c::CheckEvents` |
| Item pickup doesn't trigger | `game/item.c::GM_ActItem` + `GM_PickupItem` |
| Cutscene plays wrong characters | `kojo/demo.c::CreateDemo` and the DMO_DEF.models[] table — see [doc/demo/](../demo/) |
| Save/load broken | `memcard/` + `GCL_SaveVar` / `GCL_LoadVar` in `libgcl/variable.c` |
| Sound stuck or missing | `sound/sd_*.c` + `mts/sd_main.c` + `game/sound.c` |
