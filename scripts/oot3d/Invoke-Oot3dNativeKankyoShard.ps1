param(
    [string]$Source = "E:\ppssppvr\oot3d_decomp\work\extract\romfs\kankyo\BlueSky.zar",
    [string]$WorkRoot = "I:\oot3dre_work",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$toolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
$outputRoot = Join-Path $WorkRoot "playable_kankyo"
$archive = Join-Path $outputRoot "oot3d-kankyo-native.o2r"
$manifest = Join-Path $outputRoot "oot3d_native_kankyo_shard.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $toolRoot
try {
    $env:PYTHONPATH = Join-Path $toolRoot "src"
    $arguments = @("-m", "oot3d_asset_tool.native_kankyo_shard",
                   "--source", $Source, "--output", $archive,
                   "--manifest-output", $manifest)
    if ($Verify) { $arguments += "--verify" }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "native kankyo shard failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host "OOT3D native kankyo shard: $archive"
Write-Host "OOT3D native kankyo manifest: $manifest"
