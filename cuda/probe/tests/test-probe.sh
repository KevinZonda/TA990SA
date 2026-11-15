#!/bin/sh
set -eu
PROBE=$1
MOCK=$2
NO_ALLOC=$3
NVRTC=$4
CUBLAS=$5
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

compute_case() {
    label=$1 expected=$2 mode=$3
    shift 3
    run_case "$label" "$expected" "$mode" --library "$MOCK" \
        --nvrtc-library "$NVRTC" --cublas-library "$CUBLAS" "$@"
}
compute_case all_tests 0 '' --all-tests
grep -q 'PTX target: compute_86 (GPU sm_87)' "$TMP/stdout"
grep -q 'GPU vector addition: PASS (4099 FP32 results verified)' "$TMP/stdout"
grep -q '221 FP32 results verified; max abs error 0)' "$TMP/stdout"
test "$(grep -c '^memory_free$' "$MOCK_TRACE_FILE")" -eq 7
grep -q '^module_unload$' "$MOCK_TRACE_FILE"
grep -q '^nvrtc_destroy$' "$MOCK_TRACE_FILE"
grep -q '^blas_destroy$' "$MOCK_TRACE_FILE"
compute_case kernel_only 0 '' --kernel-test
if grep -q '^blas_create$' "$MOCK_TRACE_FILE"; then exit 1; fi
compute_case cublas_only 0 '' --cublas-test
if grep -q '^nvrtc_create$' "$MOCK_TRACE_FILE"; then exit 1; fi
compute_case query_conflict 2 '' --query-only --all-tests
test ! -s "$MOCK_TRACE_FILE"
compute_case missing_nvrtc 7 '' --kernel-test --nvrtc-library /not-present/libnvrtc.so
compute_case missing_nvrtc_symbol 7 '' --kernel-test --nvrtc-library "$MOCK"
compute_case missing_cublas 8 '' --cublas-test --cublas-library /not-present/libcublas.so
compute_case missing_cublas_symbol 8 '' --cublas-test --cublas-library "$MOCK"
compute_case no_compatible_arch 7 arch_none --kernel-test
compute_case excessive_arch_count 7 arch_count_bad --kernel-test
compute_case compilation_failure 7 compile_fail --kernel-test
grep -q 'simulated compilation failure' "$TMP/stderr"
grep -q '^nvrtc_destroy$' "$MOCK_TRACE_FILE"
test "$(grep -c '^memory_free$' "$MOCK_TRACE_FILE")" -eq 1
compute_case invalid_ptx_size 7 ptx_size_bad --kernel-test
compute_case module_load_failure 7 module_load_fail --kernel-test
if grep -q '^module_unload$' "$MOCK_TRACE_FILE"; then exit 1; fi
compute_case partial_kernel_allocation 7 kernel_alloc_fail --kernel-test
test "$(grep -c '^memory_free$' "$MOCK_TRACE_FILE")" -eq 2
grep -q '^module_unload$' "$MOCK_TRACE_FILE"
compute_case kernel_launch_failure 7 launch_fail --kernel-test
test "$(grep -c '^memory_free$' "$MOCK_TRACE_FILE")" -eq 4
grep -q '^context_destroy$' "$MOCK_TRACE_FILE"
compute_case kernel_sync_failure 7 kernel_sync_fail --kernel-test
grep -q '^module_unload$' "$MOCK_TRACE_FILE"
compute_case kernel_tail_corruption 7 kernel_corrupt --kernel-test
grep -q 'Kernel mismatch at element 4098' "$TMP/stderr"
compute_case kernel_nan 7 kernel_nan --kernel-test
compute_case kernel_did_not_execute 7 kernel_noop --kernel-test
compute_case module_unload_failure 6 module_unload_fail --kernel-test
grep -q '^nvrtc_destroy$' "$MOCK_TRACE_FILE"
compute_case nvrtc_cleanup_failure 6 nvrtc_destroy_fail --kernel-test
compute_case blas_create_failure 8 blas_create_fail --cublas-test
if grep -q '^blas_destroy$' "$MOCK_TRACE_FILE"; then exit 1; fi
compute_case blas_execution_failure 8 blas_sgemm_fail --cublas-test
grep -q '^blas_destroy$' "$MOCK_TRACE_FILE"
test "$(grep -c '^memory_free$' "$MOCK_TRACE_FILE")" -eq 4
compute_case blas_tail_corruption 8 blas_corrupt --cublas-test
grep -q 'cuBLAS mismatch at (16,12)' "$TMP/stderr"
compute_case blas_nan 8 blas_nan --cublas-test
compute_case blas_did_not_execute 8 blas_noop --cublas-test
compute_case blas_cleanup_failure 6 blas_destroy_fail --cublas-test
test "$(grep -c '^memory_free$' "$MOCK_TRACE_FILE")" -eq 4
printf 'All %s mock-driver tests passed. No real GPU was used.\n' "$COUNT"
