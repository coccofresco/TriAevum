param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [string]$SceneIndex = "",
    [string]$SceneResourceTable = "",
    [string]$QdbCatalog = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "playable_catalog"
}
if ([string]::IsNullOrWhiteSpace($SceneIndex)) {
    $SceneIndex = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\oot3d_native_scene_index_all_full.json"
}
if ([string]::IsNullOrWhiteSpace($SceneResourceTable)) {
    $SceneResourceTable = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\scene_resource_table.json"
}
if ([string]::IsNullOrWhiteSpace($QdbCatalog)) {
    $QdbCatalog = Join-Path $WorkRoot "qdb_catalog\oot3d_qdb_catalog.json"
}
$outputJson = Join-Path $OutputRoot "oot3d_asset_catalog.json"
$outputMarkdown = Join-Path $OutputRoot "oot3d_asset_catalog.md"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "-m", "oot3d_asset_tool.asset_catalog",
        "--work-root", $WorkRoot,
        "--output-json", $outputJson,
        "--output-md", $outputMarkdown
        "--scene-index", $SceneIndex
        "--scene-resource-table", $SceneResourceTable
    )
    if (Test-Path -LiteralPath $QdbCatalog) {
        $arguments += @("--qdb-catalog", $QdbCatalog)
    }
    if ($Verify) { $arguments += "--verify" }
    $sceneShardRoot = Join-Path $WorkRoot "native_scene_shards"
    if (Test-Path -LiteralPath $sceneShardRoot) {
        foreach ($manifest in Get-ChildItem -LiteralPath $sceneShardRoot -Filter "*.manifest.json" -File) {
            $arguments += @("--scene-shard-manifest", $manifest.FullName)
        }
    }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "OOT3D asset catalog failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

if ($Verify) {
    $catalog = Get-Content -LiteralPath $outputJson -Raw | ConvertFrom-Json
    if ($catalog.format -ne "oot3d_asset_catalog_v1" -or $catalog.status -ne "complete") {
        throw "OOT3D asset catalog verification failed: $($catalog.status)"
    }
    if ($catalog.record_count -lt 6000) {
        throw "OOT3D asset catalog unexpectedly contains only $($catalog.record_count) records"
    }
    if (@($catalog.duplicate_asset_ids).Count -ne 0) {
        throw "OOT3D asset catalog contains duplicate native identities"
    }
    if (@($catalog.unresolved_dependencies).Count -ne 0) {
        throw "OOT3D asset catalog contains unresolved native dependencies"
    }
}

Write-Host "OOT3D asset catalog JSON: $outputJson"
Write-Host "OOT3D asset catalog Markdown: $outputMarkdown"
