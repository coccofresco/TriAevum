param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$AssetCatalog = "",
    [string]$SceneSourceTable = "",
    [string]$CutsceneBindings = "",
    [string]$SemanticGameplaySource = "",
    [string]$SceneEntryBindings = "",
    [string]$GlobalEntranceTable = "",
    [string]$OutputJson = "",
    [string]$OutputMarkdown = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($AssetCatalog)) {
    $AssetCatalog = Join-Path $WorkRoot "playable_catalog\oot3d_asset_catalog.json"
}
if ([string]::IsNullOrWhiteSpace($SceneSourceTable)) {
    $SceneSourceTable = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\scene_source_table.json"
}
if ([string]::IsNullOrWhiteSpace($CutsceneBindings)) {
    $CutsceneBindings = Join-Path $ToolRoot "profiles\semantic_cutscene_bindings.json"
}
if ([string]::IsNullOrWhiteSpace($SceneEntryBindings)) {
    $SceneEntryBindings = Join-Path $ToolRoot "profiles\semantic_scene_entry_bindings.json"
}
if ([string]::IsNullOrWhiteSpace($SemanticGameplaySource)) {
    $SemanticGameplaySource = Join-Path $ToolRoot "profiles\semantic_gameplay_source.json"
}
if ([string]::IsNullOrWhiteSpace($GlobalEntranceTable)) {
    $GlobalEntranceTable = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\scene_global_entrance_table.json"
}
if ([string]::IsNullOrWhiteSpace($OutputJson)) {
    $OutputJson = Join-Path $WorkRoot "playable_catalog\oot3d_semantic_route_catalog.json"
}
if ([string]::IsNullOrWhiteSpace($OutputMarkdown)) {
    $OutputMarkdown = Join-Path $WorkRoot "playable_catalog\oot3d_semantic_route_catalog.md"
}
foreach ($inputPath in @($AssetCatalog, $SceneSourceTable, $CutsceneBindings, $SemanticGameplaySource, $SceneEntryBindings, $GlobalEntranceTable)) {
    if (-not (Test-Path -LiteralPath $inputPath)) {
        throw "OOT3D semantic route input not found: $inputPath"
    }
}

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "-m", "oot3d_asset_tool.semantic_route_catalog",
        "--asset-catalog", $AssetCatalog,
        "--scene-source-table", $SceneSourceTable,
        "--cutscene-bindings", $CutsceneBindings,
        "--semantic-gameplay-source", $SemanticGameplaySource,
        "--scene-entry-bindings", $SceneEntryBindings,
        "--global-entrance-table", $GlobalEntranceTable,
        "--output-json", $OutputJson,
        "--output-md", $OutputMarkdown
    )
    if ($Verify) { $arguments += "--verify" }
    & python @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D semantic route catalog failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$catalog = Get-Content -LiteralPath $OutputJson -Raw | ConvertFrom-Json
Write-Host "OOT3D semantic route catalog: $OutputJson"
Write-Host "Resolved scene routes: $($catalog.summary.scene_route_count)"
Write-Host "Resolved scene-entry routes: $($catalog.summary.scene_entry_route_count)"
Write-Host "Resolved cutscene routes: $($catalog.summary.cutscene_route_count)"
Write-Host "Coverage gaps: $($catalog.summary.coverage_gap_count)"
