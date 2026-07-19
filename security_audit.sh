#!/usr/bin/env bash
# security_audit.sh — static and dynamic security audit for cpu_stress_test
# Generates a plain-text report to stdout and (optionally) a file.
#
# Usage: ./security_audit.sh [--report FILE] [--skip-valgrind] [--rebuild]
#
# Tools used (all standard Linux CLI tools):
#   cppcheck    — static C analysis (undefined behaviour, leaks, style issues)
#   flawfinder  — pattern-based CWE scan for dangerous C functions
#   valgrind    — dynamic memory error and leak detection (requires libc6-dbg on aarch64)
#   readelf     — binary hardening: PIE, RELRO, stack canary presence
#   objdump     — stack canary call count per binary
#   gcc -Wall   — compiler warnings (rebuilds with warning flags)
#
# Exit code: 0 = all checks passed, 1 = one or more checks failed/warned

set -euo pipefail

# ── configuration ────────────────────────────────────────────────────────────
REPORT_FILE=""
SKIP_VALGRIND=0
REBUILD=0
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

BINARIES=(cpu_stress cpu_id cpu_temp cpu_cores plot_temp list_temps timestamp)
SOURCES=(cpu_stress.c cpu_id.c cpu_id_tool.c cpu_temp.c cpu_temp_tool.c
         cpu_cores.c plot_temp.c list_temps_tool.c timestamp.c timestamp_tool.c)

# ── argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --report)    REPORT_FILE="$2"; shift 2 ;;
        --skip-valgrind) SKIP_VALGRIND=1; shift ;;
        --rebuild)   REBUILD=1; shift ;;
        -h|--help)
            sed -n '/^# Usage/,/^#$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

cd "$SCRIPT_DIR"

# ── output helpers ────────────────────────────────────────────────────────────
REPORT_LINES=()
PASS=0; FAIL=0; WARN=0

ts() { date '+%Y-%m-%d %H:%M:%S'; }

emit() {
    echo "$*"
    REPORT_LINES+=("$*")
}

section() {
    emit ""
    emit "══════════════════════════════════════════════════════════════════════"
    emit "  $*"
    emit "══════════════════════════════════════════════════════════════════════"
}

result() {  # result PASS|FAIL|WARN "message"
    local status="$1"; shift
    case "$status" in
        PASS) emit "  [PASS] $*"; ((PASS++)) || true ;;
        FAIL) emit "  [FAIL] $*"; ((FAIL++)) || true ;;
        WARN) emit "  [WARN] $*"; ((WARN++)) || true ;;
    esac
}

detail() { emit "         $*"; }

# ── header ────────────────────────────────────────────────────────────────────
emit "cpu_stress_test — Security Audit Report"
emit "Generated   : $(ts)"
emit "Host        : $(uname -srm)"
emit "Directory   : $SCRIPT_DIR"
emit "Auditor     : $(basename "$0")"

# ── tool availability ─────────────────────────────────────────────────────────
section "Tool Availability"
TOOLS_OK=1
for tool in cppcheck flawfinder readelf objdump gcc; do
    if command -v "$tool" &>/dev/null; then
        result PASS "$tool found: $(command -v "$tool")"
    else
        result FAIL "$tool not found — install to enable this check"
        TOOLS_OK=0
    fi
done
if command -v valgrind &>/dev/null; then
    result PASS "valgrind found: $(command -v valgrind) ($(valgrind --version 2>/dev/null))"
else
    result WARN "valgrind not found — dynamic memory checks skipped"
    SKIP_VALGRIND=1
fi

# ── build check ───────────────────────────────────────────────────────────────
section "Build / Compiler Warnings"
if [[ $REBUILD -eq 1 ]]; then
    emit "  Rebuilding with -Wall -Wextra -Wformat-security ..."
    BUILD_LOG=$(make clean 2>&1 && \
        CFLAGS="-O2 -Wall -Wextra -Wformat-security" make all 2>&1) || true
else
    BUILD_LOG=$(CFLAGS="-O2 -Wall -Wextra -Wformat-security" make all 2>&1) || true
fi
WARNING_COUNT=$(echo "$BUILD_LOG" | grep -c ": warning:" || true)
ERROR_COUNT=$(echo "$BUILD_LOG" | grep -c ": error:" || true)

if [[ $ERROR_COUNT -gt 0 ]]; then
    result FAIL "Build produced $ERROR_COUNT compiler error(s)"
    echo "$BUILD_LOG" | grep ": error:" | while read -r l; do detail "$l"; done
