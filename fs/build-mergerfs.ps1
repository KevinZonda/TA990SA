param(
    [string]$BuilderImage = 'ta990sa-mergerfs-builder:2.42.0-arm64',
    [int]$Jobs = 4,
    [switch]$BuildImage
)

$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot 'mergerfs'
$outputPath = Join-Path $PSScriptRoot 'artifacts\mergerfs-2.42.0-aarch64'
$expectedCommit = 'ce3737ff3314ee0eb5edc38720b1a3f1fcb0e153'
$actualCommit = git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualCommit.Trim() -ne $expectedCommit) {
    throw "Expected mergerfs 2.42.0 at commit $expectedCommit."
}
git -C $sourcePath diff --quiet HEAD --
if ($LASTEXITCODE -ne 0) { throw 'Upstream tracked source files have local changes.' }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

if ($BuildImage) {
    docker build --platform linux/arm64 -f (Join-Path $PSScriptRoot 'Dockerfile.mergerfs') -t $BuilderImage $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'Failed to build ARM64 builder image.' }
}

docker run --rm --platform linux/arm64 `
    --mount "type=bind,source=$sourcePath,target=/source,readonly" `
    --mount "type=bind,source=$PSScriptRoot,target=/scripts,readonly" `
    --mount "type=bind,source=$outputPath,target=/output" `
    -e "JOBS=$Jobs" --entrypoint /bin/sh $BuilderImage /scripts/build-mergerfs.sh
if ($LASTEXITCODE -ne 0) { throw 'mergerfs build or verification failed. See artifacts build.log.' }
