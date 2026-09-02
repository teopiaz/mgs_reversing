#!/usr/bin/env bash
# Regression suite for the port. Design: port/doc/10-regression-tests.md
#
#   ./runtests.sh          all tiers (tier 3 skips if no disc image)
#   ./runtests.sh --quick  tiers 0-2 only (no disc, seconds)
set -uo pipefail
cd "$(dirname "$0")"
rc=0

echo "== Tier 0: compile & link invariants"
make mgs_tests -j8 >/dev/null 2>&1 || { echo "  FAIL: build (struct asserts live here)"; rc=1; }
[ $rc -eq 0 ] && echo "  ok: builds, static layout assertions hold"
./tests/t0_stubs.sh     || rc=1
./tests/t0_psx_addrs.sh || rc=1

echo
echo "== Tier 1-2: unit + shim parity"
./mgs_tests || rc=1

if [ "${1:-}" != "--quick" ]; then
    echo
    echo "== Tier 3: boot & assert"
    ./tests/t3_boot.sh || rc=1
fi

echo
[ $rc -eq 0 ] && echo "ALL PASS" || echo "FAILURES -- see above"
exit $rc
