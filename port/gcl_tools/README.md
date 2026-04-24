# GCL Tools

Offline tooling for the GCL scripting language used by MGS1 PSX stage
scripts (`*.gcx` bytecode). Supports byte-exact round-trip:

```
.gcx  -->  gcx2gcl  -->  .gcl (+ .tail)
.gcl  -->  gcl2gcx  -->  .gcx
```

All 135 stage scripts in the vanilla game round-trip byte-exact through
the text form.

## Usage

```
# Decompile
python3 gcx2gcl.py path/to/00ea54.gcx -o /tmp/scen.gcl --trailing /tmp/scen.tail

# Recompile
python3 gcl2gcx.py /tmp/scen.gcl --trailing /tmp/scen.tail -o /tmp/scen.gcx --no-align4
```

Rex (archive extractor) emits `.gcx` files with hash-based names:

- `00ea54.gcx` — `scenerio` script (main per-stage)
- `00a242.gcx` — `demo` script

### --trailing

`.gcx` files contain GCL bytecode followed by a trailing blob of font/raw
data that isn't GCL. The decompiler extracts that blob to the file you
pass to `--trailing`, and the compiler splices it back in verbatim. Byte-
exact round-trip requires this pair.

### --no-align4

The PSX build appends 0-padding to a 4-byte boundary. Pass `--no-align4`
to skip the pad (useful when the source `.gcx` had no pad to begin with).

## Glyph inspection

Stage `.gcx` files embed a per-stage font in the trailing blob — 12×12
2-bit-per-pixel bitmaps indexed by 2-byte codes ≥ 0x9A00 (bank 2, as
computed by [font.c `font_get_glyph_index_80044FF4`](../../source/font/font.c)).
Labeling them lets you expand [mgs_chars.py](mgs_chars.py) so more
`{XXXX}` markers render as readable glyphs.

```
# ASCII art of every bank-2 glyph used in one file
python3 show_glyphs.py path/to/00ea54.gcx

# Every glyph in the trailing blob (including unused slots)
python3 show_glyphs.py --all path/to/00ea54.gcx

# Dedupe across all stages; write a PGM sheet + labels template
python3 show_glyphs.py --scan /path/to/stages/ \
    --sheet glyphs.pgm --labels labels.txt
```

The vanilla game has ~580 unique bank-2 bitmaps across 135 scripts.
`glyphs.pgm` is a 338×352 greyscale grid openable in any image viewer;
`labels.txt` is a plain-text template with one line per bitmap for
filling in the Unicode character you recognise.

Bank 0 (font.res, ASCII + common kanji, shared across stages) isn't
rendered yet — that requires the game's `font.res` blob, which Rex
doesn't extract.

## Deriving the code→char table from WantedThing's corpus

If you have WantedThing's `.gcl` outputs alongside the original `.gcx`
files, [derive_table.py](derive_table.py) aligns the two and produces
[mgs_chars_derived.py](mgs_chars_derived.py) — a `{code: char}` dict
that [mgs_chars.py](mgs_chars.py) auto-merges on import:

```
python3 derive_table.py \
    --wt  /path/to/wantedthing/output/ \
    --rex /path/to/Rex/output/stage/   \
    -o   mgs_chars_derived.py
```

The aligner walks our `m"..."` and their `j"..."` strings in lockstep:
char↔char, 2-byte code↔char, and 2-byte code↔`\xHI`+ASCII-low splits
are all handled. Only strings whose tokens fully consume both sides
contribute mappings; anything structurally ambiguous is dropped.

Seed entries from [font.c](../../source/font/font.c) take priority over
derived; conflicts across stages (same code → different chars in
different files) get discarded so neither guess poisons the table.
ASCII chars are normalised to fullwidth at import time — if the derived
char was `7` and `１` shares its byte range, we use `７` to avoid
double-widening a single-byte ASCII `7` during encoding.

Current result on the vanilla corpus: **~166 total mappings** (80 seeded +
~86 derived), including full-width alphabet/digits, common katakana,
and a couple hundred kanji used in briefing text. Round-trip stays
byte-exact for all 135 files.

## Modules

