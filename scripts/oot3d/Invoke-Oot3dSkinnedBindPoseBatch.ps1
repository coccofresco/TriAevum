param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 10,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($ActorRoot)) {
    $ActorRoot = Join-Path $RomFs "actor"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Invoke-Oot3dTool([string[]]$Arguments) {
    Write-Host "python -m oot3d_asset_tool $($Arguments -join ' ')"
    & python -m oot3d_asset_tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D skinned bind-pose batch verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "skinned_bind_pose_batch"
$summaryOutput = Join-Path $outputRoot "skinned_bind_pose_batch_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "batch-skinned-bind-poses",
        $ActorRoot,
        "--output",
        $outputRoot,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

$manifestPath = Join-Path $outputRoot "skinned_bind_pose_batch_manifest.json"
Require-Path $manifestPath "Skinned bind-pose batch manifest"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

$missingExports = @()
foreach ($record in $manifest.records) {
    if (-not (Test-Path -LiteralPath $record.output)) {
        $missingExports += $record.output
    }
}

$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    manifest = $manifestPath
    export_dir = $manifest.export_dir
    file_count = $manifest.file_count
    archive_count = $manifest.archive_count
    loose_cmb_count = $manifest.loose_cmb_count
    counts = $manifest.counts
    influence_width_rows = $manifest.influence_width_rows
    nonzero_influence_rows = $manifest.nonzero_influence_rows
    record_count = $manifest.records.Count
    parse_error_count = $manifest.parse_error_count
    missing_export_count = $missingExports.Count
    missing_exports = $missingExports
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($manifest.format -eq "oot3d_skinned_bind_pose_batch_v1") "unexpected manifest format"
    Assert-Condition ($summary.file_count -eq 349) "expected 349 actor files"
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.loose_cmb_count -eq 1) "expected 1 loose actor CMB"
    Assert-Condition ($summary.counts.considered_cmb -eq 1310) "expected 1310 considered actor CMB models"
    Assert-Condition ($summary.counts.parsed -eq 1310) "expected 1310 parsed actor CMB models"
    Assert-Condition ($summary.counts.exported -eq 202) "expected 202 skinned bind-pose exports"
    Assert-Condition ($summary.counts.skipped_unskinned -eq 1108) "expected 1108 unskinned actor CMB skips"
    Assert-Condition ($summary.counts.failed -eq 0) "expected 0 failed CMB exports"
    Assert-Condition ($summary.counts.failed_archives -eq 0) "expected 0 failed actor archives"
    Assert-Condition ($summary.counts.mesh_count -eq 1301) "expected 1301 exported mesh records including rigid primitives inside skinned meshes"
    Assert-Condition ($summary.counts.skinned_primitive_count -eq 1097) "expected 1097 skinned primitives"
    Assert-Condition ($summary.counts.mode_1_primitive_count -eq 97) "expected 97 mode-1 skinned primitives"
    Assert-Condition ($summary.counts.mode_2_primitive_count -eq 1000) "expected 1000 mode-2 skinned primitives"
    Assert-Condition ($summary.counts.rigid_primitives_inside_skinned_meshes -eq 718) "expected 718 rigid primitives inside exported skinned meshes"
    Assert-Condition ($summary.counts.skinned_vertex_rows -eq 132296) "expected 132296 skinned vertex rows"
    Assert-Condition ($summary.counts.skeleton_bone_count -eq 3280) "expected 3280 exported skeleton bones"
    Assert-Condition ($summary.counts.finite_bind_world_matrix_entries -eq 52480) "expected 52480 finite bind matrix entries"
    Assert-Condition ($summary.counts.validation_error_count -eq 0) "expected 0 bind-pose validation errors"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "1") -eq 32213) "expected 32213 width-1 rows"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "2") -eq 47528) "expected 47528 width-2 rows"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "3") -eq 52345) "expected 52345 width-3 rows"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "4") -eq 210) "expected 210 width-4 rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "1") -eq 91498) "expected 91498 one-weight rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "2") -eq 37364) "expected 37364 two-weight rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "3") -eq 3433) "expected 3433 three-weight rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "4") -eq 1) "expected 1 four-weight row"
    Assert-Condition ($summary.record_count -eq 202) "expected 202 manifest records"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 parse errors"
    Assert-Condition ($summary.missing_export_count -eq 0) "expected every manifest export file to exist"
}

Write-Host "OOT3D skinned bind-pose batch manifest: $manifestPath"
Write-Host "OOT3D skinned bind-pose batch summary: $summaryOutput"
