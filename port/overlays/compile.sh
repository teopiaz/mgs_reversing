#!/usr/bin/env bash
# Compile every overlay .gcl alongside its .tail (font blob) into a .gcx
# that byte-matches the vanilla original.
set -euo pipefail

TOOL_DIR="$(cd "$(dirname "$0")/../../tools" && pwd)"

for f in ./*/*.gcl; do
    tail_file="${f%.gcl}.tail"
    args=("$f" -o "${f%.gcl}.gcx" --no-align4)
    if [ -f "$tail_file" ]; then
        args+=(--trailing "$tail_file")
    else
        echo "warning: no trailing blob for $f — bank-2 glyphs will be junk"
    fi
    echo "Compiling $f"
    python3 "$TOOL_DIR/gcl2gcx.py" "${args[@]}"
done
