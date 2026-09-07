param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$Catalog = "",
    [string]$SemanticRoutes = "",
    [string]$RoomCompilationUnitRoot = "",
    [string]$NativeAbiCatalog = "",
    [string]$Output = "",
    [string]$CoreArchive = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($Catalog)) {
    $Catalog = Join-Path $WorkRoot "playable_catalog\oot3d_asset_catalog.json"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $WorkRoot "playable_pack\oot3d_playable_pack.json"
}
if ([string]::IsNullOrWhiteSpace($SemanticRoutes)) {
    $SemanticRoutes = Join-Path $WorkRoot "playable_catalog\oot3d_semantic_route_catalog.json"
}
if ([string]::IsNullOrWhiteSpace($CoreArchive)) {
    $CoreArchive = Join-Path $WorkRoot "playable_pack\oot3d-core.o2r"
}
if ([string]::IsNullOrWhiteSpace($RoomCompilationUnitRoot)) {
    $RoomCompilationUnitRoot = Join-Path $WorkRoot "room_compilation_units"
}
if ([string]::IsNullOrWhiteSpace($NativeAbiCatalog)) {
    $operationalInputs = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "tools\oot3d\operational_inputs.json") | ConvertFrom-Json
    $evidenceRoot = ([string]$operationalInputs.variables.zelda3dRecompEvidence).Replace('${repoRoot}', $repoRoot)
    $NativeAbiCatalog = Join-Path $evidenceRoot "native_abi_catalog.json"
}
if (-not (Test-Path -LiteralPath $Catalog)) {
    throw "OOT3D asset catalog not found: $Catalog"
}
if (-not (Test-Path -LiteralPath $SemanticRoutes)) {
    throw "OOT3D semantic route catalog not found: $SemanticRoutes"
}
if (-not (Test-Path -LiteralPath $RoomCompilationUnitRoot -PathType Container)) {
    throw "OOT3D room compilation unit root not found: $RoomCompilationUnitRoot"
}
if (-not (Test-Path -LiteralPath $NativeAbiCatalog -PathType Leaf)) {
    throw "OOT3D native ABI catalog not found: $NativeAbiCatalog"
}

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "-m", "oot3d_asset_tool.playable_pack",
        "--catalog", $Catalog,
        "--work-root", $WorkRoot,
        "--output", $Output
        "--core-archive", $CoreArchive
        "--semantic-routes", $SemanticRoutes
        "--room-unit-root", $RoomCompilationUnitRoot
        "--lighting-semantics", (Join-Path $repoRoot "tools\oot3d\native_demo_host\lighting_tables\oot3d_pica_lighting_semantics.json")
        "--code-bin", "E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin"
        "--native-abi-catalog", $NativeAbiCatalog
    )
    if ($Verify) { $arguments += "--verify" }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "OOT3D playable pack failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$pack = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
Write-Host "OOT3D playable pack manifest: $Output"
Write-Host "OOT3D core archive: $CoreArchive"
Write-Host "Assigned records: $($pack.assigned_record_count) / $($pack.catalog_record_count)"
Write-Host "Room compilation units: $($pack.room_compilation_units.unit_count) mounted, $($pack.room_compilation_units.excluded_unit_count) excluded"
