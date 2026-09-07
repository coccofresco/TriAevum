param(
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json",
    [string]$OutputRoot = "",
    [string]$BuildRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$probeSource = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_native_runtime_probe.cpp"
$contractSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeResourceContract.cpp"
$nativeFormatSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeFormat.cpp"
$nativeAssetsSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeAssets.cpp"
$includeRoot = Join-Path $repoRoot "runtime/three_ds_recomp\include"
$vcpkgInclude = Join-Path $repoRoot "build-codex\vcpkg\installed\x64-windows-static\include"
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $Manifest)) {
    throw "Standalone demo manifest not found: $Manifest"
}
if (-not (Test-Path -LiteralPath $probeSource)) {
    throw "Native runtime probe source not found: $probeSource"
}
if (-not (Test-Path -LiteralPath $contractSource)) {
    throw "runtime/three_ds_recomp OOT3D contract source not found: $contractSource"
}
if (-not (Test-Path -LiteralPath $nativeFormatSource)) {
    throw "runtime/three_ds_recomp OOT3D native format source not found: $nativeFormatSource"
}
if (-not (Test-Path -LiteralPath $nativeAssetsSource)) {
    throw "runtime/three_ds_recomp OOT3D native assets source not found: $nativeAssetsSource"
}
if (-not (Test-Path -LiteralPath $vcpkgInclude)) {
    throw "vcpkg include directory not found: $vcpkgInclude"
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer command prompt not found: $vsDevCmd"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Split-Path -Parent $Manifest) "native_host"
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $OutputRoot "cpp_runtime_build"
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$exe = Join-Path $BuildRoot "oot3d_native_runtime_probe.exe"
$compileCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && " +
    "cl /nologo /std:c++20 /EHsc /utf-8 " +
    "/I`"$includeRoot`" " +
    "/I`"$vcpkgInclude`" " +
    "/Fe:`"$exe`" " +
    "`"$probeSource`" " +
    "`"$contractSource`" " +
    "`"$nativeFormatSource`" " +
    "`"$nativeAssetsSource`""

cmd.exe /d /c $compileCommand
if ($LASTEXITCODE -ne 0) {
    throw "Native runtime probe compilation failed with exit code $LASTEXITCODE"
}

& $exe --manifest $Manifest --output-root $OutputRoot
if ($LASTEXITCODE -ne 0) {
    throw "Native runtime probe failed with exit code $LASTEXITCODE"
}

if ($Verify) {
    $summary = Join-Path $OutputRoot "runtime/three_ds_recomp_native_runtime_probe.json"
    if (-not (Test-Path -LiteralPath $summary)) {
        throw "Native runtime probe summary was not written: $summary"
    }
    $json = Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
    if ($json.status -ne "valid" -or $json.issue_count -ne 0) {
        throw "Native runtime probe summary is not valid: $summary"
    }
    if ($json.runtime_n64_asset_substitution_used -ne $false -or $json.shipwright_replacement_path_used -ne $false) {
        throw "Native runtime probe reported forbidden runtime substitution: $summary"
    }
    if (-not (Test-Path -LiteralPath $json.trace)) {
        throw "Native runtime probe trace was not written: $($json.trace)"
    }
    if (-not (Test-Path -LiteralPath $json.preview_ppm)) {
        throw "Native runtime probe preview was not written: $($json.preview_ppm)"
    }
}
