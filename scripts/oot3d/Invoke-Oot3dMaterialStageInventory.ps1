param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 200,
    [int]$ModelLimit = 0,
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

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D material stage inventory verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$outputRoot = Join-Path $WorkRoot "material_stage_inventory"
$inventoryOutput = Join-Path $outputRoot "oot3d_material_stage_inventory.json"
$summaryOutput = Join-Path $outputRoot "oot3d_material_stage_inventory_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $inventoryArgs = @(
        "inventory-material-stages",
        $RomFs,
        "--output",
        $inventoryOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
    if ($ModelLimit -gt 0) {
        $inventoryArgs += "--model-limit"
        $inventoryArgs += [string]$ModelLimit
    }
    Invoke-Oot3dTool -Arguments $inventoryArgs
}
finally {
    Pop-Location
}

$inventory = Get-Content -LiteralPath $inventoryOutput -Raw | ConvertFrom-Json
$nonGapNonMaterialIndexSamples = @($inventory.raw_texture_stage_non_gap_non_material_index_candidate_samples)
$summary = [ordered]@{
    romfs = (Resolve-Path $RomFs).Path
    inventory = $inventoryOutput
    file_count = $inventory.file_count
    model_limit = $inventory.model_limit
    sample_limit = $inventory.sample_limit
    model_counts = $inventory.model_counts
    material_count = $inventory.material_count
    textured_material_count = $inventory.textured_material_count
    multi_texture_material_count = $inventory.multi_texture_material_count
    alpha_test_material_count = $inventory.alpha_test_material_count
    blended_material_count = $inventory.blended_material_count
    primary_texture_coord_transform_count = $inventory.primary_texture_coord_transform_count
    secondary_texture_coord_transform_count = $inventory.secondary_texture_coord_transform_count
    raw_texture_stage_selector_counts = $inventory.raw_texture_stage_selector_counts
    raw_texture_stage_candidate_summary_counts = $inventory.raw_texture_stage_candidate_summary_counts
    raw_texture_stage_export_gap_candidate_summary_counts = $inventory.raw_texture_stage_export_gap_candidate_summary_counts
    raw_texture_stage_non_gap_candidate_summary_counts = $inventory.raw_texture_stage_non_gap_candidate_summary_counts
    raw_texture_stage_export_classification_counts = $inventory.raw_texture_stage_export_classification_counts
    raw_texture_stage_export_gap_classification_counts = $inventory.raw_texture_stage_export_gap_classification_counts
    raw_texture_stage_export_blocker_counts = $inventory.raw_texture_stage_export_blocker_counts
    raw_texture_stage_alignment_case_counts = $inventory.raw_texture_stage_alignment_case_counts
    raw_texture_stage_export_gap_alignment_case_counts = $inventory.raw_texture_stage_export_gap_alignment_case_counts
    raw_texture_stage_resolution_case_counts = $inventory.raw_texture_stage_resolution_case_counts
    raw_texture_stage_export_gap_resolution_case_counts = $inventory.raw_texture_stage_export_gap_resolution_case_counts
    raw_texture_stage_export_order_case_counts = $inventory.raw_texture_stage_export_order_case_counts
    raw_texture_stage_export_gap_order_case_counts = $inventory.raw_texture_stage_export_gap_order_case_counts
    raw_texture_stage_f3d_limit_case_counts = $inventory.raw_texture_stage_f3d_limit_case_counts
    raw_texture_stage_slot_bounds_case_counts = $inventory.raw_texture_stage_slot_bounds_case_counts
    raw_texture_stage_export_gap_slot_bounds_case_counts = $inventory.raw_texture_stage_export_gap_slot_bounds_case_counts
    raw_texture_stage_slot_sequence_case_counts = $inventory.raw_texture_stage_slot_sequence_case_counts
    raw_texture_stage_export_gap_slot_sequence_case_counts = $inventory.raw_texture_stage_export_gap_slot_sequence_case_counts
    raw_texture_stage_oob_delta_counts = $inventory.raw_texture_stage_oob_delta_counts
    raw_texture_stage_export_gap_oob_delta_counts = $inventory.raw_texture_stage_export_gap_oob_delta_counts
    raw_texture_stage_oob_delta_signature_counts = $inventory.raw_texture_stage_oob_delta_signature_counts
    raw_texture_stage_export_gap_oob_delta_signature_counts = $inventory.raw_texture_stage_export_gap_oob_delta_signature_counts
    raw_texture_stage_unexported_stage_position_counts = $inventory.raw_texture_stage_unexported_stage_position_counts
    raw_texture_stage_export_gap_unexported_stage_position_counts = $inventory.raw_texture_stage_export_gap_unexported_stage_position_counts
    raw_texture_stage_non_gap_unexported_stage_position_counts = $inventory.raw_texture_stage_non_gap_unexported_stage_position_counts
    raw_texture_stage_unexported_material_ref_stage_position_counts = $inventory.raw_texture_stage_unexported_material_ref_stage_position_counts
    raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts = $inventory.raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts
    raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts = $inventory.raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts
    raw_texture_stage_unexported_material_index_ref_stage_position_counts = $inventory.raw_texture_stage_unexported_material_index_ref_stage_position_counts
    raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts = $inventory.raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts
    raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts = $inventory.raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts
    raw_texture_stage_non_material_index_unexported_stage_position_counts = $inventory.raw_texture_stage_non_material_index_unexported_stage_position_counts
    raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts = $inventory.raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts
    raw_texture_stage_non_gap_non_material_index_unexported_stage_position_counts = $inventory.raw_texture_stage_non_gap_non_material_index_unexported_stage_position_counts
    raw_texture_stage_direct_unexported_stage_position_counts = $inventory.raw_texture_stage_direct_unexported_stage_position_counts
    raw_texture_stage_export_gap_direct_unexported_stage_position_counts = $inventory.raw_texture_stage_export_gap_direct_unexported_stage_position_counts
    raw_texture_stage_non_gap_direct_unexported_stage_position_counts = $inventory.raw_texture_stage_non_gap_direct_unexported_stage_position_counts
    exported_texture_stage_counts = $inventory.exported_texture_stage_counts
    secondary_texture_coord_transform_kind_counts = $inventory.secondary_texture_coord_transform_kind_counts
    secondary_texture_coord_transform_signature_top_counts = $inventory.secondary_texture_coord_transform_signature_top_counts
    raw_texture_stage_mapper_mismatch_count = $inventory.raw_texture_stage_mapper_mismatch_count
    raw_texture_stage_baked_extra_material_count = $inventory.raw_texture_stage_baked_extra_material_count
    raw_texture_stage_baked_extra_total = $inventory.raw_texture_stage_baked_extra_total
    raw_texture_stage_export_gap_material_count = $inventory.raw_texture_stage_export_gap_material_count
    raw_texture_stage_export_gap_total = $inventory.raw_texture_stage_export_gap_total
    texture_stage_risk_material_count = $inventory.texture_stage_risk_material_count
    texture_stage_risk_counts = $inventory.texture_stage_risk_counts
    issue_counts = $inventory.issue_counts
    raw_texture_stage_export_gap_candidate_sample_pattern_count = @($inventory.raw_texture_stage_export_gap_candidate_samples.PSObject.Properties).Count
    raw_texture_stage_export_gap_candidate_sample_count = @(
        $inventory.raw_texture_stage_export_gap_candidate_samples.PSObject.Properties |
            ForEach-Object { @($_.Value).Count }
    ) | Measure-Object -Sum | Select-Object -ExpandProperty Sum
    raw_texture_stage_non_gap_candidate_sample_pattern_count = @($inventory.raw_texture_stage_non_gap_candidate_samples.PSObject.Properties).Count
    raw_texture_stage_non_gap_candidate_sample_count = @(
        $inventory.raw_texture_stage_non_gap_candidate_samples.PSObject.Properties |
            ForEach-Object { @($_.Value).Count }
    ) | Measure-Object -Sum | Select-Object -ExpandProperty Sum
    raw_texture_stage_non_gap_non_material_index_candidate_sample_count = $nonGapNonMaterialIndexSamples.Count
    raw_texture_stage_non_gap_non_material_index_candidate_count = [int](
        @(
            $nonGapNonMaterialIndexSamples |
                ForEach-Object { @($_.raw_texture_stage_non_gap_non_material_index_candidates).Count }
        ) | Measure-Object -Sum | Select-Object -ExpandProperty Sum
    )
    sample_record_count = $inventory.sample_records.Count
    secondary_texture_coord_transform_sample_count = $inventory.secondary_texture_coord_transform_samples.Count
    parse_error_count = $inventory.parse_error_count
}

