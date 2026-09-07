[CmdletBinding()]
param(
    [string]$CacheRoot = "I:\oot3dre_work\oot3d-whole-aot-product-cache",
    [string]$BuildDirectory = "I:\oot3dre_work\whole-aot-product-consumer",
    [string]$LlvmRoot = "I:\oot3dre_tools\llvm-22.1.6",
    [string]$VcpkgRoot = "C:\vcpkg",
    [string]$CodeBin = "",
    [string]$ExHeader = "",
    [ValidateSet("Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [switch]$Force,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if($Force.IsPresent -and $ValidateOnly.IsPresent) {
    throw "Force and ValidateOnly are mutually exclusive"
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$runtimeRoot = Join-Path $repoRoot "tools\oot3d\native_a32_runtime"
$productManifest = Join-Path $runtimeRoot "whole_aot_product_manifest.json"
$selection = Join-Path $runtimeRoot "whole_aot_functions.json"
$operationalManifest = Join-Path $repoRoot "tools\oot3d\operational_inputs.json"
$toolchain = Join-Path $repoRoot "cmake\Oot3dLlvmClToolchain.cmake"
$clangCl = Join-Path $LlvmRoot "bin\clang-cl.exe"
$vcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$ninja =
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$vsDevShell =
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"

function ConvertTo-CMakePath {
    param([string]$Path)
    return ([System.IO.Path]::GetFullPath($Path)).Replace('\', '/')
}

function Read-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$Cache,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $match = Select-String -LiteralPath $Cache `
        -Pattern ("^{0}:[^=]+=(.*)$" -f [Regex]::Escape($Name)) |
        Select-Object -First 1
    if($null -eq $match) {
        throw "CMake cache setting is missing: $Name"
    }
    return $match.Matches[0].Groups[1].Value
}

if(-not (Test-Path -LiteralPath $operationalManifest -PathType Leaf)) {
    throw "Operational input manifest is missing: $operationalManifest"
}
$operationalInputs = Get-Content -LiteralPath $operationalManifest -Raw |
    ConvertFrom-Json
$operationalVariables = @{}
foreach($property in $operationalInputs.variables.PSObject.Properties) {
    $operationalVariables[$property.Name] = [string]$property.Value
}
$operationalVariables["repoRoot"] = $repoRoot

function Resolve-Oot3dOperationalInput {
    param([Parameter(Mandatory = $true)][string]$Id)

    $entry = @($operationalInputs.entries | Where-Object {
            [string]$_.id -eq $Id
        } | Select-Object -First 1)
    if($entry.Count -ne 1) {
        throw "Operational input is missing: $Id"
    }
    $expanded = [string]$entry[0].path
    for($pass = 0; $pass -le $operationalVariables.Count; $pass++) {
        $previous = $expanded
        foreach($key in $operationalVariables.Keys) {
            $expanded = $expanded.Replace(
                ('${' + $key + '}'),
                [string]$operationalVariables[$key])
        }
        if($expanded -eq $previous) {
            return [System.IO.Path]::GetFullPath(
                [Environment]::ExpandEnvironmentVariables($expanded))
        }
    }
    throw "Operational input variables contain a substitution cycle: $Id"
}

if([string]::IsNullOrWhiteSpace($CodeBin)) {
    $CodeBin = Resolve-Oot3dOperationalInput -Id "oot3d_code_bin"
}
if([string]::IsNullOrWhiteSpace($ExHeader)) {
    $ExHeader = Resolve-Oot3dOperationalInput -Id "oot3d_exheader"
}
$codeBinPath = [System.IO.Path]::GetFullPath($CodeBin)
$exHeaderPath = [System.IO.Path]::GetFullPath($ExHeader)

foreach($required in @(
        $productManifest,
        $selection,
        $codeBinPath,
        $exHeaderPath,
        $toolchain,
        $clangCl,
        $vcpkgToolchain,
        (Join-Path $VcpkgRoot "vcpkg.exe"),
        $ninja,
        $cmake,
        $vsDevShell)) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Required whole-AOT consumer input is missing: $required"
    }
}

$keyExpression = @"
import sys
from pathlib import Path
sys.path.insert(0, str(Path(r'$runtimeRoot')))
from prepare_whole_aot_product import product_cache_key
print(product_cache_key(Path(r'$productManifest')))
"@
Write-Host "Resolving whole-AOT product identity..."
$cacheKey = (& python -c $keyExpression).Trim()
if($LASTEXITCODE -ne 0 -or $cacheKey -notmatch '^[0-9a-f]{64}$') {
    throw "Unable to calculate the whole-AOT product cache key"
}

$artifactRoot = Join-Path $CacheRoot $cacheKey
$program = Join-Path $artifactRoot "aot_program.json"
$generatedRoot = Join-Path $artifactRoot "cpp"
$generatedManifest = Join-Path $generatedRoot "whole_aot_cpp_manifest.json"
$archiveMetadataPath = Join-Path $artifactRoot "whole_aot_archive.json"
foreach($required in @(
        $program, $generatedManifest, $archiveMetadataPath)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Whole-AOT product artifact is missing: $required"
    }
}

$archiveMetadata = Get-Content -LiteralPath $archiveMetadataPath -Raw |
    ConvertFrom-Json
if([string]$archiveMetadata.cache_key -ne $cacheKey) {
    throw "Whole-AOT archive cache key does not match the current product"
}
$archive = [string]$archiveMetadata.archive
if(-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
    throw "Whole-AOT product archive is missing: $archive"
}
$archiveInfo = Get-Item -LiteralPath $archive
$archiveMarkerMatches =
    $null -ne $archiveMetadata.archive_last_write_ticks -and
    $archiveInfo.Length -eq [long]$archiveMetadata.archive_bytes -and
    $archiveInfo.LastWriteTimeUtc.Ticks -eq
        [long]$archiveMetadata.archive_last_write_ticks
if($archiveMarkerMatches) {
    Write-Host "Whole-AOT archive marker verified."
} else {
    Write-Host "Archive marker changed; verifying whole-AOT SHA-256..."
    $actualArchiveHash =
        (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if($actualArchiveHash -ne [string]$archiveMetadata.archive_sha256) {
        throw "Whole-AOT product archive SHA-256 mismatch"
    }
}
$actualGeneratedHash =
    (Get-FileHash -LiteralPath $generatedManifest -Algorithm SHA256).Hash.ToLowerInvariant()
if($actualGeneratedHash -ne [string]$archiveMetadata.generated_manifest_sha256) {
    throw "Whole-AOT generated manifest changed after archive compilation"
}
Write-Host "Whole-AOT archive identity verified."

$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$cache = Join-Path $buildPath "CMakeCache.txt"
if($ValidateOnly.IsPresent) {
    if(-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        throw "Whole-AOT consumer cache is missing: $cache"
    }
    $expectedValues = [ordered]@{
        CMAKE_BUILD_TYPE = $Configuration
        OOT3D_REBUILD_WHOLE_AOT = "OFF"
        OOT3D_REGENERATE_WHOLE_AOT = "OFF"
        OOT3D_REQUIRE_WHOLE_AOT = "ON"
        OOT3D_WHOLE_AOT_PRODUCT_MODE = "ON"
        OOT3D_ENABLE_VULKAN_RENDERER = "ON"
        OOT3D_SOURCE_OVERLAY_BUILD_DECOMP_OWNER = "OFF"
        THREE_DS_RECOMP_ENABLE_DX11 = "OFF"
    }
    foreach($entry in $expectedValues.GetEnumerator()) {
        $actual = Read-CMakeCacheValue -Cache $cache -Name $entry.Key
        if($actual -ne $entry.Value) {
            throw "CMake cache setting differs: $($entry.Key)=$actual"
        }
    }
    $expectedPaths = [ordered]@{
        CMAKE_CXX_COMPILER = $clangCl
        OOT3D_WHOLE_AOT_PROGRAM = $program
        OOT3D_WHOLE_AOT_SELECTION = $selection
        OOT3D_WHOLE_AOT_CODE_BIN = $codeBinPath
        OOT3D_WHOLE_AOT_EXHEADER = $exHeaderPath
        OOT3D_WHOLE_AOT_PRODUCT_MANIFEST = $productManifest
        OOT3D_WHOLE_AOT_GENERATED_DIR = $generatedRoot
        OOT3D_WHOLE_AOT_PREBUILT_LIBRARY = $archive
    }
    foreach($entry in $expectedPaths.GetEnumerator()) {
        $actual = [System.IO.Path]::GetFullPath(
            (Read-CMakeCacheValue -Cache $cache -Name $entry.Key))
        $expected = [System.IO.Path]::GetFullPath($entry.Value)
        if(-not $actual.Equals(
                $expected,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "CMake cache path differs: $($entry.Key)=$actual"
        }
    }
    [ordered]@{
        status = "validated"
        source = $repoRoot
        build = $buildPath
        cache_key = $cacheKey
        whole_aot_archive = $archive
        backend = "NRI_Vulkan"
        product_mode = $true
        configuration = $Configuration
    } | ConvertTo-Json
    exit 0
}

Write-Host "Loading the Visual Studio x64 environment..."
& $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Write-Host "Visual Studio environment loaded."
$env:OOT3D_LLVM_ROOT = [System.IO.Path]::GetFullPath($LlvmRoot)
$env:VCPKG_ROOT = [System.IO.Path]::GetFullPath($VcpkgRoot)

$arguments = @(
    "-S", (ConvertTo-CMakePath $repoRoot),
    "-B", (ConvertTo-CMakePath $buildPath),
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath $ninja)",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_TOOLCHAIN_FILE=$(ConvertTo-CMakePath $vcpkgToolchain)",
    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$(ConvertTo-CMakePath $toolchain)",
    "-DVCPKG_ROOT=$(ConvertTo-CMakePath $VcpkgRoot)",
    "-DVCPKG_INSTALLED_DIR=$(ConvertTo-CMakePath (Join-Path $VcpkgRoot 'installed'))",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DVCPKG_HOST_TRIPLET=x64-windows-static",
    "-DAUTOMATE_VCPKG_UPDATE=OFF",
    "-DOOT3D_LLVM_ROOT=$(ConvertTo-CMakePath $LlvmRoot)",
    "-DOOT3D_WHOLE_AOT_PROGRAM=$(ConvertTo-CMakePath $program)",
    "-DOOT3D_WHOLE_AOT_SELECTION=$(ConvertTo-CMakePath $selection)",
    "-DOOT3D_WHOLE_AOT_CODE_BIN=$(ConvertTo-CMakePath $codeBinPath)",
    "-DOOT3D_WHOLE_AOT_EXHEADER=$(ConvertTo-CMakePath $exHeaderPath)",
    "-DOOT3D_WHOLE_AOT_PRODUCT_MANIFEST=$(ConvertTo-CMakePath $productManifest)",
    "-DOOT3D_WHOLE_AOT_GENERATED_DIR=$(ConvertTo-CMakePath $generatedRoot)",
    "-DOOT3D_WHOLE_AOT_PREBUILT_LIBRARY=$(ConvertTo-CMakePath $archive)",
    "-DOOT3D_REBUILD_WHOLE_AOT=OFF",
    "-DOOT3D_REGENERATE_WHOLE_AOT=OFF",
    "-DOOT3D_REQUIRE_WHOLE_AOT=ON",
    "-DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON",
    "-DOOT3D_ENABLE_VULKAN_RENDERER=ON",
    "-DOOT3D_SOURCE_OVERLAY_BUILD_DECOMP_OWNER=OFF",
    "-DTHREE_DS_RECOMP_ENABLE_DX11=OFF",
    "-DOOT3D_FAST_DEV_LINK=ON",
    "-DTHREE_DS_RECOMP_FAST_DEV_LINK=ON"
)

if($Force -and (Test-Path -LiteralPath $cache)) {
    $arguments = @("--fresh") + $arguments
}
Write-Host "Configuring the NRI/Vulkan product consumer..."
& $cmake @arguments
if($LASTEXITCODE -ne 0) {
    throw "Whole-AOT product consumer configuration failed"
}

[ordered]@{
    status = "configured"
    source = $repoRoot
    build = $buildPath
    cache_key = $cacheKey
    whole_aot_archive = $archive
    backend = "NRI_Vulkan"
    product_mode = $true
    configuration = $Configuration
    build_command =
        ".\scripts\oot3d\Build-Oot3dLlvm.ps1 -BuildDirectory `"$buildPath`" -Target oot3d_native_game"
} | ConvertTo-Json
