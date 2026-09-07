[CmdletBinding()]
param(
    [string]$CacheRoot = "I:\oot3dre_work\oot3d-whole-aot-product-cache",
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [string]$VcpkgRoot = "C:\vcpkg",
    [ValidateRange(1, 8)]
    [int]$Parallel = 4,
    [switch]$ConfigureOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$runtimeRoot = Join-Path $repoRoot "tools\oot3d\native_a32_runtime"
$prepareScript = Join-Path $runtimeRoot "prepare_whole_aot_product.py"
$productManifest = Join-Path $runtimeRoot "whole_aot_product_manifest.json"
$productProject = Join-Path $runtimeRoot "product_build"
$toolchain = Join-Path $repoRoot "cmake\Oot3dLlvmClToolchain.cmake"
$clangCl = Join-Path $LlvmRoot "bin\clang-cl.exe"
$ninja =
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$vsDevShell =
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
$nlohmannInclude = Join-Path $VcpkgRoot "installed\x64-windows-static\include"

function ConvertTo-CMakePath {
    param([string]$Path)
    return ([System.IO.Path]::GetFullPath($Path)).Replace('\', '/')
}

foreach($required in @(
        $prepareScript,
        $productManifest,
        $toolchain,
        $clangCl,
        $ninja,
        $cmake,
        $vsDevShell,
        (Join-Path $nlohmannInclude "nlohmann\json_fwd.hpp"))) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Required whole-AOT product input is missing: $required"
    }
}

New-Item -ItemType Directory -Force $CacheRoot | Out-Null
$keyExpression = @"
import sys
from pathlib import Path
sys.path.insert(0, str(Path(r'$runtimeRoot')))
from prepare_whole_aot_product import product_cache_key
print(product_cache_key(Path(r'$productManifest')))
"@
$cacheKey = (& python -c $keyExpression).Trim()
if($LASTEXITCODE -ne 0 -or $cacheKey -notmatch '^[0-9a-f]{64}$') {
    throw "Unable to calculate the whole-AOT product cache key"
}

& python $prepareScript --cache-root $CacheRoot
if($LASTEXITCODE -ne 0) {
    throw "Whole-AOT product source preparation failed"
}

$artifactRoot = Join-Path $CacheRoot $cacheKey
$generatedRoot = Join-Path $artifactRoot "cpp"
$generatedManifest = Join-Path $generatedRoot "whole_aot_cpp_manifest.json"
$buildRoot = Join-Path $artifactRoot "build\llvm-22.1.6-o2-strict"
$archive = Join-Path $buildRoot "lib\oot3d_native_whole_aot.lib"

foreach($required in @($generatedManifest, $generatedRoot)) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Prepared whole-AOT artifact is missing: $required"
    }
}

& $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:OOT3D_LLVM_ROOT = [System.IO.Path]::GetFullPath($LlvmRoot)
$configureArguments = @(
    "-S", (ConvertTo-CMakePath $productProject),
    "-B", (ConvertTo-CMakePath $buildRoot),
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath $ninja)",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DCMAKE_TOOLCHAIN_FILE=$(ConvertTo-CMakePath $toolchain)",
    "-DOOT3D_REPO_ROOT=$(ConvertTo-CMakePath $repoRoot)",
    "-DOOT3D_WHOLE_AOT_GENERATED_DIR=$(ConvertTo-CMakePath $generatedRoot)",
    "-DOOT3D_NLOHMANN_INCLUDE_DIR=$(ConvertTo-CMakePath $nlohmannInclude)"
)

& $cmake @configureArguments
if($LASTEXITCODE -ne 0) {
    throw "Whole-AOT product archive configuration failed"
}
if($ConfigureOnly) {
    Write-Output $buildRoot
    exit 0
}

$started = Get-Date
& $cmake --build $buildRoot --target oot3d_native_whole_aot --parallel $Parallel
if($LASTEXITCODE -ne 0) {
    throw "Whole-AOT product archive build failed"
}
if(-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
    throw "Whole-AOT product archive was not produced: $archive"
}

$compilerVersion = (& $clangCl --version | Select-Object -First 1).Trim()
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$generatedHash =
    (Get-FileHash -LiteralPath $generatedManifest -Algorithm SHA256).Hash.ToLowerInvariant()
$metadata = [ordered]@{
    format = "oot3d_whole_aot_product_archive_v1"
    cache_key = $cacheKey
    generated_manifest_sha256 = $generatedHash
    compiler = $compilerVersion
    configuration = "RelWithDebInfo-O2-Ob2-fp-strict-MT"
    archive = [System.IO.Path]::GetFullPath($archive)
    archive_bytes = (Get-Item -LiteralPath $archive).Length
    archive_last_write_utc =
        (Get-Item -LiteralPath $archive).LastWriteTimeUtc.ToString("O")
    archive_last_write_ticks =
        (Get-Item -LiteralPath $archive).LastWriteTimeUtc.Ticks
    archive_sha256 = $archiveHash
    build_seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 3)
}
$metadataPath = Join-Path $artifactRoot "whole_aot_archive.json"
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataPath -Encoding utf8
$metadata | ConvertTo-Json -Depth 4
