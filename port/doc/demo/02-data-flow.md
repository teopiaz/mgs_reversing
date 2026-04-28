# Data flow: disc → cutscene playback

End-to-end byte path from the ISO to a frame on screen.

## On-disc layout

The MGS disc keeps every stage's assets bundled in one DATACNF blob
under `MGS/STAGE.DIR`. Each stage's blob contains:

```
DATACNF
├── 'r' (resident) DAR
│     ├── KMD geometry blobs
│     ├── HZD collision blob
│     ├── PCX texture blobs
│     └── …other resident assets
├── 'g' (GCL bytecode) cache region
│     ├── 0x6ea54 — gv_strcode("scenerio") | ('g'<<16)
│     │             scenerio.gcx (gameplay script)
│     └── 0x6a242 — gv_strcode("demo")     | ('g'<<16)
│                   demo.gcx (cutscene script) — present iff stage has one
└── 'n' (no-cache) section
      └── streamed wave / vox / texture chunks
```

Some stages have only `scenerio.gcx`; ~40 stages have both. Custom
stages built by `tools/import_stage.py` always have a scenerio.gcx
(generated stub if the user didn't author one) and may have a
hand-written demo.gcx.

## Loading: FS_LoadStageRequest

When the engine — editor or live game — switches to a stage:

1. `FS_LoadStageRequest("d00a")` reads the DATACNF off the ISO,
   walks its tag table (`'r'` / `'g'` / `'c'` / `'n'`), and registers
   each entry in `GV_CacheSystem.tags[]` keyed by the `(name_hash,
   ext)` pair.
2. For each `'g'` entry, the loader fires the registered loader
   callback for that ext. `GCL_StartDaemon` registered
   `GCL_InitFunc` as the 'g' handler:

   ```c
   static int GCL_InitFunc(unsigned char *top, int id) {
       if (id == scenerio_code)
           GCL_LoadScript(top);
       return 1;
   }
   ```

   `scenerio_code` is set by `GCL_ChangeSenerioCode(demo_flag)` to
   either `0xea54` (scenerio) or `0xa242` (demo). Only the matching
   blob is loaded — the other one stays in the cache unused.

3. The matched blob lands in `current_script.proc_table` /
   `current_script.script_body`. The GCL system is now armed but
   nothing has executed yet.

## Execution: GCL_ExecScript

In the live game, `gamed.c`'s GameWork actor reaches the post-load
state (status >= 7) and runs:

```c
GM_ResetMap();
NewCameraSystem();          // spawn camera-driver actor
GCL_ExecScript();           // walk the script body, fire chara directives
```

`GCL_ExecScript` walks `current_script.script_body` from offset 3
(skipping the script header byte + length field). The walker
recognises three kinds of statement:

- **Command** — looks up the opcode in the registered command list
  (mesg / chara / map / mapdef / camera / light / delay / etc.) and
  calls its handler. Game-side commands are registered by
  `GM_InitScript` at startup.
- **Eval** — runs an expression for its side effect (variable
  assignment, increment).
- **If / Foreach / Block** — control flow.

The most consequential command for cutscenes is `chara`. Each
`chara &TYPE $s:NNNN …` invocation:

1. Resolves the type via `GM_GetCharaID(typeHash)` — looks through
   `MainCharacterEntries[]` (statically registered factories) and
   `StageCharacterEntries[]` (from the per-stage overlay's chara
   table). Returns a `NEWCHARA` constructor.
2. Calls the constructor, which `GV_NewActor`s the actor into one
   of the nine actor lists at the appropriate level.
3. The constructor parses the directive's options (`-p X Y Z`,
   `-m $s:HHHH`, `-t 30000`, etc.) and stores them on the actor's
   `Work` struct.

After the walk, `GCL_ExecScript` returns and the actor system is
populated. From here onwards, `GV_ExecActorSystem` is what advances
state.

## Per-frame execution: GV_ExecActorSystem

Called once per render frame (in the live game by main_game.c's
main loop; in the editor by `ed_demo_tick` when the Demo Player
state is `PLAYING`).

For each actor list level (0..8), it walks the linked list and calls
each actor's `Act()` callback. Within one tick, that's typically:

