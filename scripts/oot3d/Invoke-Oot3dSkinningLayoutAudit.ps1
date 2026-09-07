param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 100,
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
        throw "OOT3D skinning layout audit verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "skinning_layout_audit"
$auditOutput = Join-Path $outputRoot "oot3d_actor_skinning_layout_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_skinning_layout_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-actor-skinning-layout",
        $ActorRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    audit = $auditOutput
    file_count = $audit.file_count
    archive_count = $audit.archive_count
    loose_cmb_count = $audit.loose_cmb_count
    model_counts = $audit.model_counts
    shape_counts = $audit.shape_counts
    primitive_counts = $audit.primitive_counts
    extra_attribute_kind_counts = $audit.extra_attribute_kind_counts
    layout_counts = $audit.layout_counts
    influence_width_counts = $audit.influence_width_counts
    nonzero_influence_counts = $audit.nonzero_influence_counts
    weight_sum_counts = $audit.weight_sum_counts
    skinned_shape_sample_count = $audit.skinned_shape_samples.Count
    constant_attribute_sample_count = $audit.constant_attribute_samples.Count
    skinned_model_record_count = $audit.skinned_model_records.Count
    validation_error_count = $audit.validation_error_count
    parse_error_count = $audit.parse_error_count
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.file_count -eq 349) "expected 349 actor files"
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.loose_cmb_count -eq 1) "expected 1 loose actor CMB"
    Assert-Condition ($summary.model_counts.discovered -eq 1310) "expected 1310 discovered actor CMB models"
    Assert-Condition ($summary.model_counts.parsed -eq 1310) "expected 1310 parsed actor CMB models"
    Assert-Condition ($summary.model_counts.parse_errors -eq 0) "expected 0 actor CMB skinning-layout parse errors"
    Assert-Condition ($summary.model_counts.skinned_model_count -eq 202) "expected 202 skinned actor CMB models"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "total") -eq 4118) "expected 4118 actor CMB shapes"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "skinned") -eq 583) "expected 583 skinned actor CMB shapes"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "mode_1") -eq 86) "expected 86 mode-1 skinned shapes"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "mode_2") -eq 497) "expected 497 mode-2 skinned shapes"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "mode1_valid_layout") -eq 86) "expected all mode-1 shapes to validate"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "mode2_valid_layout") -eq 497) "expected all mode-2 shapes to validate"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "mode1_vertex_rows") -eq 7421) "expected 7421 mode-1 vertex rows"
    Assert-Condition ((Get-JsonValue $summary.shape_counts "mode2_vertex_rows") -eq 100083) "expected 100083 mode-2 weighted vertex rows"
    Assert-Condition ((Get-JsonValue $summary.primitive_counts "mode_0") -eq 3535) "expected 3535 rigid mode-0 primitives"
    Assert-Condition ((Get-JsonValue $summary.primitive_counts "mode_1") -eq 97) "expected 97 mode-1 primitives"
    Assert-Condition ((Get-JsonValue $summary.primitive_counts "mode_2") -eq 1000) "expected 1000 mode-2 primitives"
    Assert-Condition ((Get-JsonValue $summary.extra_attribute_kind_counts "slot6_data") -eq 566) "expected 566 skinned shapes with per-vertex slot-6 indices"
    Assert-Condition ((Get-JsonValue $summary.extra_attribute_kind_counts "slot6_constant") -eq 17) "expected 17 skinned shapes with constant slot-6 indices"
    Assert-Condition ((Get-JsonValue $summary.extra_attribute_kind_counts "slot7_data") -eq 495) "expected 495 skinned shapes with per-vertex slot-7 weights"
    Assert-Condition ((Get-JsonValue $summary.extra_attribute_kind_counts "slot7_constant") -eq 2) "expected 2 skinned shapes with constant slot-7 weights"
    Assert-Condition ((Get-JsonValue $summary.extra_attribute_kind_counts "slot7_missing") -eq 86) "expected 86 mode-1 shapes without slot-7 weights"
    Assert-Condition ((Get-JsonValue $summary.layout_counts "mode1_slot6_data_slot7_missing") -eq 86) "expected mode-1 layout to use data slot 6 and no slot 7"
    Assert-Condition ((Get-JsonValue $summary.layout_counts "mode2_slot6_data_slot7_data") -eq 479) "expected 479 mode-2 data/data shapes"
    Assert-Condition ((Get-JsonValue $summary.layout_counts "mode2_slot6_constant_slot7_data") -eq 16) "expected 16 mode-2 constant-index/data-weight shapes"
    Assert-Condition ((Get-JsonValue $summary.layout_counts "mode2_slot6_data_slot7_constant") -eq 1) "expected 1 mode-2 data-index/constant-weight shape"
    Assert-Condition ((Get-JsonValue $summary.layout_counts "mode2_slot6_constant_slot7_constant") -eq 1) "expected 1 mode-2 constant/constant shape"
    Assert-Condition ((Get-JsonValue $summary.influence_width_counts "2") -eq 343) "expected 343 mode-2 shapes with 2 influences"
    Assert-Condition ((Get-JsonValue $summary.influence_width_counts "3") -eq 149) "expected 149 mode-2 shapes with 3 influences"
    Assert-Condition ((Get-JsonValue $summary.influence_width_counts "4") -eq 5) "expected 5 mode-2 shapes with 4 influences"
    Assert-Condition ((Get-JsonValue $summary.weight_sum_counts "100") -eq 100083) "expected every mode-2 vertex row to sum to 100 percent"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_counts "1") -eq 59285) "expected 59285 one-influence weighted rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_counts "2") -eq 37364) "expected 37364 two-influence weighted rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_counts "3") -eq 3433) "expected 3433 three-influence weighted rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_counts "4") -eq 1) "expected 1 four-influence weighted row"
    Assert-Condition ($summary.skinned_model_record_count -eq 202) "expected 202 skinned model records"
    Assert-Condition ($summary.validation_error_count -eq 0) "expected 0 skinning layout validation errors"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 parse errors"
}

Write-Host "OOT3D actor skinning layout audit: $auditOutput"
Write-Host "OOT3D actor skinning layout summary: $summaryOutput"
