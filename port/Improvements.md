# Rendering & Cutscene Improvements Plan

## Current State (verified against PSX emulator)
- Camera matrices match PSX (eye_m, eye_inv verified frame-by-frame)
- Camera positions from DEMO.DAT stream match exactly
- GTE rotation functions (RotMatrixYXZ, RotMatrixZYX, ratan2, MulRotMatrix) verified
- Software rasterizer works (affine texturing, z-buffer, backface cull)
- Snake mesh animation correct during gameplay

## Remaining Issues

### Issue 1: Cutscene stops after first frame
**Root cause**: `demothrd_8007CFE8` returns 0 because `adjust->type` doesn't match any model in `work->header->models`. This causes `FrameRunDemo` to return 0, which triggers `FS_StreamStop()`.

**PSX diagnostic prints needed**:
```c
// In demothrd_8007CFE8 (demo.c ~line 2120):
printf("[PSX] adj type=%d n_models=%d\n", adjust->type, work->header->n_models);
for (i = 0; i < work->header->n_models; i++)
    printf("[PSX]   model%d type=%d\n", i, model_file[i].type);
```

**Port diagnostic prints needed**:
```c
// Same location, #ifdef PORT_BUILD:
printf("[PORT] adj type=%d n_models=%d\n", adjust->type, work->header->n_models);
```

**Likely fix**: The `work->header->models` pointer may be wrong because the DMO_DEF conversion in `demothrd.c` computes the models offset from `raw` but the header is then copied via `*work->header = *header` in CreateDemo — the pointer becomes stale after the stream entry is cleared.

### Issue 2: Frustum culling broken (DG_BoundChanl)
**Root cause**: `bound.c` has 64-bit issues — `long*` stride (8 bytes vs 4), scratchpad pointer arithmetic, GTE bounding box projection uses addresses that get truncated.

**PSX diagnostic prints needed**:
```c
// In DG_BoundChanl (bound.c ~line 338):
printf("[PSX] obj%d bm=%d n=%d fl=0x%x\n", qi, current_objs->bound_mode, current_objs->n_models, flag);
```

**Port diagnostic**: Already prints same format. Compare:
- PSX: `bm=2` (visible) for nearby objects, `bm=0` (culled) for distant
- Port: Currently all `bm=0` (everything culled → frustum culling disabled)

**Fix approach**: Audit every pointer cast in bound.c for 64-bit correctness. Key locations:
- Line 137/317: `(long*)(SCRPAD_ADDR + 0x6C)` → `(int*)`
- Line 304/308: `test = (long*)` → `test = (int*)`  
- GTE operations that store to scratchpad via `gte_stsz3c`, `gte_stsxy3c`

### Issue 3: Missing cutscene character models
**Root cause**: `MakeChara` calls `GM_GetCharaID(type)` with types 1-28, but no character in `MainCharacterEntries` has these class_ids. The characters are cutscene-specific (DEMODOLL, FADEIO, TELOP etc.) and their class_ids are hash values (0xe97e, 0x3453, etc.), not the small integers MakeChara uses.

**PSX diagnostic prints needed**:
```c
// In MakeChara (demo.c ~line 564):
printf("[PSX] MakeChara type=%d funcptr=%p\n", data->field_4_type, GM_GetCharaID(data->field_4_type));
```

**Key insight**: MakeChara doesn't call `GM_GetCharaID(data->field_4_type)` directly for all types. Each case in the switch statement calls `GM_GetCharaID(N)` with a DIFFERENT number than `field_4_type`. For example, type 0x3 calls `GM_GetCharaID(3)`, type 0x1C calls `GM_GetCharaID(0x1C)`. These small integers must be registered somewhere — check if the stage overlay's character table has entries with `class_id = 3, 4, 5...`.

**Fix**: Add cutscene character entries to the stage overlay tables, or map them to existing functions (e.g., `NewDoll_800DCD78` for demodoll).

### Issue 4: DMO_DEF models pointer becomes stale
**Root cause**: In `CreateDemo` (demo.c:73-79):
```c
work->header = GV_Malloc(sizeof(DMO_DEF));
*work->header = *header;  // copies the struct, including the models pointer
```
The `header->models` pointer points into the stream buffer. After `FS_StreamClear(data)` is called, the stream entry's type byte is cleared but the data remains valid. However, if the stream buffer is reused (circular), the models pointer could become stale.

**PSX diagnostic**:
```c
// After CreateDemo:
printf("[PSX] header->models=%p n_models=%d\n", work->header->models, work->header->n_models);
printf("[PSX] model0 type=%d flag=%d\n", work->header->models[0].type, work->header->models[0].flag);
```

### Issue 5: Extra geometry rendered (no frustum culling)
**Root cause**: With `DG_BoundChanl` broken, all 160+ map models render regardless of visibility. PSX renders 200-800 faces; port renders 2000-3600.

**Performance impact**: Mostly fixed by rasterizer rewrite (affine + fixed-point + inline texel). But still wastes CPU on vertex transforms for off-screen geometry.

**Fix**: Fix `DG_BoundChanl` (Issue 2) to properly cull off-screen objects.

## PSX Emulator Diagnostic Summary

| What to print | Where (PSX code) | Purpose |
|---|---|---|
| `adjust->type` vs `model_file->type` | `demo.c` demothrd_8007CFE8 | Why adjust matching fails |
| Model table after CreateDemo | `demo.c` CreateDemo | Verify models pointer valid |
| `bound_mode` per object | `trans.c` DG_TransChanl | Compare frustum culling |
| `DG_CurrentGroupID` | `dgd.c` or `display.c` | Verify group filtering |
| `MakeChara` type + funcptr | `demo.c` MakeChara switch | Which characters have functions |
| `n_charas` + `n_adjusts` per frame | `demothrd.c` StreamAct | Verify stream parsing |

## Priority Order
1. **Fix cutscene stopping** (Issue 1 + 4) — adjust matching + models pointer
2. **Fix frustum culling** (Issue 2) — bound.c 64-bit audit
3. **Add cutscene characters** (Issue 3) — character table mapping
4. **Performance** (Issue 5) — depends on Issue 2
