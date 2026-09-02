#!/usr/bin/env bash
# Tier 3 -- boot a stage and assert on the log. Needs the disc image.
#
#   ./t3_boot.sh [stage ...]          default: s00a s01a s04c d01a
#   MGS_ISO=<path>                    override the disc image
#
# Each of these assertions corresponds to a bug that shipped:
#   binds over            -> gBindsArray overflow (GM_ResetScript stubbed out)
#   Double Pcm            -> stream never torn down (FS_StreamIsForceStop)
#   Stream:File Pos Error -> ditto, downstream
#   [ot] ABORT            -> ordering-table cycle from corrupted map masks
#   TRY/OPEN loop         -> CD shims not writing result[0]
#   snake=(0,0,0)         -> stalled boot (the playbook's stall signature)
set -uo pipefail
cd "$(dirname "$0")/.."

STAGES=("$@"); [ ${#STAGES[@]} -eq 0 ] && STAGES=(s00a s01a s04c d01a)
ISO="${MGS_ISO:-../../mgs_iso/Metal Gear Solid - Integral (Japan, Asia) (En,Ja) (Disc 1).cue}"
FRAMES="${T3_FRAMES:-1200}"

if [ ! -f "$ISO" ]; then
    echo "  SKIP: no disc image at $ISO (set MGS_ISO)"; exit 0
fi

rc=0
for stage in "${STAGES[@]}"; do
    out=/tmp/mgstest_t3/$stage; rm -rf "$out"; mkdir -p "$out"
    PORT_GL=1 PORT_AUTOLOAD_STAGE="$stage" PORT_RUN_FRAMES="$FRAMES" \
        ./mgs "$ISO" > "$out/run.log" 2>&1
    exit_code=$?
    log="$out/run.log"
    bad=""

    [ $exit_code -ne 0 ] && bad="$bad\n    process exited $exit_code"

    for pat in "=== CRASH" "Signal caught" "\[ot\] ABORT" "binds over" \
               "Double Pcm" "Stream:File Pos Error" "FATAL: no game data" \
               "NOT FOUND" "command not found"; do
        n=$(grep -acE "$pat" "$log" || true)
        [ "$n" -gt 0 ] && bad="$bad\n    saw '$pat' x$n"
    done

    for pat in "LoadReq $stage" "exec scenario" "end scenario"; do
        grep -qaE "$pat" "$log" || bad="$bad\n    missing '$pat'"
    done

    last=$(grep -a "\[tick" "$log" | tail -1)
    if [ -z "$last" ]; then
        bad="$bad\n    no [tick] lines at all"
    else
        echo "$last" | grep -qa "hp=256/256" || bad="$bad\n    last tick not hp=256/256: $last"
        echo "$last" | grep -qa "snake=(0,0,0)" && bad="$bad\n    Snake stuck at origin: $last"
    fi

    nf=$(grep -ac "func not found" "$log" || true)

    if [ -n "$bad" ]; then
        printf "  FAIL %-6s%b\n" "$stage" "$bad"; rc=1
    else
        printf "  ok   %-6s (%s, %d dropped charas)\n" "$stage" \
               "$(echo "$last" | sed 's/.*snake=/snake=/')" "$nf"
    fi
done
exit $rc
