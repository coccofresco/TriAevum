param(
    [string]$ToolRoot = "",
    [string]$ActorRoot = "E:\ppssppvr\oot3d_decomp\work\extract\romfs\actor",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 100,
    [switch]$NoRecords,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
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

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D actor ZSI audit verification failed: $Message"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor root"

$outputRoot = Join-Path $WorkRoot "actor_zsi_audit"
$auditOutput = Join-Path $outputRoot "oot3d_actor_zsi_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_zsi_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-actor-zsi-payloads",
        $ActorRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        ([string]$SampleLimit)
    )
    if ($NoRecords) {
        $auditArgs += "--no-records"
    }
    Invoke-Oot3dTool -Arguments $auditArgs
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    zar_file_count = $audit.zar_file_count
    parsed_zar_count = $audit.parsed_zar_count
    parse_error_count = $audit.parse_error_count
    zsi_file_count = $audit.zsi_file_count
    archive_with_zsi_count = $audit.archive_with_zsi_count
    total_size = $audit.total_size
    size_summary = $audit.size_summary
    archive_zsi_counts = $audit.archive_zsi_counts
    zsi_count_distribution = $audit.zsi_count_distribution
    embedded_parent_dir_counts = $audit.embedded_parent_dir_counts
    type_name_counts = $audit.type_name_counts
    magic4_counts = $audit.magic4_counts
    bgdata_marker_counts = $audit.bgdata_marker_counts
    version_counts = $audit.version_counts
    declared_size_delta_counts = $audit.declared_size_delta_counts
    size_mod4_counts = $audit.size_mod4_counts
    size_mod16_counts = $audit.size_mod16_counts
    footer_layout_status_counts = $audit.footer_layout_status_counts
    footer_field_counts = $audit.footer_field_counts
    totals = $audit.totals
    scene_setup_count_counts = $audit.scene_setup_count_counts
    embedded_cmb_count_counts = $audit.embedded_cmb_count_counts
    collision_candidate_count_counts = $audit.collision_candidate_count_counts
    embedded_name_count = $audit.embedded_name_count
    duplicate_embedded_names = $audit.duplicate_embedded_names
    issue_count = $audit.issue_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.zar_file_count -eq 348) "expected 348 actor ZAR files"
    Assert-Condition ($summary.parsed_zar_count -eq 348) "expected all actor ZAR files to parse"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 actor ZSI parse errors"
    Assert-Condition ($summary.zsi_file_count -eq 227) "expected 227 actor ZSI payloads"
    Assert-Condition ($summary.archive_with_zsi_count -eq 83) "expected 83 actor archives with ZSI payloads"
    Assert-Condition ($summary.total_size -eq 192628) "expected actor ZSI total byte size 192628"
    Assert-Condition ($summary.size_summary.min -eq 124) "expected actor ZSI min size 124"
    Assert-Condition ($summary.size_summary.max -eq 8132) "expected actor ZSI max size 8132"
    Assert-Condition ($summary.size_summary.unique_size_count -eq 105) "expected 105 unique actor ZSI sizes"

    Assert-Condition ($summary.zsi_count_distribution.'1' -eq 45) "expected 45 archives with 1 ZSI"
    Assert-Condition ($summary.zsi_count_distribution.'2' -eq 16) "expected 16 archives with 2 ZSI"
    Assert-Condition ($summary.zsi_count_distribution.'4' -eq 6) "expected 6 archives with 4 ZSI"
    Assert-Condition ($summary.zsi_count_distribution.'8' -eq 4) "expected 4 archives with 8 ZSI"
    Assert-Condition ($summary.archive_zsi_counts.'zelda_hidan_objects.zar' -eq 22) "expected 22 ZSI in zelda_hidan_objects.zar"
    Assert-Condition ($summary.archive_zsi_counts.'zelda_haka_objects.zar' -eq 21) "expected 21 ZSI in zelda_haka_objects.zar"
    Assert-Condition ($summary.archive_zsi_counts.'zelda_mizu_objects.zar' -eq 11) "expected 11 ZSI in zelda_mizu_objects.zar"
    Assert-Condition ($summary.archive_zsi_counts.'zelda_keep.zar' -eq 8) "expected 8 ZSI in zelda_keep.zar"
    Assert-Condition ($summary.archive_zsi_counts.'zelda_keep_opening.zar' -eq 8) "expected 8 ZSI in zelda_keep_opening.zar"

    Assert-Condition ($summary.embedded_parent_dir_counts.collision -eq 225) "expected 225 collision parent ZSI paths"
    Assert-Condition ((Get-JsonValue $summary.embedded_parent_dir_counts "switch_1_bgc/collision") -eq 1) "expected switch_1_bgc/collision path"
    Assert-Condition ((Get-JsonValue $summary.embedded_parent_dir_counts "brick_15_bgc/collision") -eq 1) "expected brick_15_bgc/collision path"
    Assert-Condition ($summary.type_name_counts.zsi -eq 227) "expected every embedded ZSI type to be zsi"
    Assert-Condition ((Get-JsonValue $summary.magic4_counts "hex:5a534901") -eq 227) "expected every ZSI magic signature"
    Assert-Condition ($summary.bgdata_marker_counts.ShUnqueen -eq 227) "expected every actor ZSI BGData marker"
    Assert-Condition ($summary.version_counts.'3' -eq 227) "expected every actor ZSI version candidate to be 3"
    Assert-Condition ($summary.declared_size_delta_counts.'60' -eq 227) "expected every actor ZSI declared-size delta 60"
    Assert-Condition ($summary.size_mod4_counts.'0' -eq 227) "expected every actor ZSI payload to be 4-byte aligned"
    Assert-Condition ($summary.footer_layout_status_counts.valid -eq 227) "expected every actor ZSI BGData footer layout to be valid"

    Assert-Condition ($summary.totals.vertex_count -eq 5664) "expected 5664 actor ZSI footer vertices"
    Assert-Condition ($summary.totals.polygon_count -eq 6938) "expected 6938 actor ZSI footer polygons"
    Assert-Condition ($summary.totals.surface_type_count -eq 318) "expected 318 actor ZSI footer surface types"
    Assert-Condition ($summary.totals.unknown_count -eq 227) "expected actor ZSI footer unknown_count total 227"
    Assert-Condition ($summary.footer_field_counts.unknown_count.'1' -eq 227) "expected footer unknown_count to be 1 for every ZSI"
    Assert-Condition ($summary.footer_field_counts.reserved_0.'0' -eq 227) "expected footer reserved_0 to be 0"
    Assert-Condition ($summary.footer_field_counts.reserved_1.'0' -eq 227) "expected footer reserved_1 to be 0"
    Assert-Condition ($summary.footer_field_counts.vertex_rel_offset.'8' -eq 227) "expected footer vertex relative offset 8"
    Assert-Condition ($summary.footer_field_counts.terminator.'0' -eq 227) "expected footer terminator 0"

    Assert-Condition ($summary.scene_setup_count_counts.'0' -eq 227) "expected actor ZSI payloads to have no scene setups"
    Assert-Condition ($summary.embedded_cmb_count_counts.'0' -eq 227) "expected actor ZSI payloads to have no embedded CMBs"
    Assert-Condition ($summary.collision_candidate_count_counts.'0' -eq 227) "expected actor ZSI payloads to have no scene collision candidates"
    Assert-Condition ($summary.embedded_name_count -eq 218) "expected 218 unique embedded ZSI names"
    Assert-Condition ($summary.duplicate_embedded_names.'collision/test_floor_bgdatainfo.zsi' -eq 3) "expected repeated test_floor BGDataInfo ZSI name"
    Assert-Condition ($summary.duplicate_embedded_names.'collision/d_brick_3_bgdatainfo.zsi' -eq 2) "expected repeated d_brick_3 BGDataInfo ZSI name"
    Assert-Condition ($summary.issue_count -eq 0) "expected 0 actor ZSI audit issues"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D actor ZSI audit: $auditOutput"
Write-Host "OOT3D actor ZSI summary: $summaryOutput"