elif [[ $WARNING_COUNT -gt 0 ]]; then
    result WARN "Build produced $WARNING_COUNT compiler warning(s) with -Wall -Wextra -Wformat-security"
    echo "$BUILD_LOG" | grep ": warning:" | while read -r l; do detail "$l"; done
else
    result PASS "Clean build — no warnings with -Wall -Wextra -Wformat-security"
fi

# ── binary hardening ──────────────────────────────────────────────────────────
section "Binary Hardening (readelf / objdump)"
emit "  Checking: PIE, RELRO, stack-canary calls"
emit ""

for bin in "${BINARIES[@]}"; do
    if [[ ! -f "$bin" ]]; then
        result WARN "$bin: binary not found, skipping"
        continue
    fi
    emit "  ── $bin ──"

    # PIE
    elf_type=$(readelf -h "$bin" 2>/dev/null | awk '/Type:/{print $2}')
    if [[ "$elf_type" == "DYN" ]]; then
        result PASS "$bin: PIE enabled (ELF type DYN)"
    else
        result FAIL "$bin: PIE not enabled (ELF type $elf_type) — rebuild with -fPIE -pie"
    fi

    # RELRO (partial or full)
    relro=$(readelf -l "$bin" 2>/dev/null | grep -c "GNU_RELRO" || true)
    bind_now=$(readelf -d "$bin" 2>/dev/null | grep -c "BIND_NOW" || true)
    if [[ $relro -gt 0 && $bind_now -gt 0 ]]; then
        result PASS "$bin: Full RELRO (GNU_RELRO + BIND_NOW)"
    elif [[ $relro -gt 0 ]]; then
        result WARN "$bin: Partial RELRO only — add -Wl,-z,now for full RELRO"
    else
        result FAIL "$bin: No RELRO — rebuild with -Wl,-z,relro"
    fi

    # Stack canary (heuristic: count __stack_chk calls)
    canary=$(objdump -d "$bin" 2>/dev/null | grep -c "stack_chk" || true)
    if [[ $canary -gt 0 ]]; then
        result PASS "$bin: Stack canaries present ($canary call sites)"
    else
        result WARN "$bin: No stack canary calls detected — consider -fstack-protector-strong"
    fi
done

# ── cppcheck ──────────────────────────────────────────────────────────────────
section "Static Analysis — cppcheck"
CPPCHECK_OUT=$(cppcheck --enable=warning,performance,portability \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --error-exitcode=0 \
    "${SOURCES[@]}" 2>&1) || true

CPPCHECK_ERRORS=$(echo "$CPPCHECK_OUT" | grep -c ": error:" || true)
CPPCHECK_WARNINGS=$(echo "$CPPCHECK_OUT" | grep -c ": warning:" || true)

emit "  cppcheck $(cppcheck --version 2>/dev/null)"
if [[ $CPPCHECK_ERRORS -gt 0 ]]; then
    result FAIL "cppcheck: $CPPCHECK_ERRORS error(s) found"
    echo "$CPPCHECK_OUT" | grep ": error:" | while read -r l; do detail "$l"; done
elif [[ $CPPCHECK_WARNINGS -gt 0 ]]; then
    result WARN "cppcheck: $CPPCHECK_WARNINGS warning(s) found (no errors)"
    echo "$CPPCHECK_OUT" | grep ": warning:" | while read -r l; do detail "$l"; done
else
    result PASS "cppcheck: no errors or warnings"
fi

# ── flawfinder ────────────────────────────────────────────────────────────────
section "Static Analysis — flawfinder (CWE / dangerous function scan)"
FLAWFINDER_OUT=$(flawfinder --quiet --minlevel 3 "${SOURCES[@]}" 2>&1) || true
HIGH_HITS=$(echo "$FLAWFINDER_OUT" | grep -cE "^\s+[A-Za-z_/].*\[[4-9]\]" || true)
MED_HITS=$(echo "$FLAWFINDER_OUT" | grep -cE "^\s+[A-Za-z_/].*\[[3]\]" || true)

emit "  flawfinder $(flawfinder --version 2>/dev/null), showing level ≥ 3"
if [[ $HIGH_HITS -gt 0 ]]; then
    result FAIL "flawfinder: $HIGH_HITS high-severity hit(s) (level 4+)"
    echo "$FLAWFINDER_OUT" | grep -E "\[[4-9]\]" | while read -r l; do detail "$l"; done
