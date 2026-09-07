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

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D CMAB audit verification failed: $Message"
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
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "cmab_audit"
$auditOutput = Join-Path $outputRoot "oot3d_cmab_payload_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_cmab_payload_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-cmab-payloads",
        $ActorRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        ([string]$SampleLimit)
    )
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    archive_count = $audit.archive_count
    archives_with_cmab = $audit.archives_with_cmab
    cmab_count = $audit.cmab_count
    total_mmad_count = $audit.total_mmad_count
    archive_parse_error_count = $audit.archive_parse_error_count
    cmb_parse_error_count = $audit.cmb_parse_error_count
    cmab_size_summary = $audit.cmab_size_summary
    magic_counts = $audit.magic_counts
    declared_size_match_counts = $audit.declared_size_match_counts
    header_version_counts = $audit.header_version_counts
    layout_status_counts = $audit.layout_status_counts
    mads_record_count_counts = $audit.mads_record_count_counts
    mads_record_offsets_match_mmad_offsets_counts = $audit.mads_record_offsets_match_mmad_offsets_counts
    mmads_per_payload_counts = $audit.mmads_per_payload_counts
    mmads_native_type_counts = $audit.mmads_native_type_counts
    mmads_native_type_status_counts = $audit.mmads_native_type_status_counts
    mmads_native_target_material_counts = $audit.mmads_native_target_material_counts
    mmads_native_target_component_or_stage_counts = $audit.mmads_native_target_component_or_stage_counts
    mmads_native_target_selector_status_counts = $audit.mmads_native_target_selector_status_counts
    mmads_native_target_selector_counts = $audit.mmads_native_target_selector_counts
    mmads_native_value_kind_counts = $audit.mmads_native_value_kind_counts
    mmads_native_channel_offset_count_counts = $audit.mmads_native_channel_offset_count_counts
    mmads_native_channel_offset_status_counts = $audit.mmads_native_channel_offset_status_counts
    total_native_channel_offset_count = $audit.total_native_channel_offset_count
    total_native_channel_offset_present_count = $audit.total_native_channel_offset_present_count
    mmads_native_source_curve_count_counts = $audit.mmads_native_source_curve_count_counts
    mmads_native_source_curve_status_counts = $audit.mmads_native_source_curve_status_counts
    mmads_native_source_curve_type_counts = $audit.mmads_native_source_curve_type_counts
    total_native_source_curve_count = $audit.total_native_source_curve_count
    total_native_source_curve_decoded_count = $audit.total_native_source_curve_decoded_count
    total_native_source_curve_point_count = $audit.total_native_source_curve_point_count
    mmads_component_scalar_track_count_counts = $audit.mmads_component_scalar_track_count_counts
    mmads_component_scalar_track_status_counts = $audit.mmads_component_scalar_track_status_counts
    total_component_scalar_track_count = $audit.total_component_scalar_track_count
    total_component_scalar_keyframe_count = $audit.total_component_scalar_keyframe_count
    archive_support_counts = $audit.archive_support_counts
    archive_cmab_count_counts = $audit.archive_cmab_count_counts
    target_resolution_counts = $audit.target_resolution_counts
    target_resolved_count = $audit.target_resolved_count
    target_unresolved_or_ambiguous_count = $audit.target_unresolved_or_ambiguous_count
    target_support_counts = $audit.target_support_counts
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.archives_with_cmab -eq 146) "expected 146 actor archives with CMAB payloads"
    Assert-Condition ($summary.cmab_count -eq 474) "expected 474 CMAB payloads"
    Assert-Condition ($summary.total_mmad_count -eq 924) "expected 924 scanned MMAD records"
    Assert-Condition ($summary.archive_parse_error_count -eq 0) "expected 0 archive parse errors"
    Assert-Condition ($summary.cmb_parse_error_count -eq 0) "expected 0 embedded CMB parse errors during CMAB audit"

    Assert-Condition ($summary.cmab_size_summary.min -eq 128) "expected CMAB min size 128"
    Assert-Condition ($summary.cmab_size_summary.max -eq 301552) "expected CMAB max size 301552"
    Assert-Condition ($summary.cmab_size_summary.total -eq 1983680) "expected CMAB total byte size 1983680"
    Assert-Condition ($summary.cmab_size_summary.unique_size_count -eq 80) "expected 80 unique CMAB sizes"

    Assert-Condition ((Get-JsonValue $summary.magic_counts "ascii:cmab") -eq 474) "expected all CMAB payloads to use cmab magic"
    Assert-Condition ($summary.declared_size_match_counts.matches -eq 474) "expected all CMAB declared sizes to match"
    Assert-Condition ((Get-JsonValue $summary.header_version_counts "1") -eq 474) "expected all CMAB payloads to report version 1"

    Assert-Condition ($summary.layout_status_counts.markers_consistent -eq 474) "expected all CMAB marker layouts to be structurally consistent"
    Assert-Condition ($summary.mads_record_offsets_match_mmad_offsets_counts.yes -eq 474) "expected all CMAB MADS record tables to match MMAD marker offsets"
    Assert-Condition ((Get-JsonValue $summary.mmads_native_type_counts "1") -eq 486) "expected 486 native CMAB type-1 records"
    Assert-Condition ((Get-JsonValue $summary.mmads_native_type_counts "2") -eq 149) "expected 149 native CMAB type-2 records"
    Assert-Condition ((Get-JsonValue $summary.mmads_native_type_counts "3") -eq 56) "expected 56 native CMAB type-3 records"
    Assert-Condition ((Get-JsonValue $summary.mmads_native_type_counts "4") -eq 175) "expected 175 native CMAB type-4 records"
    Assert-Condition ((Get-JsonValue $summary.mmads_native_type_counts "5") -eq 52) "expected 52 native CMAB type-5 records"
    Assert-Condition ($summary.mmads_native_type_status_counts.factory_supported_1_to_5 -eq 918) "expected 918 native CMAB records supported by the code.bin type factory"
    Assert-Condition ($summary.mmads_native_type_status_counts.truncated_header -eq 6) "expected 6 short MADS entries with no complete MMAD header"
    Assert-Condition ($summary.mmads_native_value_kind_counts.transform_vec2_gate_1_plus_selector -eq 486) "expected 486 native transform vec2 CMAB records"
    Assert-Condition ($summary.mmads_native_value_kind_counts.texture_frame_int_gate_7_plus_stage -eq 149) "expected 149 native texture-frame CMAB records"
    Assert-Condition ($summary.mmads_native_value_kind_counts.material_color_vec4_gate_0 -eq 56) "expected 56 native material-color CMAB records"
    Assert-Condition ($summary.mmads_native_value_kind_counts.constant_color_vec4_gate_0a_plus_selector -eq 175) "expected 175 native constant-color CMAB records"
    Assert-Condition ($summary.mmads_native_value_kind_counts.transform_scalar_gate_4_plus_selector -eq 52) "expected 52 native transform scalar CMAB records"
    Assert-Condition ($summary.mmads_native_target_selector_status_counts.selector_present -eq 862) "expected 862 selector-bearing native CMAB records"
    Assert-Condition ($summary.mmads_native_target_selector_status_counts.selector_absent -eq 62) "expected 62 CMAB records without a target selector"
    Assert-Condition ($summary.mmads_native_channel_offset_count_counts.'0' -eq 6) "expected 6 CMAB records without native channel offsets"
    Assert-Condition ($summary.mmads_native_channel_offset_count_counts.'1' -eq 201) "expected 201 one-channel native CMAB records"
    Assert-Condition ($summary.mmads_native_channel_offset_count_counts.'2' -eq 486) "expected 486 two-channel native CMAB records"
    Assert-Condition ($summary.mmads_native_channel_offset_count_counts.'4' -eq 231) "expected 231 four-channel native CMAB records"
    Assert-Condition ($summary.total_native_channel_offset_count -eq 2097) "expected 2097 native CMAB channel offset slots"
    Assert-Condition ($summary.total_native_channel_offset_present_count -eq 1401) "expected 1401 present native CMAB channel offsets"
    Assert-Condition ($summary.mmads_native_channel_offset_status_counts.native_channel_offset_present -eq 1401) "expected 1401 present native CMAB channel offsets by status"
    Assert-Condition ($summary.mmads_native_channel_offset_status_counts.native_channel_offset_absent -eq 696) "expected 696 absent native CMAB channel offsets by status"
    Assert-Condition ($summary.total_native_source_curve_count -eq 1401) "expected one native CMAB source curve per present channel offset"
    Assert-Condition ($summary.total_native_source_curve_decoded_count -eq 1353) "expected 1353 decoded native CMAB source curves"
    Assert-Condition ($summary.total_native_source_curve_point_count -eq 3913) "expected 3913 decoded native CMAB source curve points"
    Assert-Condition ($summary.mmads_native_source_curve_count_counts.'0' -eq 6) "expected 6 CMAB records without native source curves"
    Assert-Condition ($summary.mmads_native_source_curve_count_counts.'1' -eq 624) "expected 624 one-curve native CMAB records"
    Assert-Condition ($summary.mmads_native_source_curve_count_counts.'2' -eq 133) "expected 133 two-curve native CMAB records"
    Assert-Condition ($summary.mmads_native_source_curve_count_counts.'3' -eq 133) "expected 133 three-curve native CMAB records"
    Assert-Condition ($summary.mmads_native_source_curve_count_counts.'4' -eq 28) "expected 28 four-curve native CMAB records"
    Assert-Condition ($summary.mmads_native_source_curve_status_counts.decoded -eq 1353) "expected 1353 decoded native CMAB source curves by status"
    Assert-Condition ($summary.mmads_native_source_curve_status_counts.source_curve_unordered_points -eq 48) "expected 48 native CMAB source curves with unordered point data"
    Assert-Condition ($summary.mmads_native_source_curve_type_counts.'1' -eq 878) "expected 878 decoded type-1 linear native CMAB source curves"
    Assert-Condition ($summary.mmads_native_source_curve_type_counts.'2' -eq 322) "expected 322 decoded type-2 Hermite native CMAB source curves"
    Assert-Condition ($summary.mmads_native_source_curve_type_counts.'3' -eq 153) "expected 153 decoded type-3 step native CMAB source curves"
    Assert-Condition ($summary.mmads_native_source_curve_type_counts.undecoded -eq 48) "expected 48 undecoded native CMAB source curves"
    Assert-Condition ($summary.mads_record_count_counts.'1' -eq 237) "expected 237 one-MMAD CMAB payloads"
    Assert-Condition ($summary.mads_record_count_counts.'2' -eq 124) "expected 124 two-MMAD CMAB payloads"
    Assert-Condition ($summary.mads_record_count_counts.'3' -eq 71) "expected 71 three-MMAD CMAB payloads"
    Assert-Condition ($summary.mads_record_count_counts.'11' -eq 1) "expected 1 eleven-MMAD CMAB payload"
    Assert-Condition ($summary.mmads_per_payload_counts.'1' -eq 237) "expected MMAD count distribution to match MADS one-count payloads"
    Assert-Condition ($summary.mmads_per_payload_counts.'2' -eq 124) "expected MMAD count distribution to match MADS two-count payloads"
    Assert-Condition ($summary.total_component_scalar_track_count -eq 1093) "expected 1093 CMAB component scalar tracks"
    Assert-Condition ($summary.total_component_scalar_keyframe_count -eq 2200) "expected 2200 decoded CMAB component scalar keyframes"
    Assert-Condition ($summary.mmads_component_scalar_track_count_counts.'0' -eq 69) "expected 69 MMAD records with no component scalar tracks"
    Assert-Condition ($summary.mmads_component_scalar_track_count_counts.'1' -eq 617) "expected 617 one-component scalar MMAD records"
    Assert-Condition ($summary.mmads_component_scalar_track_count_counts.'2' -eq 238) "expected 238 two-component scalar MMAD records"
    Assert-Condition ($summary.mmads_component_scalar_track_status_counts.component_scalar_keyframes_decoded -eq 869) "expected 869 decoded component scalar tracks"
    Assert-Condition ($summary.mmads_component_scalar_track_status_counts.component_scalar_raw_or_bounds_mismatch -eq 224) "expected 224 unresolved component scalar track bounds"

    Assert-Condition ($summary.archive_support_counts.needs_skeleton_skinning_and_animation_support -eq 327) "expected 327 CMAB payloads in skinned/animated actor archives"
    Assert-Condition ($summary.archive_support_counts.static_models_with_animation_data -eq 147) "expected 147 CMAB payloads in static-model animation archives"
    Assert-Condition ($summary.archive_cmab_count_counts.'1' -eq 75) "expected 75 archives with one CMAB"
    Assert-Condition ($summary.archive_cmab_count_counts.'59' -eq 2) "expected 2 archives with 59 CMAB payloads"

    Assert-Condition ($summary.target_resolved_count -eq 433) "expected 433 conservative CMAB target candidates"
    Assert-Condition ($summary.target_unresolved_or_ambiguous_count -eq 41) "expected 41 unresolved or ambiguous CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.single_exact_name_match -eq 199) "expected 199 exact-name CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.single_contained_name_match -eq 222) "expected 222 contained-name CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.single_texture_name_match -eq 11) "expected 11 texture-name CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.single_cmb_archive_fallback -eq 1) "expected 1 single-CMB archive fallback candidate"
    Assert-Condition ($summary.target_resolution_counts.multiple_exact_name_matches -eq 7) "expected 7 ambiguous exact-name CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.multiple_contained_name_matches -eq 22) "expected 22 ambiguous contained-name CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.multiple_texture_name_matches -eq 3) "expected 3 ambiguous texture-name CMAB target candidates"
    Assert-Condition ($summary.target_resolution_counts.multiple_cmb_unresolved -eq 9) "expected 9 unresolved multi-CMB CMAB target candidates"

    Assert-Condition ($summary.target_support_counts.static_cmb_export_supported -eq 245) "expected 245 resolved static CMAB targets"
    Assert-Condition ($summary.target_support_counts.rigid_multibone_export_supported -eq 31) "expected 31 resolved rigid multibone CMAB targets"
    Assert-Condition ($summary.target_support_counts.needs_skinning_mode_1_support -eq 11) "expected 11 resolved mode-1 CMAB targets"
    Assert-Condition ($summary.target_support_counts.needs_skinning_mode_2_support -eq 98) "expected 98 resolved mode-2 CMAB targets"
    Assert-Condition ($summary.target_support_counts.needs_skinning_mode_1_and_2_support -eq 48) "expected 48 resolved mixed-skinning CMAB targets"
}

Write-Host "OOT3D CMAB payload audit: $auditOutput"
Write-Host "OOT3D CMAB payload summary: $summaryOutput"
