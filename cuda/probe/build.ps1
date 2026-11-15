param(
    [string]$BuilderImage = 'ta990sa-cuda-probe-builder:ubuntu20.04-arm64',
    [ValidateRange(1, 32)]
    [int]$Jobs = 4,
    [switch]$BuildImage
)

$ErrorActionPreference = 'Stop'
$outputPath = Join-Path $PSScriptRoot 'build'
$artifactsPath = Join-Path $PSScriptRoot 'artifacts'
New-Item -ItemType Directory -Path $outputPath, $artifactsPath -Force | Out-Null

if ($BuildImage) {
    docker build --platform linux/arm64 -f (Join-Path $PSScriptRoot 'Dockerfile') -t $BuilderImage $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'CUDA probe builder image failed.' }
}

docker run --rm --platform linux/arm64 `
    --mount "type=bind,source=$PSScriptRoot,target=/source,readonly" `
    --mount "type=bind,source=$PSScriptRoot,target=/scripts,readonly" `
    --mount "type=bind,source=$outputPath,target=/output" `
    --mount "type=bind,source=$artifactsPath,target=/artifacts" `
    -e "JOBS=$Jobs" --entrypoint /bin/sh $BuilderImage /scripts/build.sh
if ($LASTEXITCODE -ne 0) { throw 'CUDA probe build or mock-driver tests failed.' }

$imageId = docker image inspect --format '{{.Id}}' $BuilderImage
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect builder image.' }
$binaryPath = Join-Path $outputPath 'cuda-probe'
$metadata = [ordered]@{
    target = 'aarch64-linux-gnu'
    glibc_build_environment = '2.31'
    builder_image = $BuilderImage
    builder_image_id = $imageId.Trim()
    sha256 = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
    bytes = (Get-Item -LiteralPath $binaryPath).Length
    validation = 'ARM64 build, mock Driver/NVRTC/cuBLAS tests, help startup, ELF and GLIBC symbol inspection'
    real_gpu_tested = $false
    board_deployed = $false
    completed_at = [DateTimeOffset]::UtcNow.ToString('o')
}
[IO.File]::WriteAllText((Join-Path $artifactsPath 'build-info.json'), ($metadata | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
