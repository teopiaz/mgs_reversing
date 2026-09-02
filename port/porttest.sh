#!/usr/bin/env bash
# Unattended run of the port: boot to a stage, grab frames, exit on its own.
#
#   ./porttest.sh <stage> [run_frames] [shot_frames]
#   ./porttest.sh s00a                      # 1800 frames, shots at 900/1500/1790
#   ./porttest.sh s01a 3000 1200,2400,2990
#   ./porttest.sh -    600  300,580         # "-" = no stage jump (title screen)
#
# Output: /tmp/mgstest/<stage>/<stage>_<frame>.png  and  run.log
# Set MGS_ISO to override the disc image.
set -uo pipefail
cd "$(dirname "$0")"

STAGE="${1:-s00a}"
FRAMES="${2:-1800}"
SHOTS="${3:-}"
if [ -z "$SHOTS" ]; then
    SHOTS="$((FRAMES/2)),$((FRAMES*5/6)),$((FRAMES-10))"
fi

ISO="${MGS_ISO:-../../mgs_iso/Metal Gear Solid - Integral (Japan, Asia) (En,Ja) (Disc 1).cue}"
if [ ! -f "$ISO" ]; then echo "no disc image at: $ISO" >&2; exit 1; fi

OUT="/tmp/mgstest/$STAGE"
rm -rf "$OUT"; mkdir -p "$OUT"

env_stage=()
if [ "$STAGE" != "-" ]; then
    env_stage=(PORT_AUTOLOAD_STAGE="$STAGE")
else
    env_stage=(PORT_SKIP_MENU=1)
fi

echo "stage=$STAGE frames=$FRAMES shots=$SHOTS -> $OUT"
env PORT_GL=1 "${env_stage[@]}" \
    PORT_RUN_FRAMES="$FRAMES" PORT_SHOT_AT="$SHOTS" \
    PORT_SHOT_DIR="$OUT" PORT_SHOT_TAG="$STAGE" \
    ./mgs "$ISO" > "$OUT/run.log" 2>&1
rc=$?

echo "exit=$rc"
grep -aE "harness|auto-load|LoadReq|screenshot\]|=== CRASH|Signal caught|ABORT" "$OUT/run.log" | head -30
echo "--- frames ---"; ls -1 "$OUT"/*.png 2>/dev/null || echo "(none)"
exit $rc
