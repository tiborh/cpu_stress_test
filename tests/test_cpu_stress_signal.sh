#!/usr/bin/env bash

set -euo pipefail

stress_binary=${1:?Usage: test_cpu_stress_signal.sh <cpu_stress_binary>}
invocation_dir=$(pwd)
case "$stress_binary" in
    /*) ;;
    *) stress_binary="$invocation_dir/$stress_binary" ;;
esac
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
fixture_root="$repo_root/tests/fixtures/sysfs_x86"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

run_interruption_test() {
    local signal_name=$1
    local expected_status=$2
    local run_dir="$workdir/$signal_name"
    local output="$run_dir/output.txt"
    mkdir -p "$run_dir"

    (
        cd "$run_dir"
        exec env CPU_TEMP_SYSFS_ROOT="$fixture_root" "$stress_binary" 1 30 math
    ) > "$output" 2>&1 &
    local pid=$!

    sleep 1
    kill -"$signal_name" "$pid"
    local status
    if wait "$pid"; then
        fail "$signal_name did not report an interrupted exit status"
    else
        status=$?
    fi

    [[ $status -eq $expected_status ]] ||
        fail "$signal_name returned $status instead of $expected_status"
    grep -q "Interrupted by signal" "$output" ||
        fail "$signal_name interruption was not reported"
    grep -q "Stress test complete." "$output" ||
        fail "$signal_name did not complete normal cleanup"

    local csv
    csv=$(find "$run_dir/results" -maxdepth 1 -type f -name '*.csv' -print -quit)
    [[ -n $csv && -s $csv ]] || fail "$signal_name did not leave a closed CSV log"
    grep -q '^Timestamp,ElapsedSeconds,TemperatureCelsius$' "$csv" ||
        fail "$signal_name CSV is missing its header"
}

run_interruption_test INT 130
run_interruption_test TERM 143

echo "cpu_stress signal cleanup tests passed"
