#!/bin/sh
set -eu
PROBE=$1
MOCK=$2
NO_ALLOC=$3
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
export MOCK_TRACE_FILE="$TMP/trace"
COUNT=0

run_case() {
    label=$1 expected=$2 mode=$3
    shift 3
    : > "$MOCK_TRACE_FILE"
    result=0
    MOCK_CUDA_MODE="$mode" "$PROBE" "$@" > "$TMP/stdout" 2> "$TMP/stderr" || result=$?
    if [ "$result" -ne "$expected" ]; then
        printf 'FAIL %s: expected exit %s, got %s\n' "$label" "$expected" "$result"
        cat "$TMP/stdout" "$TMP/stderr"
        exit 1
    fi
    if [ "$expected" -ne 0 ] && grep -q 'Result: PASS' "$TMP/stdout"; then
        printf 'FAIL %s: failure incorrectly reported PASS\n' "$label"
        exit 1
    fi
    COUNT=$((COUNT + 1))
    printf 'PASS %s\n' "$label"
}

run_case help 0 '' --help
run_case missing_library 3 '' --library /not-present/libcuda.so.1
run_case round_trip 0 '' --library "$MOCK"
grep -q 'Driver API version: 12.1 (raw 12010)' "$TMP/stdout"
grep -q 'Memory round-trip: PASS (1048576 bytes verified)' "$TMP/stdout"
grep -q '^memory_free$' "$MOCK_TRACE_FILE"
grep -q '^context_destroy$' "$MOCK_TRACE_FILE"

run_case query_only 0 '' --library "$NO_ALLOC" --query-only
grep -q 'memory test skipped' "$TMP/stdout"
test ! -s "$MOCK_TRACE_FILE"
run_case missing_memory_symbol 3 '' --library "$NO_ALLOC"
grep -q 'Missing CUDA symbol cuMemAlloc_v2' "$TMP/stderr"
test ! -s "$MOCK_TRACE_FILE"
run_case init_failure 4 init_fail --library "$MOCK"
run_case no_devices 4 no_devices --library "$MOCK"
run_case invalid_device 4 '' --library "$MOCK" --device 1
test ! -s "$MOCK_TRACE_FILE"
run_case context_failure 5 context_fail --library "$MOCK"
test "$(wc -l < "$MOCK_TRACE_FILE")" -eq 1

run_case allocation_failure 5 alloc_fail --library "$MOCK"
grep -q 'CUDA_ERROR_OUT_OF_MEMORY (2)' "$TMP/stderr"
grep -q '^context_destroy$' "$MOCK_TRACE_FILE"
if grep -q '^memory_free$' "$MOCK_TRACE_FILE"; then exit 1; fi

run_case data_corruption 5 corrupt_copy --library "$MOCK" --bytes 4097
grep -q 'mismatch at byte 4096' "$TMP/stderr"
grep -q '^memory_free$' "$MOCK_TRACE_FILE"
grep -q '^context_destroy$' "$MOCK_TRACE_FILE"
run_case synchronization_failure 5 sync_fail --library "$MOCK"
grep -q '^memory_free$' "$MOCK_TRACE_FILE"
grep -q '^context_destroy$' "$MOCK_TRACE_FILE"
run_case free_failure 6 free_fail --library "$MOCK"
grep -q '^context_destroy$' "$MOCK_TRACE_FILE"
run_case destroy_failure 6 destroy_fail --library "$MOCK"
run_case one_byte 0 '' --library "$MOCK" --bytes 1
run_case maximum_buffer 0 '' --library "$MOCK" --bytes 16777216
run_case zero_bytes 2 '' --bytes 0
run_case too_many_bytes 2 '' --bytes 16777217
run_case negative_size 2 '' --bytes -1
run_case negative_device 2 '' --device -1
run_case missing_argument 2 '' --bytes
run_case unknown_argument 2 '' --unknown
printf 'All %s mock-driver tests passed. No real GPU was used.\n' "$COUNT"
