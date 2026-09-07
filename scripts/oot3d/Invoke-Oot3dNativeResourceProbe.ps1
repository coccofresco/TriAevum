param(
    [string]$Contract = "I:\oot3dre_work\standalone_demo\link_house\contracts\native_resource_contract.json",
    [string]$Output = "",
    [string]$BuildRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$probeSource = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_native_resource_probe.cpp"
$contractSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeResourceContract.cpp"
$nativeFormatSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeFormat.cpp"
$nativeAssetsSource = Join-Path $repoRoot "runtime/three_ds_recomp\src\ship\oot3d\Oot3dNativeAssets.cpp"
$includeRoot = Join-Path $repoRoot "runtime/three_ds_recomp\include"
$vcpkgInclude = Join-Path $repoRoot "build-codex\vcpkg\installed\x64-windows-static\include"
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $Contract)) {
    throw "Native resource contract not found: $Contract"
}
if (-not (Test-Path -LiteralPath $probeSource)) {
    throw "Native resource probe source not found: $probeSource"
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

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Split-Path -Parent (Split-Path -Parent $Contract)) "native_host\runtime/three_ds_recomp_native_resource_probe.json"
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path (Split-Path -Parent $Output) "cpp_probe_build"
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$exe = Join-Path $BuildRoot "oot3d_native_resource_probe.exe"
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
    throw "Native resource probe compilation failed with exit code $LASTEXITCODE"
}

& $exe --contract $Contract --output $Output
if ($LASTEXITCODE -ne 0) {
    throw "Native resource probe failed with exit code $LASTEXITCODE"
}

if ($Verify) {
    if (-not (Test-Path -LiteralPath $Output)) {
        throw "Native resource probe output was not written: $Output"
    }
    $json = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
    if ($json.status -ne "valid" -or $json.issue_count -ne 0) {
        throw "Native resource probe output is not valid: $Output"
    }
    if ($json.runtime_n64_asset_substitution_used -ne $false -or $json.shipwright_replacement_path_used -ne $false) {
        throw "Native resource probe reported forbidden runtime substitution: $Output"
    }
}