$summary | ConvertTo-Json -Depth 10 | Out-File -LiteralPath $summaryOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.file_count -gt 0) "RomFS contains no files"
    Assert-Condition ($summary.model_counts.discovered -gt 0) "no CMB models discovered"
    Assert-Condition ($summary.model_counts.parsed -gt 0) "no CMB models parsed"
    Assert-Condition ($summary.material_count -gt 0) "no materials counted"
    Assert-Condition ($summary.textured_material_count -gt 0) "no textured materials counted"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample output exceeded SampleLimit"
    Assert-Condition ($summary.secondary_texture_coord_transform_sample_count -le $SampleLimit) "secondary transform sample output exceeded SampleLimit"
    Assert-Condition (
        $summary.model_counts.parsed -eq ($summary.model_counts.static_candidate + $summary.model_counts.nonstatic_candidate)
    ) "parsed model count does not match static + nonstatic counts"
    if ($ModelLimit -eq 0) {
        Assert-Condition ($summary.model_counts.discovered -eq 1998) "expected 1998 discovered CMB models"
        Assert-Condition ($summary.model_counts.parsed -eq 1998) "expected 1998 parsed CMB models"
        Assert-Condition ($summary.model_counts.static_candidate -eq 1508) "expected 1508 static CMB candidates"
        Assert-Condition ($summary.model_counts.nonstatic_candidate -eq 490) "expected 490 nonstatic CMB candidates"
        Assert-Condition ($summary.model_counts.parse_errors -eq 0) "expected 0 known CMB parse errors"
        Assert-Condition ($summary.material_count -eq 11173) "expected 11173 materials"
        Assert-Condition ($summary.textured_material_count -eq 10998) "expected 10998 textured materials"
        Assert-Condition ($summary.multi_texture_material_count -eq 1640) "expected 1640 multi-texture materials"
        Assert-Condition ($summary.raw_texture_stage_mapper_mismatch_count -eq 1473) "expected 1473 raw stage mapper mismatches"
        Assert-Condition ($summary.raw_texture_stage_baked_extra_material_count -eq 4) "expected 4 raw extra-stage baked materials"
        Assert-Condition ($summary.raw_texture_stage_baked_extra_total -eq 4) "expected raw extra-stage baked total 4"
        Assert-Condition ($summary.raw_texture_stage_export_gap_material_count -eq 1346) "expected 1346 raw stage export-gap materials"
        Assert-Condition ($summary.raw_texture_stage_export_gap_total -eq 1551) "expected raw stage export gap total 1551"
        Assert-Condition ($summary.texture_stage_risk_material_count -eq 2154) "expected 2154 texture-stage risk materials"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_selector_counts "stage_count_1") -eq 8681) "expected 8681 raw stage_count_1 materials"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_selector_counts "stage_count_2") -eq 1871) "expected 1871 raw stage_count_2 materials"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_selector_counts "stage_count_3") -eq 507) "expected 507 raw stage_count_3 materials"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_selector_counts "stage_count_4") -eq 103) "expected 103 raw stage_count_4 materials"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_selector_counts "stage_count_5") -eq 10) "expected 10 raw stage_count_5 materials"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_selector_counts "stage_count_6") -eq 1) "expected 1 raw stage_count_6 material"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_candidate_summary_counts "stage=1;valid=1;exported=1;unexported=0;active=1;primary=yes;secondary=no") -eq 5276) "expected 5276 single-stage primary raw texture candidates"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_candidate_summary_counts "stage=2;valid=1;exported=1;unexported=0;active=0;primary=yes;secondary=no") -eq 33) "expected 33 two-stage raw gaps with a raw primary override and no valid secondary"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_candidate_summary_counts "stage=2;valid=1;exported=1;unexported=0;active=1;primary=yes;secondary=no") -eq 244) "expected 244 two-stage raw gaps with only exported primary as valid texture candidate"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_candidate_summary_counts "stage=2;valid=2;exported=1;unexported=1;active=1;primary=yes;secondary=no") -eq 0) "expected 0 remaining two-stage raw gaps with one exportable candidate"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_candidate_summary_counts "stage=3;valid=3;exported=2;unexported=1;active=2;primary=yes;secondary=yes") -eq 59) "expected 59 three-stage raw gaps with primary, secondary, and one unexported valid texture candidate"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_classification_counts "covered_by_current_export") -eq 9823) "expected 9823 raw-stage classifications covered by current export"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_classification_counts "covered_by_baked_extra_texture_stage") -eq 4) "expected 4 raw-stage classifications covered by baked extra texture stage"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_classification_counts "candidate_two_texture_raw_secondary") -eq 0) "expected 0 remaining raw-stage classifications with a two-texture raw secondary candidate"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_classification_counts "blocked_candidate_alignment") -eq 729) "expected 729 raw-stage classifications blocked by candidate alignment"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_classification_counts "blocked_f3d_texture_stage_limit") -eq 617) "expected 617 raw-stage classifications blocked by the F3D texture-stage limit"
        $f3dLimitCaseTotal = @(
            $summary.raw_texture_stage_f3d_limit_case_counts.PSObject.Properties |
                ForEach-Object { [int]$_.Value }
        ) | Measure-Object -Sum | Select-Object -ExpandProperty Sum
        Assert-Condition ($f3dLimitCaseTotal -eq 617) "expected 617 categorized F3D-limit raw-stage cases"
        Assert-Condition (@($summary.raw_texture_stage_f3d_limit_case_counts.PSObject.Properties).Count -eq 23) "expected 23 F3D-limit raw-stage case buckets"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_f3d_limit_case_counts "stage3_prefix_matches_exported_order_one_extra_p2") -eq 102) "expected 102 simple stage3 F3D-limit raw gaps"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_classification_counts "candidate_two_texture_raw_secondary") -eq 0) "expected 0 remaining raw export gaps with a two-texture raw secondary candidate"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_classification_counts "blocked_candidate_alignment") -eq 729) "expected 729 raw export gaps blocked by candidate alignment"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_classification_counts "blocked_f3d_texture_stage_limit") -eq 617) "expected 617 raw export gaps blocked by the F3D texture-stage limit"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_blocker_counts "raw_stage_count_exceeds_f3d_two_texture_limit") -eq 617) "expected 617 raw-stage blockers for the F3D texture-stage limit"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_blocker_counts "raw_stage_slot_not_texture_index") -eq 1111) "expected 1111 raw-stage blockers where a raw slot is not a texture index"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_blocker_counts "selected_primary_not_in_raw_selector") -eq 804) "expected 804 raw-stage blockers where the selected primary is not in the raw selector"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_blocker_counts "no_distinct_valid_raw_texture_candidate") -eq 1066) "expected 1066 raw-stage blockers without a distinct valid raw texture candidate"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_blocker_counts "no_free_f3d_texture_stage") -eq 266) "expected 266 raw-stage blockers with no free F3D texture stage"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "covered_by_current_export") -eq 9823) "expected 9823 covered raw-stage alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "covered_by_baked_extra_texture_stage") -eq 4) "expected 4 baked raw-stage alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "blocked_f3d_texture_stage_limit") -eq 617) "expected 617 F3D-limit raw-stage alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "stage2_all_raw_slots_oob_primary_selected") -eq 277) "expected 277 stage2 all-oob raw alignment cases with a selected primary"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "stage2_primary_only_second_raw_slot_oob") -eq 277) "expected 277 stage2 raw alignment cases with only exported primary valid"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "untextured_all_raw_slots_oob") -eq 175) "expected 175 untextured all-oob raw alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "stage2_two_valid_raw_textures_primary_not_selected") -eq 0) "expected 0 remaining stage2 raw alignment cases with two valid raw textures but selected primary absent"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_alignment_case_counts "stage2_one_valid_raw_texture_primary_not_selected") -eq 0) "expected 0 remaining stage2 raw alignment cases with one valid raw texture and selected primary absent"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_alignment_case_counts "blocked_f3d_texture_stage_limit") -eq 617) "expected 617 raw export-gap alignment cases blocked by F3D stage limit"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_alignment_case_counts "stage2_all_raw_slots_oob_primary_selected") -eq 277) "expected 277 raw export-gap stage2 all-oob alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_alignment_case_counts "stage2_primary_only_second_raw_slot_oob") -eq 277) "expected 277 raw export-gap primary-only alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_alignment_case_counts "untextured_all_raw_slots_oob") -eq 175) "expected 175 raw export-gap untextured all-oob alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_alignment_case_counts "stage2_two_valid_raw_textures_primary_not_selected") -eq 0) "expected 0 remaining raw export-gap two-valid-primary-absent alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_alignment_case_counts "stage2_one_valid_raw_texture_primary_not_selected") -eq 0) "expected 0 remaining raw export-gap one-valid-primary-absent alignment cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_resolution_case_counts "covered_by_current_export") -eq 9823) "expected 9823 covered raw-stage resolution cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_resolution_case_counts "covered_by_baked_extra_texture_stage") -eq 4) "expected 4 baked raw-stage resolution cases"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "blocked_f3d_limit_all_raw_slots_valid") -eq 235) "expected 235 raw export gaps with all raw slots valid but blocked by the F3D limit"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "blocked_f3d_limit_boundary_oob") -eq 20) "expected 20 F3D-limit raw export gaps at texture_count boundary OOB"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "blocked_f3d_limit_offset_oob") -eq 188) "expected 188 F3D-limit raw export gaps with offset OOB slots"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "blocked_f3d_limit_valid_prefix_boundary_oob") -eq 174) "expected 174 F3D-limit raw export gaps with valid prefix then boundary OOB"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "probable_non_texture_boundary_oob") -eq 241) "expected 241 raw export gaps that likely point at boundary non-texture data"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "probable_non_texture_boundary_oob_after_valid_prefix") -eq 277) "expected 277 raw export gaps with valid texture prefix then probable boundary non-texture data"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_resolution_case_counts "unknown_oob_texture_or_material_indirection") -eq 211) "expected 211 raw export gaps with unknown offset OOB indirection"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_order_case_counts "raw_prefix_matches_exported_order") -eq 7307) "expected 7307 raw-stage order cases where the raw prefix matches exported texture order"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_order_case_counts "raw_prefix_starts_with_exported_secondary") -eq 10) "expected 10 raw-stage order cases where raw prefix starts with exported secondary"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_order_case_counts "selected_primary_absent_from_raw_order") -eq 1434) "expected 1434 raw-stage order cases where selected primary is absent from raw order"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_order_case_counts "no_exported_texture_stages") -eq 175) "expected 175 raw export gaps with no exported texture stages"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_order_case_counts "raw_prefix_contains_oob_slot") -eq 509) "expected 509 raw export gaps whose raw prefix contains an OOB slot"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_order_case_counts "raw_prefix_matches_exported_order") -eq 534) "expected 534 raw export gaps where raw prefix matches exported texture order"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_order_case_counts "raw_prefix_starts_with_exported_primary_then_differs") -eq 4) "expected 4 raw export gaps where raw prefix starts with exported primary then differs"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_order_case_counts "raw_prefix_starts_with_exported_secondary") -eq 0) "expected 0 raw export gaps where raw prefix starts with exported secondary"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_order_case_counts "selected_primary_absent_from_raw_order") -eq 124) "expected 124 raw export gaps where selected primary is absent from raw order"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_bounds_case_counts "stage2__valid_p0__oob_p1") -eq 277) "expected 277 raw export-gap stage2 slot layouts with valid first slot and OOB second slot"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_bounds_case_counts "stage2__oob_p0_p1") -eq 283) "expected 283 raw export-gap stage2 slot layouts with both slots OOB"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_bounds_case_counts "stage3__valid_p0_p1_p2") -eq 205) "expected 205 raw export-gap stage3 slot layouts with all slots valid but over the F3D stage limit"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_sequence_case_counts "texture_count_boundary_oob_run") -eq 261) "expected 261 raw export-gap OOB slot sequences starting at texture_count"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_sequence_case_counts "valid_prefix_then_texture_count_boundary_oob_run") -eq 451) "expected 451 raw export-gap slot sequences with a valid prefix then texture_count boundary OOB run"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_sequence_case_counts "contiguous_all_valid") -eq 235) "expected 235 raw export-gap slot sequences with all slots valid"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_slot_sequence_case_counts "contiguous_oob_run_offset_from_texture_count") -eq 399) "expected 399 raw export-gap OOB slot sequences offset from texture_count"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_counts "delta_0") -eq 712) "expected 712 raw export-gap OOB slots equal to texture_count"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_counts "delta_1") -eq 256) "expected 256 raw export-gap OOB slots equal to texture_count + 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_counts "delta_2") -eq 155) "expected 155 raw export-gap OOB slots equal to texture_count + 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_signature_counts "none") -eq 235) "expected 235 raw export gaps with no OOB delta signature"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_signature_counts "delta_0") -eq 540) "expected 540 raw export gaps with a single texture_count OOB slot"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_signature_counts "delta_0_1") -eq 148) "expected 148 raw export gaps with texture_count and texture_count + 1 OOB slots"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_oob_delta_signature_counts "delta_1_2") -eq 55) "expected 55 raw export gaps with offset texture_count + 1/+2 OOB slots"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_stage_position_counts "stage_position_0") -eq 1465) "expected 1465 unexported valid raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_stage_position_counts "stage_position_1") -eq 317) "expected 317 unexported valid raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_stage_position_counts "stage_position_2") -eq 247) "expected 247 unexported valid raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_stage_position_counts "stage_position_3") -eq 30) "expected 30 unexported valid raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_stage_position_counts "stage_position_0") -eq 141) "expected 141 export-gap unexported valid raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_stage_position_counts "stage_position_1") -eq 135) "expected 135 export-gap unexported valid raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_stage_position_counts "stage_position_2") -eq 247) "expected 247 export-gap unexported valid raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_stage_position_counts "stage_position_3") -eq 30) "expected 30 export-gap unexported valid raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_stage_position_counts "stage_position_0") -eq 1324) "expected 1324 non-gap unexported valid raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_stage_position_counts "stage_position_1") -eq 182) "expected 182 non-gap unexported valid raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_stage_position_counts "stage_position_2") -eq 0) "expected 0 non-gap unexported valid raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_stage_position_counts "stage_position_3") -eq 0) "expected 0 non-gap unexported valid raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_ref_stage_position_counts "stage_position_0") -eq 39) "expected 39 unexported raw material refs at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_ref_stage_position_counts "stage_position_1") -eq 53) "expected 53 unexported raw material refs at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_ref_stage_position_counts "stage_position_2") -eq 33) "expected 33 unexported raw material refs at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_ref_stage_position_counts "stage_position_3") -eq 2) "expected 2 unexported raw material refs at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts "stage_position_0") -eq 26) "expected 26 export-gap unexported raw material refs at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts "stage_position_1") -eq 25) "expected 25 export-gap unexported raw material refs at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts "stage_position_2") -eq 33) "expected 33 export-gap unexported raw material refs at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts "stage_position_3") -eq 2) "expected 2 export-gap unexported raw material refs at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts "stage_position_0") -eq 13) "expected 13 non-gap unexported raw material refs at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts "stage_position_1") -eq 28) "expected 28 non-gap unexported raw material refs at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_index_ref_stage_position_counts "stage_position_0") -eq 1454) "expected 1454 unexported raw material-index refs at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_index_ref_stage_position_counts "stage_position_1") -eq 301) "expected 301 unexported raw material-index refs at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_index_ref_stage_position_counts "stage_position_2") -eq 216) "expected 216 unexported raw material-index refs at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_unexported_material_index_ref_stage_position_counts "stage_position_3") -eq 22) "expected 22 unexported raw material-index refs at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts "stage_position_0") -eq 134) "expected 134 export-gap unexported raw material-index refs at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts "stage_position_1") -eq 127) "expected 127 export-gap unexported raw material-index refs at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts "stage_position_2") -eq 216) "expected 216 export-gap unexported raw material-index refs at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts "stage_position_3") -eq 22) "expected 22 export-gap unexported raw material-index refs at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts "stage_position_0") -eq 1320) "expected 1320 non-gap unexported raw material-index refs at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts "stage_position_1") -eq 174) "expected 174 non-gap unexported raw material-index refs at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_material_index_unexported_stage_position_counts "stage_position_0") -eq 11) "expected 11 non-material-index unexported raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_material_index_unexported_stage_position_counts "stage_position_1") -eq 16) "expected 16 non-material-index unexported raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_material_index_unexported_stage_position_counts "stage_position_2") -eq 31) "expected 31 non-material-index unexported raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_material_index_unexported_stage_position_counts "stage_position_3") -eq 8) "expected 8 non-material-index unexported raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts "stage_position_0") -eq 7) "expected 7 export-gap non-material-index unexported raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts "stage_position_1") -eq 8) "expected 8 export-gap non-material-index unexported raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts "stage_position_2") -eq 31) "expected 31 export-gap non-material-index unexported raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts "stage_position_3") -eq 8) "expected 8 export-gap non-material-index unexported raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_non_material_index_unexported_stage_position_counts "stage_position_0") -eq 4) "expected 4 non-gap non-material-index unexported raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_non_material_index_unexported_stage_position_counts "stage_position_1") -eq 8) "expected 8 non-gap non-material-index unexported raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_direct_unexported_stage_position_counts "stage_position_0") -eq 1426) "expected 1426 direct unexported raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_direct_unexported_stage_position_counts "stage_position_1") -eq 264) "expected 264 direct unexported raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_direct_unexported_stage_position_counts "stage_position_2") -eq 214) "expected 214 direct unexported raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_direct_unexported_stage_position_counts "stage_position_3") -eq 28) "expected 28 direct unexported raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_direct_unexported_stage_position_counts "stage_position_0") -eq 115) "expected 115 export-gap direct unexported raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_direct_unexported_stage_position_counts "stage_position_1") -eq 110) "expected 110 export-gap direct unexported raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_direct_unexported_stage_position_counts "stage_position_2") -eq 214) "expected 214 export-gap direct unexported raw textures at stage position 2"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_export_gap_direct_unexported_stage_position_counts "stage_position_3") -eq 28) "expected 28 export-gap direct unexported raw textures at stage position 3"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_direct_unexported_stage_position_counts "stage_position_0") -eq 1311) "expected 1311 non-gap direct unexported raw textures at stage position 0"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_direct_unexported_stage_position_counts "stage_position_1") -eq 154) "expected 154 non-gap direct unexported raw textures at stage position 1"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=1;valid=1;exported=0;unexported=1;active=0;primary=no;secondary=no") -eq 1136) "expected 1136 non-gap single-stage raw texture replacement candidates"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=2;valid=2;exported=0;unexported=2;active=0;primary=no;secondary=no") -eq 174) "expected 174 non-gap two-stage raw texture replacement candidates with no exported raw match"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=2;valid=2;exported=1;unexported=1;active=1;primary=no;secondary=yes") -eq 0) "expected 0 non-gap two-stage raw texture replacement candidates with only secondary in raw"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=2;valid=2;exported=1;unexported=1;active=1;primary=yes;secondary=no") -eq 8) "expected 8 non-gap two-stage raw texture replacement candidates with only primary in raw"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=2;valid=2;exported=1;unexported=1;active=2;primary=no;secondary=yes") -eq 0) "expected 0 non-gap active-mapper raw pairs with selected primary absent"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=2;valid=2;exported=2;unexported=0;active=2;primary=yes;secondary=yes") -eq 302) "expected 302 non-gap active-mapper raw pairs covered by export"
        Assert-Condition ((Get-JsonValue $summary.raw_texture_stage_non_gap_candidate_summary_counts "stage=2;valid=2;exported=2;unexported=0;active=1;primary=yes;secondary=yes") -eq 288) "expected 288 non-gap two-stage raw prefixes with one active mapper covered by export"
        Assert-Condition ($summary.raw_texture_stage_export_gap_candidate_sample_pattern_count -eq 34) "expected 34 raw gap candidate sample patterns"
        Assert-Condition ($summary.raw_texture_stage_export_gap_candidate_sample_count -eq 131) "expected 131 raw gap candidate samples"
        Assert-Condition ($summary.raw_texture_stage_non_gap_candidate_sample_pattern_count -eq 4) "expected 4 non-gap raw candidate sample patterns"
        Assert-Condition ($summary.raw_texture_stage_non_gap_candidate_sample_count -eq 20) "expected 20 non-gap raw candidate samples"
        Assert-Condition ($summary.raw_texture_stage_non_gap_non_material_index_candidate_sample_count -eq 12) "expected 12 non-gap non-material-index raw candidate samples"
        Assert-Condition ($summary.raw_texture_stage_non_gap_non_material_index_candidate_count -eq 12) "expected 12 non-gap non-material-index raw candidates"
        Assert-Condition ((Get-JsonValue $summary.issue_counts "multi_texture_without_distinct_secondary") -eq 9) "expected 9 multi-texture materials without a distinct secondary"
        Assert-Condition ((Get-JsonValue $summary.issue_counts "repeated_active_texture_index") -eq 232) "expected 232 repeated active texture-index cases"
        Assert-Condition ((Get-JsonValue $summary.issue_counts "rotated_texture_coord") -eq 7) "expected 7 unexported rotated texture coords"
        Assert-Condition ((Get-JsonValue $summary.exported_texture_stage_counts "stage_count_0") -eq 175) "expected 175 exported zero-stage materials"
        Assert-Condition ((Get-JsonValue $summary.exported_texture_stage_counts "stage_count_1") -eq 9094) "expected 9094 exported single-stage materials"
        Assert-Condition ((Get-JsonValue $summary.exported_texture_stage_counts "stage_count_2") -eq 1904) "expected 1904 exported two-stage materials"
        Assert-Condition ((Get-JsonValue $summary.issue_counts "secondary_texture_coord_transform_not_exported") -eq 0) "expected 0 secondary texture coord transforms not exported"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "scale") -eq 232) "expected 232 scale-only secondary texture coord transforms"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "scale_translation") -eq 231) "expected 231 scale+translation secondary texture coord transforms"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "translation") -eq 147) "expected 147 translation-only secondary texture coord transforms"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "rotation") -eq 4) "expected 4 rotation-only secondary texture coord transforms"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "rotation_translation") -eq 5) "expected 5 rotation+translation secondary texture coord transforms"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "scale_rotation") -eq 7) "expected 7 scale+rotation secondary texture coord transforms"
        Assert-Condition ((Get-JsonValue $summary.secondary_texture_coord_transform_kind_counts "scale_rotation_translation") -eq 0) "expected 0 scale+rotation+translation secondary texture coord transforms"
        Assert-Condition ($summary.secondary_texture_coord_transform_sample_count -eq 200) "expected 200 secondary texture coord transform samples"
    }
}

Write-Host "OOT3D material stage inventory: $inventoryOutput"
Write-Host "OOT3D material stage inventory summary: $summaryOutput"
