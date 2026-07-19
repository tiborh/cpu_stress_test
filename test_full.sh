#!/usr/bin/env bash
# test_full.sh — Full test suite for local machines with real hardware.
#
# Runs everything that CI does, PLUS hardware-dependent tests that require
# thermal sensors, actual CPU cores, and root-level access for some audits.
#
# Usage:
#   ./test_full.sh              # run all tests
#   ./test_full.sh --quick      # skip the stress test (faster)
#   ./test_full.sh --help       # show this help
#
# Exit codes:
#   0  All tests passed
#   1  One or more tests failed

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Options ───────────────────────────────────────────────────────────────────
QUICK=false
STRESS_DURATION=5

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)  QUICK=true; shift ;;
        --help|-h)
            sed -n '2,/^$/{ s/^# \?//; p }' "$0"
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── Helpers ───────────────────────────────────────────────────────────────────
PASS=0
FAIL=0
SKIP=0

run_test() {
    local name="$1"
    shift
    printf "  %-40s " "$name"
    if output=$("$@" 2>&1); then
        echo "PASS"
        ((PASS++))
    else
        echo "FAIL"
        echo "    $output" | head -5
        ((FAIL++))
    fi
}

run_test_allow_skip() {
    local name="$1"
    local reason="$2"
    shift 2
    printf "  %-40s " "$name"
    if output=$("$@" 2>&1); then
        echo "PASS"
        ((PASS++))
    else
        echo "SKIP ($reason)"
        ((SKIP++))
    fi
}

section() {
    echo ""
    echo "═══ $1 ═══"
}

# ── Record existing CSVs so we only clean up test-generated ones ──────────────
EXISTING_CSVS=$(ls results/*.csv 2>/dev/null | sort)

# ── 1. Build ──────────────────────────────────────────────────────────────────
section "Build (gcc)"
run_test "make clean" make clean
run_test "make (zero warnings)" make

section "Build (clang)"
if command -v clang &>/dev/null; then
    run_test "make clean" make clean
    run_test "make CC=clang" make CC=clang
else
    printf "  %-40s SKIP (clang not installed)\n" "make CC=clang"
    ((SKIP++))
fi

# Rebuild with default compiler for subsequent tests
make clean >/dev/null 2>&1
make >/dev/null 2>&1

# ── 2. Smoke tests ───────────────────────────────────────────────────────────
section "Smoke tests (make check)"
run_test "make check" make check

# ── 3. Dependency checker ─────────────────────────────────────────────────────
section "Dependency checkers"
run_test "check_build_deps.sh" ./check_build_deps.sh --quiet
run_test "check_build_deps.sh --build" ./check_build_deps.sh --build
run_test_allow_skip "check_audit_deps.sh" "audit tools not installed" ./check_audit_deps.sh --quiet

# ── 4. Hardware-dependent tests ───────────────────────────────────────────────
section "Hardware-dependent tests"

run_test "cpu_cores (returns > 0)" bash -c './cpu_cores | grep -qE "^[1-9][0-9]*$"'
run_test "cpu_id (non-empty output)" bash -c 'test -n "$(./cpu_id)"'
run_test "timestamp (format check)" bash -c './timestamp | grep -qE "^[0-9]{8}_[0-9]{6}$"'
run_test "cpu_temp (reads temperature)" bash -c './cpu_temp | grep -qE "[0-9]+\.[0-9]+°C"'
run_test "list_temps (finds sensors)" bash -c './list_temps | grep -qE "[0-9]"'

# ── 5. Stress test (hardware required) ───────────────────────────────────────
section "Stress test"

if [[ "$QUICK" == true ]]; then
    printf "  %-40s SKIP (--quick mode)\n" "cpu_stress math ${STRESS_DURATION}s"
    ((SKIP++))
    printf "  %-40s SKIP (--quick mode)\n" "cpu_stress urandom ${STRESS_DURATION}s"
    ((SKIP++))
else
    run_test "cpu_stress math ${STRESS_DURATION}s" ./cpu_stress auto "$STRESS_DURATION" math
    run_test "cpu_stress urandom ${STRESS_DURATION}s" ./cpu_stress auto "$STRESS_DURATION" urandom

    # Verify CSV was produced
    LATEST_CSV=$(ls -t results/*.csv 2>/dev/null | head -1)
    if [[ -n "$LATEST_CSV" ]]; then
        run_test "CSV output produced" test -s "$LATEST_CSV"
        run_test "CSV has header + data rows" bash -c "test \$(wc -l < \"$LATEST_CSV\") -gt 2"
    else
        printf "  %-40s FAIL\n" "CSV output produced"
        ((FAIL++))
    fi
fi

# ── 6. Plot generation ────────────────────────────────────────────────────────
section "Plot generation"

if command -v gnuplot &>/dev/null; then
    run_test "make plot (generate PNG)" make plot
    run_test "PNG file exists" bash -c 'ls results/*.png >/dev/null 2>&1'
else
    printf "  %-40s SKIP (gnuplot not installed)\n" "make plot"
    ((SKIP++))
fi

# ── 7. Security audit ────────────────────────────────────────────────────────
section "Security audit"

if ./check_audit_deps.sh --quiet >/dev/null 2>&1; then
    run_test "security_audit.sh" ./security_audit.sh
else
    printf "  %-40s SKIP (audit deps missing)\n" "security_audit.sh"
    ((SKIP++))
fi

# ── Cleanup: remove CSVs created during this test run ─────────────────────────
NEW_CSVS=$(comm -13 <(echo "$EXISTING_CSVS") <(ls results/*.csv 2>/dev/null | sort))
if [[ -n "$NEW_CSVS" ]]; then
    echo "$NEW_CSVS" | xargs rm -f
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════"
echo "  PASS: $PASS   FAIL: $FAIL   SKIP: $SKIP"
echo "═══════════════════════════════════════════"

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
