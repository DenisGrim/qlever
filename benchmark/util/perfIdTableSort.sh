#!/usr/bin/env bash
# Profile only the sorting inside IdTableSortBenchmark with perf.
# Usage: perfIdTableSort.sh <path/to/IdTableSortBenchmark> [perf args...] -- [benchmark args...]
# Example: perfIdTableSort.sh build/benchmark/IdTableSortBenchmark record -g -- -p
set -euo pipefail

BIN="$1"; shift
PERF_ARGS=()
while [[ $# -gt 0 && "$1" != "--" ]]; do PERF_ARGS+=("$1"); shift; done
[[ $# -gt 0 ]] && shift   # drop "--"
[[ ${#PERF_ARGS[@]} -eq 0 ]] && PERF_ARGS=(stat)

DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
mkfifo "$DIR/ctl" "$DIR/ack"

export PERF_CTL_FIFO="$DIR/ctl" PERF_ACK_FIFO="$DIR/ack"
# --delay=-1: start with counters disabled, the benchmark enables them.
perf "${PERF_ARGS[@]}" --delay=-1 --control "fifo:$DIR/ctl,$DIR/ack" -- "$BIN" "$@"
