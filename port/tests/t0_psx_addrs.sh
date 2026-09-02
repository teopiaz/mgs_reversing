#!/usr/bin/env bash
# Tier 0.3 -- raw PSX addresses in dispatch tables.
#
# source/stage/s04b.c:9 registers { 0xb99f, (NEWCHARA *)0x800dcdbc }. On the
# port, port_chara_override() drops any constructor below 0x100000000 as a
# stale PSX pointer, so the actor silently never spawns -- the symptom is a
# missing character, not an error. Stages whose tables were migrated to real
# symbols (e.g. source/stage/s04c.c) work; the rest do not.
#
# This pins the current set. A rebase that ADDS entries is reported; one that
# removes them prompts a baseline refresh. Regenerate with --update.
set -uo pipefail
cd "$(dirname "$0")/../.."

BASE=port/tests/baseline_psx_addrs.txt
found=$(grep -rn "(NEWCHARA \*)0x8" source/stage source/stagevr 2>/dev/null \
        | sed 's/:[0-9]*:/:/' | awk -F: '{print $1}' | sort | uniq -c \
        | awk '{printf "%s %s\n", $2, $1}' | sort)

if [ "${1:-}" = "--update" ]; then
    printf '%s\n' "$found" > "$BASE"
    echo "  baseline updated: $(printf '%s\n' "$found" | grep -c . ) files"
    exit 0
fi

if [ ! -f "$BASE" ]; then
    echo "  FAIL: no baseline at $BASE (run with --update)"; exit 1
fi

total=$(printf '%s\n' "$found" | awk '{s+=$2} END{print s+0}')
newf=$(comm -13 <(sort "$BASE") <(printf '%s\n' "$found" | sort) || true)
gone=$(comm -23 <(sort "$BASE") <(printf '%s\n' "$found" | sort) || true)

echo "  $total raw PSX constructors across $(printf '%s\n' "$found" | grep -c .) stage tables"
[ -n "$gone" ] && printf '  note: baselined entry changed/resolved, refresh with --update:\n%s\n' "$gone"
if [ -n "$newf" ]; then
    printf '  FAIL: NEW raw PSX addresses in stage tables:\n%s\n' "$newf"
    echo "  -> these actors will be dropped by port_chara_override(); add a"
    echo "     mapping in port/chara_overrides.c or migrate the table to symbols"
    exit 1
fi
echo "  ok: no new raw PSX addresses"
