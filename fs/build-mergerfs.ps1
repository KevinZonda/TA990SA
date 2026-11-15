param(
    [string]$CompilerVersion = '15.2.0',
    [string]$AlpineVersion = '3.23',
    [string]$BuilderImage = '',
    [ValidateRange(1, 64)]
    [int]$Jobs = 8,
    [switch]$BuildImage
)

$ErrorActionPreference = 'Stop'
if ($CompilerVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid GCC version.' }
if ($AlpineVersion -notmatch '^\d+\.\d+$') { throw 'Invalid Alpine version.' }
if (-not $BuilderImage) {
    $BuilderImage = "ta990sa-mergerfs-builder:2.42.0-gcc$CompilerVersion-arm64"
}
$sourcePath = Join-Path $PSScriptRoot 'mergerfs'
$outputPath = Join-Path $PSScriptRoot "artifacts\mergerfs-2.42.0-aarch64-gcc$CompilerVersion"
$binaryOutputPath = Join-Path $PSScriptRoot 'build'
$expectedCommit = 'ce3737ff3314ee0eb5edc38720b1a3f1fcb0e153'
$actualCommit = git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualCommit.Trim() -ne $expectedCommit) {
    throw "Expected mergerfs 2.42.0 at commit $expectedCommit."
}
git -C $sourcePath diff --quiet HEAD --
if ($LASTEXITCODE -ne 0) { throw 'Upstream tracked source files have local changes.' }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

if ($BuildImage) {
    docker build --platform linux/arm64 `
        --build-arg "ALPINE_VERSION=$AlpineVersion" --build-arg "GCC_VERSION=$CompilerVersion" `
        -f (Join-Path $PSScriptRoot 'Dockerfile.mergerfs') -t $BuilderImage $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'Failed to build ARM64 builder image.' }
}

docker run --rm --platform linux/arm64 `
    --mount "type=bind,source=$sourcePath,target=/source,readonly" `
    --mount "type=bind,source=$PSScriptRoot,target=/scripts,readonly" `
    --mount "type=bind,source=$outputPath,target=/output" `
    -e "JOBS=$Jobs" -e "EXPECTED_GCC_VERSION=$CompilerVersion" `
    --entrypoint /bin/sh $BuilderImage /scripts/build-mergerfs.sh
if ($LASTEXITCODE -ne 0) { throw 'mergerfs build or verification failed. See artifacts build.log.' }

$imageId = docker image inspect --format '{{.Id}}' $BuilderImage
if ($LASTEXITCODE -ne 0) { throw 'Failed to inspect builder image.' }
$metadata = [ordered]@{
    project = 'mergerfs'
    version = '2.42.0'
    source_url = 'https://github.com/trapexit/mergerfs.git'
    source_commit = $expectedCommit
    target = 'aarch64-linux-musl'
    linkage = 'static'
    compiler_version = $CompilerVersion
    alpine_version = $AlpineVersion
    builder_image = $BuilderImage
    builder_image_id = $imageId.Trim()
    make_arguments = @("-j$Jobs", 'NDEBUG=1', 'STATIC=1')
    status = 'complete'
    completed_at = [DateTimeOffset]::UtcNow.ToString('o')
    validation = @('ARM64 ELF inspection', 'Static linkage inspection', 'Version startup check in ARM64 Linux emulation')
    deployment = 'Not deployed to Orin; no mount tests performed.'
}
[IO.File]::WriteAllText((Join-Path $outputPath 'build-info.json'), ($metadata | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))

# Publish verified binaries at the stable paths used for deployment.
# Versioned artifacts retain the build logs, checksums, and historical binaries.
New-Item -ItemType Directory -Path $binaryOutputPath -Force | Out-Null
foreach ($binaryName in @('mergerfs', 'mergerfs-fusermount', 'mount.mergerfs')) {
    $builtBinary = Join-Path $outputPath $binaryName
    $publishedBinary = Join-Path $binaryOutputPath $binaryName
    Copy-Item -LiteralPath $builtBinary -Destination $publishedBinary -Force
    if ((Get-FileHash -LiteralPath $builtBinary -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $publishedBinary -Algorithm SHA256).Hash) {
        throw "Published binary checksum mismatch: $binaryName"
    }
}
Write-Output "Verified binaries published to $binaryOutputPath"
