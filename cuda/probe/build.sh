#!/bin/sh
set -eu
test "$(uname -m)" = aarch64
test "$(getconf GNU_LIBC_VERSION)" = 'glibc 2.31'
mkdir -p /tmp/ta990sa-cuda-probe /output /artifacts
cp /source/probe.c /source/compute_tests.c /source/compute_tests.h /source/cuda_driver_minimal.h /source/Makefile /tmp/ta990sa-cuda-probe/
cp -R /source/tests /tmp/ta990sa-cuda-probe/tests
cd /tmp/ta990sa-cuda-probe
if ! make -j"${JOBS:-4}" all check > /artifacts/build.log 2>&1; then
    cat /artifacts/build.log
    exit 1
fi
install -m 755 build/cuda-probe /output/cuda-probe
{
    uname -m
    getconf GNU_LIBC_VERSION
    gcc --version | head -n 1
    file /output/cuda-probe
    readelf -h /output/cuda-probe
    readelf -l /output/cuda-probe
    readelf -d /output/cuda-probe
    readelf --version-info /output/cuda-probe
    /output/cuda-probe --help
    tail -n 1 /artifacts/build.log
} > /artifacts/verification.txt 2>&1
cd /output
sha256sum cuda-probe > /artifacts/SHA256SUMS
cat /artifacts/build.log /artifacts/verification.txt /artifacts/SHA256SUMS
