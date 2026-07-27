#!/usr/bin/env bash

set -euo pipefail

plot_temp=${1:?Usage: test_plot_temp.sh <plot_temp_binary>}
fixture_dir="tests/fixtures/plot_temp"
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

fake_bin="$workdir/bin"
mkdir -p "$fake_bin"
cat > "$fake_bin/gnuplot" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

script=${1:?}
output=$(awk -F"'" '/^set output / { print $2; exit }' "$script")
data=$(awk -F"'" '/^plot / { print $2; exit }' "$script")
cp "$data" "$PLOT_TEST_CAPTURE"
touch "$output"
EOF
chmod +x "$fake_bin/gnuplot"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

run_plot() {
    local capture=$1
    shift
    PLOT_TEST_CAPTURE="$capture" PATH="$fake_bin:$PATH" "$plot_temp" "$@"
}

average_capture="$workdir/average.dat"
if ! run_plot "$average_capture" \
    "$fixture_dir/test_cpu_math_2cores_3sec_20260101_000000.csv" \
    "$fixture_dir/test_cpu_math_2cores_3sec_20260101_000001.csv" \
    "$workdir/average.png" > "$workdir/average.out" 2> "$workdir/average.err"; then
    cat "$workdir/average.err" >&2
    fail "same-interval fixture could not be plotted"
fi
printf '0 15.00\n1 25.00\n2 35.00\n' > "$workdir/expected-average.dat"
cmp "$workdir/expected-average.dat" "$average_capture" ||
    fail "same-interval files were not averaged point-by-point"
grep -q "Info: averaged 2 file(s)" "$workdir/average.out" ||
    fail "same-interval averaging was not reported"

na_capture="$workdir/na.dat"
run_plot "$na_capture" \
    "$fixture_dir/test_cpu_urandom_2cores_3sec_20260101_000000.csv" \
    "$workdir/na.png" > "$workdir/na.out" 2> "$workdir/na.err"
printf '0 30.00\n2 40.00\n' > "$workdir/expected-na.dat"
cmp "$workdir/expected-na.dat" "$na_capture" ||
    fail "N/A samples were not excluded from the plotted data"
grep -q "invalid temperature row" "$workdir/na.err" ||
    fail "N/A sample was not reported"

mixed_capture="$workdir/mixed.dat"
run_plot "$mixed_capture" \
    "$fixture_dir/test_cpu_urandom_2cores_3sec_20260101_000003.csv" \
    "$fixture_dir/test_cpu_urandom_2cores_4sec_20260101_000001.csv" \
    "$workdir/mixed.png" > "$workdir/mixed.out" 2> "$workdir/mixed.err"
grep -q "different poll intervals" "$workdir/mixed.err" ||
    fail "mixed intervals were not reported"
[[ $(grep -c "^Plotted series" "$workdir/mixed.out") -eq 2 ]] ||
    fail "mixed intervals were not emitted as separate series"

if run_plot "$workdir/bad-header.dat" \
    "$fixture_dir/test_cpu_math_2cores_3sec_20260101_000002.csv" \
    "$workdir/bad-header.png" > "$workdir/bad-header.out" 2> "$workdir/bad-header.err"; then
    fail "malformed CSV header was accepted"
fi
grep -q "unexpected header" "$workdir/bad-header.err" ||
    fail "malformed CSV header was not reported"

if run_plot "$workdir/bad-name.dat" \
    "$fixture_dir/not_a_stress_log.csv" \
    "$workdir/bad-name.png" > "$workdir/bad-name.out" 2> "$workdir/bad-name.err"; then
    fail "malformed filename was accepted"
fi
grep -q "expected filename pattern" "$workdir/bad-name.err" ||
    fail "malformed filename was not reported"

echo "plot_temp fixture tests passed"
