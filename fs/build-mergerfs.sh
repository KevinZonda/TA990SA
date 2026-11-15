#!/bin/sh
set -eu

VERSION=2.42.0
JOBS=${JOBS:-4}
EXPECTED_GCC_VERSION=${EXPECTED_GCC_VERSION:-15.2.0}
BUILD_DIR=/tmp/ta990sa-mergerfs
OUT=/output

ACTUAL_GCC_VERSION=$(g++ -dumpfullversion -dumpversion)
if [ "$ACTUAL_GCC_VERSION" != "$EXPECTED_GCC_VERSION" ]; then
    printf 'Expected GCC %s, found %s\n' "$EXPECTED_GCC_VERSION" "$ACTUAL_GCC_VERSION" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR" "$OUT"
cp -a /source/. "$BUILD_DIR/"
cd "$BUILD_DIR"

# Keep generated metadata out of the upstream checkout. No Git is needed here.
printf '%s\n' "$VERSION" > VERSION
printf '#pragma once\nstatic const char MERGERFS_VERSION[] = "%s";\n' "$VERSION" > src/version.hpp

printf 'Building mergerfs %s for %s with %s jobs\n' "$VERSION" "$(uname -m)" "$JOBS"
g++ --version | head -n 1
if ! make -j"$JOBS" NDEBUG=1 STATIC=1 > "$OUT/build.log" 2>&1; then
    tail -n 80 "$OUT/build.log"
    exit 1
fi

strip build/mergerfs vendored/libfuse/build/mergerfs-fusermount vendored/libfuse/build/mount.mergerfs
cp build/mergerfs vendored/libfuse/build/mergerfs-fusermount vendored/libfuse/build/mount.mergerfs "$OUT/"

{
    printf 'Source: https://github.com/trapexit/mergerfs\n'
    printf 'Tag: %s\n' "$VERSION"
    printf 'Commit: ce3737ff3314ee0eb5edc38720b1a3f1fcb0e153\n'
    printf 'Build flags: NDEBUG=1 STATIC=1\n'
    g++ --version | head -n 1
    file "$OUT/mergerfs" "$OUT/mergerfs-fusermount" "$OUT/mount.mergerfs"
    "$OUT/mergerfs" --version
    readelf -h "$OUT/mergerfs"
    readelf -l "$OUT/mergerfs"
    readelf -d "$OUT/mergerfs"
    readelf -p .comment "$OUT/mergerfs"
} > "$OUT/verification.txt" 2>&1

cd "$OUT"
sha256sum mergerfs mergerfs-fusermount mount.mergerfs > SHA256SUMS
cp /source/LICENSE "$OUT/LICENSE"
cat verification.txt
cat SHA256SUMS
