param(
    [string]$ToolRoot = "",
    [string]$SceneRoot = "E:\ppssppvr\oot3d_decomp\work\extract\romfs\scene",
    [string]$SceneIndex = "",
    [string[]]$Scene = @("spot04"),
    [string]$ShardName = "oot3d-scenes-overworld",
    [string]$WorkRoot = "I:\oot3dre_work",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($SceneIndex)) {
    $SceneIndex = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_all_full.json"
}
$outputRoot = Join-Path $WorkRoot "native_scene_shards"
$outputManifest = Join-Path $outputRoot "$ShardName.manifest.json"
$outputArchive = Join-Path $outputRoot "$ShardName.o2r"

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @("-m", "oot3d_asset_tool.native_scene_shard",
        "--scene-index", $SceneIndex, "--scene-root", $SceneRoot, "--shard-name", $ShardName,
        "--output-manifest", $outputManifest, "--output-archive", $outputArchive)
    foreach ($stem in $Scene) { $arguments += @("--scene", $stem) }
    if ($Verify) { $arguments += "--verify" }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "OOT3D native scene shard failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

Write-Host "OOT3D native scene shard: $outputArchive"
Write-Host "OOT3D native scene manifest: $outputManifest"
