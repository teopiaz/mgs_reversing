# vscode-gcl — GCL syntax highlighting for VS Code

Visual Studio Code language extension for the MGS1 Game Command Language
used by the [port/gcl_tools/](..) decompiler / compiler.

Adds:

- File association: `*.gcl` → `gcl` language id
- TextMate grammar covering proc / script blocks, all 28 commands,
  expressions, sigil variables (`$w:` / `$b:` / `$f:` / `$s:` / `$c:` /
  `$p:`), typed literals (`b:N`, `t:N`, `sd:N`), MGS-encoded strings
  (`m"..."` with `{XXXX}` glyph codes and `\xNN` escapes), proc refs
  (`sub_XXXX`), stack args (`argN`), dash options (including the
  `-!X` NULL_SIZE form), and the full operator set
- Comments: `#` line comments, including after backslash continuations

## Install (development mode)

The extension isn't published to the marketplace. Two ways to load it:

### A. Symlink into your VS Code extensions dir

```bash
ln -s "$PWD/port/gcl_tools/vscode-gcl" \
      "$HOME/.vscode/extensions/vscode-gcl-0.1.0"
```

Restart VS Code. Open any `.gcl` file — the bottom-right of the status
bar should show `GCL`.

### B. Package as `.vsix` and install

```bash
npm install -g @vscode/vsce
cd port/gcl_tools/vscode-gcl
vsce package           # produces vscode-gcl-0.1.0.vsix
code --install-extension vscode-gcl-0.1.0.vsix
```

## Files

| File                                     | Role                                  |
|------------------------------------------|---------------------------------------|
| [package.json](package.json)             | Extension manifest                    |
| [language-configuration.json](language-configuration.json) | Brackets, comments, autoclose |
| [syntaxes/gcl.tmLanguage.json](syntaxes/gcl.tmLanguage.json) | TextMate grammar       |

## Reference

The grammar's token names follow standard TextMate conventions
(`keyword.control.gcl`, `support.function.command.gcl`, etc.) so any
VS Code colour theme should pick them up automatically. Notable scopes:

| Scope                                     | What it tags              |
|-------------------------------------------|---------------------------|
| `keyword.control.conditional.gcl`         | `if` / `elseif` / `else`  |
| `keyword.control.eval.gcl`                | `eval(...)`               |
| `keyword.control.call.gcl`                | `call(...)`               |
| `support.function.command.gcl`            | the 28 commands           |
| `entity.name.function.gcl`                | `sub_XXXX` proc refs      |
| `variable.parameter.stack.gcl`            | `argN`                    |
| `variable.parameter.option.gcl`           | `-x` / `-!x`              |
| `variable.other.hashname.gcl`             | bare identifiers (becomes `GV_StrCode(name)`) |
| `storage.type.variable.gcl`               | sigil letter (`w`/`b`/`f`/`s`/`c`/`p`) |
| `constant.numeric.address.gcl`            | the hex after the sigil   |
| `constant.numeric.glyph-code.gcl`         | `{XXXX}` inside `m"..."`  |
| `constant.character.escape.hex.gcl`       | `\xNN` inside strings     |
| `string.quoted.double.gcl`                | ASCII `"..."` strings     |
| `string.quoted.double.mgs.gcl`            | MGS `m"..."` strings      |
| `keyword.operator.{assignment,logical,comparison,bitwise,arithmetic}.gcl` | operator categories |

## Caveats

- Line continuation: handled inside the TextMate grammar — both the
  bare `\` form and the `\` + `# comment` form (the latter is a small
  parser extension we added, see
  [gcl_parser.py](../gcl_parser.py)).
- The grammar can't validate semantics — e.g. it won't flag a wrong
  command hash or a missing proc. Use the round-trip workflow in
  [13-debugging.md](../../doc/gcl/13-debugging.md) for that.