| Level | Typical occupants | Notes |
| ----- | ----------------- | ----- |
| 0 | `gvd.c` daemon | always alive, processes `LoadReq` etc. |
| 1 | (unused in most cutscenes) | |
| 2 | `camera.c` (player/cutscene cam driver) | reads `gUnkCameraStruct2_800B7868`, calls `DG_LookAt(DG_Chanl(0), …)` |
| 3 | `cinema.c` (fade bars), `wt_view.c` (water visuals), `demothrd.c` (when streamed) | streamed cutscenes' DemoWork actor lives here; writes `gUnkCameraStruct2` from `.dmo` records each tick (see [09-streamed-demos.md](09-streamed-demos.md)) |
| 4 | `pato_lmp.c`, lamp / lighting actors | |
| 5 | `wall.c`, `shakemdl.c`, geometry / FX | |
| 6 | (overlay-specific) | |
| 7 | (unused in most cutscenes) | |
| 8 | (system) | |

For streamed cutscenes the camera-animation source lives at level 3
(`demothrd.c::StreamAct` calls `FrameRunDemo`), and the camera-driver
at level 2 consumes its writes within the same tick — levels walk
HIGH → LOW (`for (i = GV_ACTOR_LEVEL; i > 0; i--)`), so the order
is correct without any dependency inversion. For GCL-scripted-only
cutscenes there's no level-3 camera writer in the base actor set;
some per-stage overlays add their own (see
[04-camera-pipeline.md](04-camera-pipeline.md)).

## Render: port_RenderObjects → gl_renderer → screen

After all actors tick, the engine walks the OT (ordering table) and
the `DG_OBJS` list. `port_RenderObjects(GV_Clock)`:

1. Resets clip rect + draw offset
2. Clears the per-frame Z-buffer
3. Calls `gl_renderer_begin_3d()` — drops last frame's submitted tris
4. Runs `DG_BoundChanl / DG_TransChanl / DG_ShadeChanl` on each
   channel — the PSX-replica vertex pipeline (frustum cull, world →
   eye transform, per-vertex shading)
5. Walks `DG_OBJS` and submits every model's faces as `gl_submit_tri3d`
   batches keyed off the engine's current `eye_inv` from
   `DG_Chanls[0].eye_inv` (set by `camera.c` two steps earlier)

`gl_renderer_present()` is then called once per OS-window frame: it
uploads any dirty VRAM rows for textured fills, runs the GL draw of
the accumulated tri buffer, and swaps the window. The cinematic
camera shows up because every face was projected through the
`eye_inv` matrix that the camera source (streamed `FrameRunDemo` or
a per-stage overlay actor — see
[04-camera-pipeline.md](04-camera-pipeline.md)) → `gUnkCameraStruct2`
→ `DG_LookAt` produced.

## Stop / reset

In the live game, end-of-cutscene typically triggers a `load`
command pointing at the next stage; FS_LoadStageRequest unloads the
current DATACNF, the actor system is destroyed (or re-pooled), and
the new stage's GCL takes over.

In the editor's Demo Player, **Stop** calls `ed_load_stage(name)`,
which:

- `GV_InitMemorySystem(GV_NORMAL_MEMORY, …)` — wipes the GV pool
- `port_fs_unload_stage()` — drops the DATACNF buffer
- `GV_InitCacheSystem()` — clears cache entries
- `DG_InitTextureSystem()` — clears texture LUT
- Loads the stage fresh

That brute-forces a clean slate. The next **Play** has to re-load
demo.gcx + re-spawn everything.

## Custom-stage path

For a custom stage authored via `tools/import_stage.py`:

1. The user's `port/editor/assets/<stage>/scenerio.gcl` is compiled
   to `scenerio.gcx` by `tools/mgs_tools/stage/gcx_writer.py`
   (which calls `mgs_tools.gcl.compile`).
2. If the user dropped a `demo.gcl` next to it,
   `tools/mgs_tools/stage/gcx_writer.py` compiles that too as a
   second `'g'` entry. (This isn't fully wired yet — see
   [06-authoring.md](06-authoring.md).)
3. Both blobs land in the DATACNF as `'g'`-typed entries with the
   correct `gv_strcode("scenerio") / gv_strcode("demo")` hashes.
4. From the engine's perspective, custom and disc cutscenes are
   indistinguishable.
