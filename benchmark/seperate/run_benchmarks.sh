#!/usr/bin/env bash
# Builds all benchmark modes (each in a plain and an _ips4o variant) and runs
# each, merging their CSV output into two files with a "mode" column so
# show_results.py can compare modes directly:
#   data/results.csv       - std::sort based modes
#   data/results_ips4o.csv - same modes, sorted with ips4o::parallel::sort
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BUILD_DIR=build
OUT=data/results.csv
OUT_IPS4O=data/results_ips4o.csv

cmake -S . -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" -j >/dev/null

mkdir -p data

run_set() {
    local out="$1"
    shift
    {
        echo "mode,num_columns,num_rows,sort_column_amount,trial,time_ms"
        for entry in "$@"; do
            bin="${entry%%:*}"
            mode="${entry##*:}"
            echo "running $mode..." >&2
            "$BUILD_DIR/$bin" | tail -n +2 | sed "s/^/${mode},/"
        done
    } > "$out"
    echo "wrote $out"
}

run_set "$OUT" \
    "benchmark_row:ROW" \
    "benchmark_col_normalsort:COL_NORMALSORT" \
    "benchmark_col_permsort:COL_PERMSORT"

run_set "$OUT_IPS4O" \
    "benchmark_row_ips4o:ROW" \
    "benchmark_col_normalsort_ips4o:COL_NORMALSORT" \
    "benchmark_col_permsort_ips4o:COL_PERMSORT"
