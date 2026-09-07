[CmdletBinding()]
param(
    [string]$BuildDir = "build-source-native\host-clean",
    [string]$SurfaceContracts =
        "build-source-native\llvm-legalized-surface\source_surface_contracts.json",
    [string]$Output =
        "build-source-native\link-probe\nnmain_link_contracts.json"
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$build = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$log = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build-source-native\link-probe\nnmain_link.log"))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Output))
$contractsPath = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot $SurfaceContracts))
$cmake = "C:\Program Files\CMake\bin\cmake.exe"

New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $cmake --build $build --target oot3d_source_link_probe -j 12 *>&1 |
    Set-Content -LiteralPath $log -Encoding utf8
$linkExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
& python (Join-Path $repoRoot `
    "tools\oot3d\source_native_runtime\analyze_link_probe.py") `
    --log $log --surface-contracts $contractsPath --output $outputPath
$analysisExitCode = $LASTEXITCODE
if ($analysisExitCode -ne 0) {
    exit $analysisExitCode
}
if ($linkExitCode -eq 0) {
    Write-Host "Link probe complete: source closure linked successfully"
} else {
    Write-Host "Link probe exit code: $linkExitCode (source closure is incomplete)"
}
exit 0