- [gcx.py](gcx.py) — big-endian bytecode buffer + `GclNode` AST container.
- [constants.py](constants.py) — `GclCode` opcodes, `GclOperator` table,
  known command hashes (`GclCommand`), and `gv_strcode()` (replica of
  [strcode.c:27](../../source/libgv/strcode.c#L27)).
- [gcl_decompile.py](gcl_decompile.py) — bytecode → AST.
- [gcl_writer.py](gcl_writer.py) — AST → `.gcl` text.
- [gcl_parser.py](gcl_parser.py) — `.gcl` text → AST (recursive descent).
- [gcl_compile.py](gcl_compile.py) — AST → bytecode.
- [gcx2gcl.py](gcx2gcl.py) — decompile CLI.
- [gcl2gcx.py](gcl2gcx.py) — compile CLI.
- [mgs_chars.py](mgs_chars.py) — code ↔ char table (seeded from
  [font.c](../../source/font/font.c)); grow as mappings are learned.
- [glyph_extractor.py](glyph_extractor.py) — bank/offset math, 12×12
  bitmap decode, PGM sheet output.
- [show_glyphs.py](show_glyphs.py) — inspect stage glyphs (single file,
  full trailing blob, or deduped scan across many).

## Text format cheat sheet

```gcl
proc sub_DF2A {                         # proc definition (hash in name)
    eval($f:000001 = 0)                 # variable assignment
    load "title" \                      # command with options
        -m $s:7df9                      # -letter option
        -s 1
}

script {                                # main script body
    eval($b:000000 = $b:000000 + 1 | 128)
    if ($w:800002 == 3) {
        eval($w:80007A = 1)
    } else {
        eval($w:80007A = 2)
    }
    chara $s:81c7 $s:81c7 \
        -e sub_1D3C "\x90c\x9A..."
    map -a $s:80000E
}
```

| Syntax           | Meaning                                   |
|------------------|-------------------------------------------|
| `$w:XXXXXX`      | WORD variable (6 hex = 3-byte encoding)   |
| `$b:XXXXXX`      | BYTE variable                              |
| `$f:XXXXXX`      | FLAG variable                              |
| `$s:XXXX`        | 4-hex: STR_ID hashed-string literal       |
| `$s:XXXXXX`      | 6-hex: STR_ID variable                    |
| `N`              | WORD literal (signed decimal, incl. `-N`) |
| `b:N`            | BYTE literal                               |
| `0xN`            | WORD literal (hex)                         |
| `"..."`          | ASCII string (`\xNN` escapes for non-ASCII)|
| `m"..."`         | MGS-encoded string: ASCII chars + `{XXXX}` markers for 2-byte glyph codes, or chars from the [mgs_chars](mgs_chars.py) table |
| `'c'`            | CHAR literal                               |
| `true` / `false` | FLAG literal                               |
| `sub_XXXX`       | proc reference                             |
| `argN`           | stack-arg reference (inside a proc)       |
| `-X`             | option prefix                              |
| `-!X`            | option with NULL_SIZE marker              |
| `-(N)`           | unary NEGATE OP (vs. `-N` = negative WORD)|

The `-N` vs `-(N)` distinction matters for byte-exact round-trip: the
original compiler uses direct negative WORD in some places and a
NEGATE OP elsewhere, and both forms appear in the shipped bytecode.

## Attribution

The binary-layout logic was cross-checked against the excellent
[mgs_compilation_tools](../../../psx/inspirations/mgs_compilation_tools/)
reference (JSON AST). This port strips extraneous dependencies (radio /
vox / demo resolution), adds a text parser/writer pair, and cleans up
the decompile path.

## Ground-truth references

| Concept                    | File                                                     |
|----------------------------|----------------------------------------------------------|
| Opcodes                    | [libgcl.h:76](../../source/libgcl/libgcl.h#L76)          |
| Expression operators       | [libgcl.h:95](../../source/libgcl/libgcl.h#L95)          |
| Bytecode walker            | [parse.c:20](../../source/libgcl/parse.c#L20)            |
| Block executor             | [command.c:170](../../source/libgcl/command.c#L170)      |
| Script loader              | [command.c:150](../../source/libgcl/command.c#L150)      |
| Variable encoding          | [variable.c:199](../../source/libgcl/variable.c#L199)    |
| Expression evaluator       | [expr.c](../../source/libgcl/expr.c)                     |
| Command table              | [game/script.c:1141](../../source/game/script.c#L1141)   |
| Built-in commands          | [basic.c](../../source/libgcl/basic.c)                   |
| Hash function              | [strcode.c:27](../../source/libgv/strcode.c#L27)         |
| Known hashes               | [strcode.h](../../source/include/strcode.h)              |
