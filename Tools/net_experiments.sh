#!/usr/bin/env bash

set -u
BIN=${1:-build/Release}
OUT=${2:-Docs/Results/net_results.csv}
ITER=${3:-5000}
REPS=${4:-5}
PORT=${PORT:-5555}
WARMUP=200

mkdir -p "$(dirname "$OUT")"
echo "experiment,format,clients,server_objects,owned_per_client,rep,client_index,iterations,elapsed_ms,mean_us,replicas,bytes_sent,bytes_received,bytes_per_snapshot,mean_rtt_us" > "$OUT"

run_case() {
    local exp=$1 fmt=$2 clients=$3 objects=$4 owned=$5 rep=$6
    "$BIN/engine_server" --port "$PORT" --objects "$objects" > /dev/null 2>&1 &
    local server=$!
    sleep 0.4
    local pids=() tmp
    tmp=$(mktemp -d)
    for c in $(seq 1 "$clients"); do
        "$BIN/net_bench_client" --port "$PORT" --format "$fmt" --iterations "$ITER" --warmup "$WARMUP" \
            --owned "$owned" --hold-ms 1500 2>/dev/null | grep -v '^Connected' > "$tmp/$c.csv" &
        pids+=($!)
    done
    for p in "${pids[@]}"; do wait "$p"; done
    for c in $(seq 1 "$clients"); do
        if [ -s "$tmp/$c.csv" ]; then
            echo "$exp,$fmt,$clients,$objects,$owned,$rep,$c,$(cut -d, -f2- "$tmp/$c.csv")" >> "$OUT"
        else
            echo "WARN: no result for $exp $fmt clients=$clients objects=$objects owned=$owned rep=$rep client=$c" >&2
        fi
    done
    rm -rf "$tmp"
    kill "$server" 2>/dev/null
    wait "$server" 2>/dev/null
}

total=0
for rep in $(seq 1 "$REPS"); do
    if [ $((rep % 2)) -eq 1 ]; then formats="binary text"; else formats="text binary"; fi
    for fmt in $formats; do
        for n in 10 100 500; do run_case E1 "$fmt" 1 "$n" 1 "$rep"; total=$((total+1)); done
        for c in 1 2 4;      do run_case E2 "$fmt" "$c" 100 1 "$rep"; total=$((total+1)); done
        for o in 1 100 500;  do run_case E3 "$fmt" 2 10 "$o" "$rep"; total=$((total+1)); done
    done
    echo "rep $rep/$REPS done ($total runs)" >&2
done
echo "Results: $OUT" >&2
