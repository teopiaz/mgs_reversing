# Variables

GCL variables are typed cells backed by one of two global buffers:

- **`gGcl_vars_800B3CC8`** (local) — per-stage scratch, zeroed on
  stage start.
- **`linkvarbuf`** / `GM_*Flag` union (game-state) — persistent across
  stages; a subset persists into memory-card saves.

The ground truth is [source/libgcl/variable.c](../../../source/libgcl/variable.c).

## Sigil syntax

```
$<sigil>:<hex offset>
```

| Sigil | Type     | Size     | Notes                                           |
|-------|----------|----------|-------------------------------------------------|
| `$w:` | WORD     | 2 bytes  | signed 16-bit, the most common variable type   |
| `$b:` | BYTE     | 1 byte   | unsigned 8-bit                                 |
| `$f:` | FLAG     | 1 bit    | boolean, packed into a byte                    |
| `$s:` | STR_ID   | 2 bytes  | holds a hashed name                            |
| `$c:` | CHAR     | 1 byte   | rarely used                                    |
| `$p:` | PROC     | 2 bytes  | a proc hash (rare)                             |

## The encoded address

The 6 hex digits after the sigil decode to a packed 24-bit field:

```
bit  20 .. 19     18 .. 17 .. 16     15 .. 0
     ┌───────┐    ┌───────────┐    ┌───────────┐
     │ scope │    │ FLAG bit# │    │  offset   │
     └───────┘    └───────────┘    └───────────┘
```

- **scope** — `0x8x0000` marks game-state (`linkvarbuf`); otherwise
  local (`gGcl_vars_800B3CC8`).
- **FLAG bit #** — which bit inside the target byte (only for `$f:`).
- **offset** — byte offset into the scope buffer.

Example: `$f:04006E` decodes as:

- `04` → bit 4 of the byte
- `00` → scope = local
- `6E` → offset 0x6E bytes into `gGcl_vars_800B3CC8`

In practice you treat the 6 hex digits as an opaque identifier — the
decompiler emits them verbatim from the bytecode, and the compiler
encodes them back unchanged. What matters is that **the same
`$<sigil>:<hex>` always refers to the same cell**.

## Scope and lifetime

- **Local vars** (`gGcl_vars_800B3CC8`): cleared when you enter a new
  stage. Use for scratch state that only matters within the current
  scenerio execution.
- **Game-state vars** (game-state flag bit set): preserved across
  stage transitions. Subset survives to memory-card save via
  `GCL_SaveVar` / `GCL_RestoreVar`.

## STR_ID "variables" vs STR_ID literals

The `$s:` sigil is overloaded in the text format:

| Token          | Meaning                                                  |
|----------------|----------------------------------------------------------|
| `$s:XXXX`      | 4 hex digits → STR_ID **literal** (a hashed name value)  |
| `$s:XXXXXX`    | 6 hex digits → STR_ID **variable** (storage cell)        |

For example:

```gcl
eval($s:80000E = $s:7df9)
#    ^---- 6 hex: variable at offset 0x80000E
#                    ^---- 4 hex: hashed-string literal (0x7df9 = "main")
```

The decompiler pads/trims hex digits consistently so you can always
tell them apart. 4 hex = literal, 6 hex = storage.

## Common well-known variables

These appear in virtually every scenerio. Offsets from
[linkvarbuf.h](../../../source/include/linkvarbuf.h) and usage scanned
across all 135 vanilla scripts.

| Variable          | What it holds                                    |
|-------------------|--------------------------------------------------|
| `$w:000002`       | Camera mode / stage difficulty bias              |
| `$w:000004`       | General stage-setup scratch                      |
| `$w:000044`       | Player face direction (yaw seed)                 |
| `$b:000000`       | Stage counter, incremented per visit             |
| `$b:000049`       | Menu / map-selection index                       |
| `$f:000001`       | "Demo playing" flag                              |
| `$f:0002CC`       | "Player has bandana" flag                        |
| `$w:800010..14`   | Initial Snake spawn XYZ (game-state, persists)   |
| `$w:800016..18`   | Snake bust size / scale                          |
| `$w:80001C / 1E`  | Camera yaw/pitch seeds                           |
| `$w:80007A`       | Difficulty level (1 = easy, 2 = normal, 3 = hard)|
| `$s:80000E`       | Current map ID (hashed stage name)               |

## FLAG bit-packing

FLAG variables reuse the offset byte — multiple `$f:` at the same
offset just set different bits of the same byte:

```gcl
$f:000001   # bit 0 of byte 0
$f:010001   # bit 1 of byte 0
$f:020001   # bit 2 of byte 0
$f:030001   # bit 3 of byte 0
```

8 flags per byte. The decompiler/compiler handle this transparently;
you only need to remember that `$f:0X...Y` aliases other `$f:0*...Y`.

## Assigning and reading

```gcl
eval($w:800010 = -2000)                 # literal assignment
eval($b:000049 = $b:000049 + 1)         # increment
eval($f:000001 = false)                 # clear flag
eval($f:0002CC = true)                  # set flag
eval($s:80000E = $s:7df9)               # assign hashed string
if ($w:80007A == 3) { ... }             # read in condition
if ($f:0002CC && $b:000049 < 5) { ... } # combined
```

The expression machinery is described in [03-expressions.md](03-expressions.md).

## Persistence into save files

`varsave` copies values from the live linkvarbuf into the save-buffer
copy (`sv_linkvarbuf`). Used for things you want to survive reloads:

```gcl
varsave $w:0002CA     # persist this variable on next save
```

Not all game-state vars are saved — only those explicitly covered by
`varsave` calls during scenerio execution.
