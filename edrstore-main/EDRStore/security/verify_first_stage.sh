#!/usr/bin/env bash
set -u

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <server-log>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
metrics="$project_root/security/stage6-baseline-metrics.txt"
server_log=$1

get_metric() {
    awk -F= -v name="$1" '$1 == name {print substr($0, index($0, "=") + 1); exit}' "$metrics"
}

failed=0
if (cd "$project_root" &&
    sha256sum -c security/stage6-current-binaries.sha256); then
    echo "Stage-6 binary set: PASS"
else
    echo "Stage-6 binary set: ROLLBACK REQUIRED"
    failed=1
fi

if [ ! -s "$server_log" ]; then
    echo "server-log is missing or empty: $server_log" >&2
    exit 1
fi

row=$(tail -n 1 "$server_log")
IFS=',' read -r logical_size logical_chunks unique_size unique_chunks \
    similar_size_l similar_chunks_l delta_size_l similar_size_g \
    similar_chunks_g delta_size_g storage_size cache_size remainder <<< "$row"

trim() { echo "$1" | tr -d '[:space:]'; }
logical_size=$(trim "$logical_size")
logical_chunks=$(trim "$logical_chunks")
unique_size=$(trim "$unique_size")
unique_chunks=$(trim "$unique_chunks")
similar_size_l=$(trim "$similar_size_l")
similar_chunks_l=$(trim "$similar_chunks_l")
delta_size_l=$(trim "$delta_size_l")
similar_size_g=$(trim "$similar_size_g")
similar_chunks_g=$(trim "$similar_chunks_g")
delta_size_g=$(trim "$delta_size_g")
storage_size=$(trim "$storage_size")
cache_size=$(trim "$cache_size")

compare_metric() {
    local name=$1
    local actual=$2
    local expected
    expected=$(get_metric "$name")
    if [ "$actual" = "$expected" ]; then
        echo "$name: PASS ($actual)"
    else
        echo "$name: ROLLBACK REQUIRED (baseline=$expected new=$actual)"
        failed=1
    fi
}

compare_metric logical_size "$logical_size"
compare_metric logical_chunk_num "$logical_chunks"
compare_metric unique_size "$unique_size"
compare_metric unique_chunk_num "$unique_chunks"
compare_metric similar_size_l "$similar_size_l"
compare_metric similar_chunk_num_l "$similar_chunks_l"
compare_metric delta_size_l "$delta_size_l"
compare_metric similar_size_g "$similar_size_g"
compare_metric similar_chunk_num_g "$similar_chunks_g"
compare_metric delta_size_g "$delta_size_g"
compare_metric storage_size "$storage_size"
compare_metric cache_size "$cache_size"

if [ "$failed" -ne 0 ]; then
    echo "FIRST_STAGE_RESULT=ROLLBACK"
    exit 1
fi
echo "FIRST_STAGE_RESULT=PASS"
