param(
    [string]$RouteId = "scene_entry:SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE:KOKIRI_FOREST_INITIAL_CHILD_DAY",
    [Nullable[int]]$SetupIndex = $null,
    [string]$EvidenceSnapshot = "",
    [string]$OutputRoot = "",
    [switch]$RefreshEvidence
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$configPath = Join-Path $repoRoot "tools\oot3d\operational_inputs.json"
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$variables = @{}
foreach ($property in $config.variables.PSObject.Properties) {
    $variables[$property.Name] = [string]$property.Value
}
$variables["repoRoot"] = $repoRoot

function Expand-OperationalPath([string]$Value) {
    for ($pass = 0; $pass -le $variables.Count; $pass++) {
        $previous = $Value
        foreach ($key in $variables.Keys) {
            $Value = $Value.Replace('${' + $key + '}', [string]$variables[$key])
        }
        if ($Value -eq $previous) {
            return $Value
        }
    }
    return $Value
}

function Get-OperationalEntry([string]$Id) {
    $entry = $config.entries | Where-Object { $_.id -eq $Id }
    if ($null -eq $entry) {
        throw "Operational input is missing: $Id"
    }
    return Expand-OperationalPath ([string]$entry.path)
}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Find-LatestRoomEvidenceSnapshot([string]$Root) {
    $required = @(
        "analysis\codebin_actor_workflow_candidates.csv",
        "analysis\codebin_room_scene_semantics_symbols.csv",
        "build\analysis\decomp_batches\scene_room_lifecycle_typed.md"
    )
    foreach ($directory in Get-ChildItem -LiteralPath $Root -Directory | Sort-Object LastWriteTime -Descending) {
        $complete = Test-Path -LiteralPath (Join-Path $directory.FullName "manifest.json") -PathType Leaf
        foreach ($relative in $required) {
            $complete = $complete -and (Test-Path -LiteralPath (Join-Path $directory.FullName $relative) -PathType Leaf)
        }
        if ($complete) {
            return $directory.FullName
        }
    }
    return ""
}

$toolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
$snapshotRoot = Join-Path $repoRoot "tools\oot3d\decomp_support\evidence\zelda3drecomp"
$importer = Join-Path $repoRoot "tools\oot3d\decomp_support\scripts\import_zelda3drecomp_evidence.py"

if ($RefreshEvidence -or [string]::IsNullOrWhiteSpace($EvidenceSnapshot)) {
    if ($RefreshEvidence) {
        & python $importer
        if ($LASTEXITCODE -ne 0) {
            throw "Zelda3drecomp evidence import failed with exit code $LASTEXITCODE"
        }
    }
    if ([string]::IsNullOrWhiteSpace($EvidenceSnapshot)) {
        $EvidenceSnapshot = Get-OperationalEntry "zelda3drecomp_evidence_snapshot"
        if (-not (Test-Path -LiteralPath (Join-Path $EvidenceSnapshot "manifest.json") -PathType Leaf)) {
            $EvidenceSnapshot = Find-LatestRoomEvidenceSnapshot $snapshotRoot
        }
    }
}
if ([string]::IsNullOrWhiteSpace($EvidenceSnapshot)) {
    & python $importer
    if ($LASTEXITCODE -ne 0) {
        throw "Zelda3drecomp evidence import failed with exit code $LASTEXITCODE"
    }
    $EvidenceSnapshot = Find-LatestRoomEvidenceSnapshot $snapshotRoot
}
if ([string]::IsNullOrWhiteSpace($EvidenceSnapshot)) {
    throw "No Room Compilation Unit evidence snapshot is available"
}

$sceneRoot = Get-OperationalEntry "oot3d_romfs_scene_root"
$codeBin = Get-OperationalEntry "oot3d_code_bin"
$semanticRouteCatalog = Join-Path (Expand-OperationalPath '${workRoot}') "playable_catalog\oot3d_semantic_route_catalog.json"
$assetCatalog = Join-Path (Expand-OperationalPath '${workRoot}') "playable_catalog\oot3d_asset_catalog.json"
$sceneResourceTable = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\scene_resource_table.json"
$roomCallbackContracts = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool\profiles\room_scene_callback_native_contracts.json"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Expand-OperationalPath '${workRoot}') "room_compilation_units"
}
$safeName = $RouteId -replace '[^A-Za-z0-9_.-]', '_'
$output = Join-Path $OutputRoot ($safeName + ".json")
$markdownOutput = Join-Path $OutputRoot ($safeName + ".md")

Require-File $codeBin "OOT3D code.bin"
Require-File $semanticRouteCatalog "Semantic route catalog"
Require-File $assetCatalog "OOT3D asset catalog"
Require-File $sceneResourceTable "OOT3D scene resource table"
Require-File $roomCallbackContracts "OOT3D room callback runtime contracts"
Require-File (Join-Path $EvidenceSnapshot "manifest.json") "Evidence snapshot manifest"

$arguments = @(
    "-m", "oot3d_asset_tool",
    "compile-room-unit",
    "--scene-root", $sceneRoot,
    "--code-bin", $codeBin,
    "--semantic-route-catalog", $semanticRouteCatalog,
    "--asset-catalog", $assetCatalog,
    "--scene-resource-table", $sceneResourceTable,
    "--room-callback-contracts", $roomCallbackContracts,
    "--evidence-snapshot", $EvidenceSnapshot,
    "--route-id", $RouteId,
    "--output", $output,
    "--markdown-output", $markdownOutput
)
if ($null -ne $SetupIndex) {
    $arguments += @("--setup-index", [string]$SetupIndex)
}

$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = Join-Path $toolRoot "src"
    & python @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Room Compilation Unit compiler failed with exit code $LASTEXITCODE"
    }
    & python -m oot3d_asset_tool validate-room-unit $output
    if ($LASTEXITCODE -ne 0) {
        throw "Room Compilation Unit validation failed with exit code $LASTEXITCODE"
    }
}
finally {
    $env:PYTHONPATH = $previousPythonPath
}

Write-Host "Room Compilation Unit: $output"
Write-Host "Summary: $markdownOutput"
Write-Host "Evidence snapshot: $EvidenceSnapshot"
