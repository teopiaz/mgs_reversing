#!/usr/bin/env bash
# Tier 0.2 -- no trivial stub may shadow a real implementation.
#
# GM_ResetScript was `{ return 0; }` in port/link_stubs.c while the real body
# lived in port/game_script_fix.c as GM_InitBinds. Upstream calls it from
# GM_ActInit on every stage start, so bind counts accumulated until trap/ntrap
# wrote past gBindsArray_800b58e0[128] and corrupted the globals behind it.
#
# Mark a deliberate stub with  /* STUB-OK: reason */  on the line above.
set -uo pipefail
cd "$(dirname "$0")/../.."          # repo root

python3 - "$@" <<'PY'
import re, os, sys

stub_files = []
for dp, dirs, fs in os.walk("port"):
    dirs[:] = [d for d in dirs if d not in ("obj", "imgui")]
    for f in fs:
        if f.endswith(".c") and "stub" in f:
            stub_files.append(os.path.join(dp, f))

trivial = {}
for path in stub_files:
    txt = open(path, errors="replace").read()
    lines = txt.split("\n")
    for m in re.finditer(
        r'^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{]*\)\s*\{([^{}]*)\}', txt, re.M):
        name, body = m.group(1), m.group(2)
        b = re.sub(r'\(void\)\s*\w+\s*;', '', body)
        b = re.sub(r'\breturn\s*[-\w]*\s*;', '', b).strip()
        if b:
            continue
        ln = txt[:m.start()].count("\n")
        if ln and "STUB-OK" in lines[ln-1]:
            continue
        trivial.setdefault(name, path)

# A symbol whose source counterpart is still `#pragma INCLUDE_ASM(".../<n>.s")`
# is legitimately stubbed -- it has no C body yet. Exclude those.
asm_backed = set()
for root, _, files in os.walk("source"):
    for fn in files:
        if not fn.endswith((".c", ".h")):
            continue
        txt = open(os.path.join(root, fn), errors="replace").read()
        if "INCLUDE_ASM" not in txt:
            continue
        for name in list(trivial):
            if name + ".s" in txt:
                asm_backed.add(name)
for name in asm_backed:
    trivial.pop(name, None)

bad = []
for root, _, files in os.walk("source"):
    for fn in files:
        if not fn.endswith(".c"):
            continue
        p = os.path.join(root, fn)
        txt = open(p, errors="replace").read()
        for name, stub_path in trivial.items():
            if re.search(r'^(?!static)[A-Za-z_][\w\s\*]*?\b' + re.escape(name) +
                         r'\s*\([^;]*?\)\s*\n?\s*\{', txt, re.M):
                bad.append((name, stub_path, p))

# The remainder is a known backlog (overlay constructors that really do live in
# a dynamically loaded overlay, plus genuinely-open items). Pin it: this test
# exists to catch a NEW shadowing introduced by a rebase, not to re-report work
# that is already tracked. Regenerate deliberately with --update.
found = sorted({f"{n}\t{s}\t{r}" for n, s, r in bad})
base_path = "port/tests/baseline_stub_shadow.txt"

if "--update" in sys.argv:
    with open(base_path, "w") as f:
        f.write("\n".join(found) + ("\n" if found else ""))
    print(f"  baseline updated: {len(found)} entries")
    sys.exit(0)

baseline = []
if os.path.exists(base_path):
    baseline = [l for l in open(base_path).read().split("\n") if l.strip()]

new_entries = [e for e in found if e not in baseline]
gone        = [e for e in baseline if e not in found]

print(f"  scanned {len(stub_files)} stub files, {len(trivial)} trivial "
      f"non-asm stubs, {len(found)} shadowing ({len(baseline)} baselined)")
for e in gone:
    print(f"  note: baselined entry resolved, drop it with --update: {e.split(chr(9))[0]}")
if new_entries:
    print("  FAIL: NEW trivial stub shadows a real implementation:")
    for e in new_entries:
        n, st, rl = e.split("\t")
        print(f"    {n:32s} stub:{st}  real:{rl}")
    sys.exit(1)
print("  ok: no new shadowing")
PY