elif [[ $MED_HITS -gt 0 ]]; then
    result WARN "flawfinder: $MED_HITS medium-severity hit(s) (level 3) — review below"
    echo "$FLAWFINDER_OUT" | grep -E "\[3\]" | while read -r l; do detail "$l"; done
else
    result PASS "flawfinder: no hits at level 3+"
fi

# ── valgrind ──────────────────────────────────────────────────────────────────
section "Dynamic Analysis — valgrind memcheck"
if [[ $SKIP_VALGRIND -eq 1 ]]; then
    emit "  Skipped (--skip-valgrind or valgrind not found)"
    result WARN "valgrind checks skipped"
else
    # Build static debug binaries for valgrind (avoids stripped-ld aarch64 issue)
    emit "  Building static debug binaries for valgrind..."
    VDIR=$(mktemp -d)
    trap 'rm -rf "$VDIR"' EXIT

    V_BINS=(
        "cpu_id_tool.c cpu_id.c"
        "cpu_temp_tool.c cpu_temp.c cpu_id.c"
        "cpu_cores.c"
        "timestamp_tool.c timestamp.c"
    )
    V_NAMES=(cpu_id cpu_temp cpu_cores timestamp)

    # Generate a glibc suppression file for static-link false positives
    SUPPFILE="$VDIR/glibc_static.supp"
    cat > "$SUPPFILE" <<'SUPP'
{
   glibc_static_init
   Memcheck:Cond
   ...
   obj:*/ld-linux*
}
{
   glibc_static_malloc
   Memcheck:Cond
   fun:malloc
   ...
}
{
   glibc_getrandom
   Memcheck:Cond
   fun:getrandom
   ...
}
{
   glibc_dl_init
   Memcheck:Param
   set_robust_list(head)
   ...
}
SUPP

    VALGRIND_ANY_FAIL=0
    for i in "${!V_NAMES[@]}"; do
        name="${V_NAMES[$i]}"
        srcs="${V_BINS[$i]}"
        bin="$VDIR/$name"
        # shellcheck disable=SC2086
        if ! gcc -g -O0 -static $srcs -lpthread -o "$bin" 2>/dev/null; then
            result WARN "valgrind/$name: static build failed — skipping"
            continue
        fi
        VG_OUT=$(valgrind \
            --tool=memcheck \
            --leak-check=full \
            --errors-for-leak-kinds=definite \
            --suppressions="$SUPPFILE" \
            --error-exitcode=0 \
            "$bin" 2>&1) || true

        OUR_ERRORS=$(echo "$VG_OUT" | grep -E "by 0x[0-9A-Fa-f]+: (main|get_cpu|read_cpu|print_all)" | wc -l || true)
        DEFINITE_LEAKS=$(echo "$VG_OUT" | grep -c "definitely lost:" || true)
        LEAK_BYTES=$(echo "$VG_OUT" | grep "definitely lost:" | grep -v "0 bytes" | wc -l || true)

        if [[ $LEAK_BYTES -gt 0 ]]; then
            result FAIL "valgrind/$name: definite memory leak(s) detected"
            echo "$VG_OUT" | grep "definitely lost:" | while read -r l; do detail "$l"; done
            VALGRIND_ANY_FAIL=1
        elif [[ $OUR_ERRORS -gt 0 ]]; then
            result WARN "valgrind/$name: $OUR_ERRORS error(s) in application code (review output)"
            VALGRIND_ANY_FAIL=1
        else
            result PASS "valgrind/$name: no application-code memory errors or definite leaks"
        fi
    done
fi

# ── summary ───────────────────────────────────────────────────────────────────
section "Summary"
TOTAL=$((PASS + FAIL + WARN))
emit "  Checks run : $TOTAL"
emit "  Passed     : $PASS"
emit "  Warnings   : $WARN"
emit "  Failed     : $FAIL"
emit ""
if [[ $FAIL -gt 0 ]]; then
    emit "  OVERALL: FAIL — $FAIL check(s) require attention"
elif [[ $WARN -gt 0 ]]; then
    emit "  OVERALL: PASS WITH WARNINGS — review $WARN item(s) above"
else
    emit "  OVERALL: PASS — all checks passed cleanly"
fi
emit ""
emit "Report end: $(ts)"

# ── write report file ─────────────────────────────────────────────────────────
if [[ -n "$REPORT_FILE" ]]; then
    printf '%s\n' "${REPORT_LINES[@]}" > "$REPORT_FILE"
    echo ""
    echo "Report written to: $REPORT_FILE"
fi

[[ $FAIL -eq 0 ]]
