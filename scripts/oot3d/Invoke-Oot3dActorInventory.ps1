param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
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
        throw "OOT3D actor inventory verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "actor_inventory"
$inventoryOutput = Join-Path $outputRoot "oot3d_actor_inventory.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_inventory_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $inventoryArgs = @(
        "inventory-actors",
        $ActorRoot,
        "--output",
        $inventoryOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
    if ($NoRecords) {
        $inventoryArgs += "--no-records"
    }
    Invoke-Oot3dTool -Arguments $inventoryArgs
}
finally {
    Pop-Location
}

$inventory = Get-Content -LiteralPath $inventoryOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    inventory = $inventoryOutput
    file_count = $inventory.file_count
    archive_count = $inventory.archive_count
    loose_cmb_count = $inventory.loose_cmb_count
    embedded_file_count = $inventory.embedded_file_count
    embedded_type_counts = $inventory.embedded_type_counts
    animation_file_counts = $inventory.animation_file_counts
    animation_like_file_counts = $inventory.animation_like_file_counts
    archive_counts = $inventory.archive_counts
    archive_support_counts = $inventory.archive_support_counts
    cmb_counts = $inventory.cmb_counts
    model_support_counts = $inventory.model_support_counts
    skinning_mode_primitive_counts = $inventory.skinning_mode_primitive_counts
    primitive_bone_count_counts = $inventory.primitive_bone_count_counts
    skinning_mode_bone_count_counts = $inventory.skinning_mode_bone_count_counts
    bone_count_counts = $inventory.bone_count_counts
    skeleton_metadata_counts = $inventory.skeleton_metadata_counts
    animation_payload_magic_counts = $inventory.animation_payload_magic_counts
    animation_payload_declared_size_match_counts = $inventory.animation_payload_declared_size_match_counts
    animation_payload_header_version_counts = $inventory.animation_payload_header_version_counts
    animation_payload_header_field_counts = $inventory.animation_payload_header_field_counts
    animation_payload_archive_support_counts = $inventory.animation_payload_archive_support_counts
    animation_payload_size_summary = $inventory.animation_payload_size_summary
    animation_payload_sample_count = $inventory.animation_payload_samples.Count
    csab_node_table_counts = $inventory.csab_node_table_counts
    csab_anod_record_counts = $inventory.csab_anod_record_counts
    csab_anod_active_channel_count_counts = $inventory.csab_anod_active_channel_count_counts
    csab_anod_channel_slot_active_counts = $inventory.csab_anod_channel_slot_active_counts
    csab_anod_record_field_04_high16_counts = $inventory.csab_anod_record_field_04_high16_counts
    csab_anod_channel_block_counts = $inventory.csab_anod_channel_block_counts
    csab_anod_channel_block_class_counts = $inventory.csab_anod_channel_block_class_counts
    csab_anod_channel_block_class_key_entry_counts = $inventory.csab_anod_channel_block_class_key_entry_counts
    csab_anod_channel_block_key_count_counts = $inventory.csab_anod_channel_block_key_count_counts
    csab_anod_channel_block_slot_class_counts = $inventory.csab_anod_channel_block_slot_class_counts
    csab_skeleton_match_counts = $inventory.csab_skeleton_match_counts
    csab_target_resolution_counts = $inventory.csab_target_resolution_counts
    csab_resolved_target_support_counts = $inventory.csab_resolved_target_support_counts
    csab_playback_blocker_counts = $inventory.csab_playback_blocker_counts
    csab_playback_candidate_node_table_counts = $inventory.csab_playback_candidate_node_table_counts
    csab_playback_candidate_anod_record_counts = $inventory.csab_playback_candidate_anod_record_counts
    csab_playback_candidate_anod_active_channel_count_counts = $inventory.csab_playback_candidate_anod_active_channel_count_counts
    csab_playback_candidate_anod_channel_slot_active_counts = $inventory.csab_playback_candidate_anod_channel_slot_active_counts
    csab_playback_candidate_anod_record_field_04_high16_counts = $inventory.csab_playback_candidate_anod_record_field_04_high16_counts
    csab_playback_candidate_anod_channel_block_counts = $inventory.csab_playback_candidate_anod_channel_block_counts
    csab_playback_candidate_anod_channel_block_class_counts = $inventory.csab_playback_candidate_anod_channel_block_class_counts
    csab_playback_candidate_anod_channel_block_class_key_entry_counts = $inventory.csab_playback_candidate_anod_channel_block_class_key_entry_counts
    csab_playback_candidate_anod_channel_block_key_count_counts = $inventory.csab_playback_candidate_anod_channel_block_key_count_counts
    csab_playback_candidate_anod_channel_block_slot_class_counts = $inventory.csab_playback_candidate_anod_channel_block_slot_class_counts
    csab_playback_candidate_f32_sampler_counts = $inventory.csab_playback_candidate_f32_sampler_counts
    csab_playback_candidate_f32_sampler_frame_count_counts = $inventory.csab_playback_candidate_f32_sampler_frame_count_counts
    csab_playback_candidate_f32_sampler_slot_sample_counts = $inventory.csab_playback_candidate_f32_sampler_slot_sample_counts
    csab_playback_candidate_pose_sample_counts = $inventory.csab_playback_candidate_pose_sample_counts
    csab_playback_candidate_pose_sample_slot_counts = $inventory.csab_playback_candidate_pose_sample_slot_counts
    csab_playback_candidate_full_pose_sample_counts = $inventory.csab_playback_candidate_full_pose_sample_counts
    csab_playback_candidate_full_pose_sample_slot_counts = $inventory.csab_playback_candidate_full_pose_sample_slot_counts
    csab_skeleton_match_sample_count = $inventory.csab_skeleton_match_samples.Count
    csab_playback_candidate_sample_count = $inventory.csab_playback_candidate_samples.Count
    nonstatic_model_sample_count = $inventory.nonstatic_model_samples.Count
    parse_error_count = $inventory.parse_error_count
}

$summary | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $summaryOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.file_count -gt 0) "actor directory contains no files"
    Assert-Condition ($summary.archive_count -gt 0) "actor inventory found no ZAR archives"
    Assert-Condition ($summary.cmb_counts.discovered -gt 0) "actor inventory discovered no CMB models"
    Assert-Condition (
        $summary.cmb_counts.parsed -eq ($summary.cmb_counts.static_candidate + $summary.cmb_counts.nonstatic_candidate)
    ) "parsed CMB count does not match static + nonstatic counts"
    Assert-Condition ($summary.nonstatic_model_sample_count -le $SampleLimit) "nonstatic sample output exceeded SampleLimit"
    Assert-Condition ($summary.animation_payload_sample_count -le $SampleLimit) "animation payload sample output exceeded SampleLimit"
    Assert-Condition ($summary.csab_skeleton_match_sample_count -le $SampleLimit) "CSAB skeleton match sample output exceeded SampleLimit"
    Assert-Condition ($summary.csab_playback_candidate_sample_count -le $SampleLimit) "CSAB playback candidate sample output exceeded SampleLimit"

    Assert-Condition ($summary.file_count -eq 349) "expected 349 actor files"
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.loose_cmb_count -eq 1) "expected 1 loose actor CMB"
    Assert-Condition ($summary.embedded_file_count -eq 7121) "expected 7121 files embedded in actor ZAR archives"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "cmb") -eq 1309) "expected 1309 embedded CMB files"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "csab") -eq 2465) "expected 2465 embedded CSAB files"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "cmab") -eq 474) "expected 474 embedded CMAB files"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "anb") -eq 1122) "expected 1122 embedded ANB files"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "faceb") -eq 1185) "expected 1185 embedded FACEB files"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "zsi") -eq 227) "expected 227 embedded ZSI files"
    Assert-Condition ((Get-JsonValue $summary.embedded_type_counts "ctxb") -eq 277) "expected 277 embedded CTXB files"
    Assert-Condition ((Get-JsonValue $summary.animation_file_counts "csab") -eq 2465) "expected 2465 CSAB animation files"
    Assert-Condition ((Get-JsonValue $summary.animation_file_counts "cmab") -eq 474) "expected 474 CMAB animation files"
    Assert-Condition ((Get-JsonValue $summary.animation_like_file_counts "anb") -eq 1122) "expected 1122 ANB animation-like files"
    Assert-Condition ((Get-JsonValue $summary.animation_like_file_counts "faceb") -eq 1185) "expected 1185 FACEB animation-like files"
    Assert-Condition ($summary.archive_counts.with_cmb -eq 346) "expected 346 actor archives with CMB files"
    Assert-Condition ($summary.archive_counts.without_cmb -eq 2) "expected 2 actor archives without CMB files"
    Assert-Condition ($summary.archive_counts.with_known_animation -eq 202) "expected 202 actor archives with CSAB/CMAB files"
    Assert-Condition ($summary.archive_counts.with_animation_like_data -eq 204) "expected 204 actor archives with animation-like data"
    Assert-Condition ($summary.archive_counts.static_payload_only -eq 141) "expected 141 actor archives with only static payload file types"
    Assert-Condition ((Get-JsonValue $summary.archive_support_counts "static_payload_supported_by_current_exporter") -eq 104) "expected 104 actor archives supported by the current static exporter"
    Assert-Condition ((Get-JsonValue $summary.archive_support_counts "static_models_with_animation_data") -eq 36) "expected 36 static actor archives with CSAB/CMAB animation data"
    Assert-Condition ((Get-JsonValue $summary.archive_support_counts "needs_skeleton_or_skinning_support") -eq 40) "expected 40 actor archives needing skeleton or skinning support"
    Assert-Condition ((Get-JsonValue $summary.archive_support_counts "needs_skeleton_skinning_and_animation_support") -eq 166) "expected 166 actor archives needing skeleton, skinning, and animation support"
    Assert-Condition ((Get-JsonValue $summary.archive_support_counts "no_parsed_cmb_model") -eq 2) "expected 2 actor archives with no parsed CMB model"
    Assert-Condition ($summary.cmb_counts.discovered -eq 1310) "expected 1310 actor CMB models discovered"
    Assert-Condition ($summary.cmb_counts.parsed -eq 1310) "expected 1310 actor CMB models parsed"
    Assert-Condition ($summary.cmb_counts.static_candidate -eq 974) "expected 974 actor static CMB candidates"
    Assert-Condition ($summary.cmb_counts.nonstatic_candidate -eq 336) "expected 336 actor non-static CMB candidates"
    Assert-Condition ($summary.cmb_counts.parse_errors -eq 0) "expected 0 known actor CMB parse errors"
    Assert-Condition ((Get-JsonValue $summary.model_support_counts "static_cmb_export_supported") -eq 974) "expected 974 static one-bone actor CMBs supported by the rigid exporter"
    Assert-Condition ((Get-JsonValue $summary.model_support_counts "rigid_multibone_export_supported") -eq 134) "expected 134 rigid multi-bone actor CMBs supported by the rigid exporter"
    Assert-Condition ((Get-JsonValue $summary.model_support_counts "needs_skinning_mode_1_support") -eq 13) "expected 13 actor CMBs needing skinning mode 1"
    Assert-Condition ((Get-JsonValue $summary.model_support_counts "needs_skinning_mode_2_support") -eq 140) "expected 140 actor CMBs needing skinning mode 2"
    Assert-Condition ((Get-JsonValue $summary.model_support_counts "needs_skinning_mode_1_and_2_support") -eq 49) "expected 49 actor CMBs needing skinning modes 1 and 2"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_primitive_counts "0") -eq 3535) "expected 3535 rigid actor primitives"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_primitive_counts "1") -eq 97) "expected 97 actor primitives using skinning mode 1"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_primitive_counts "2") -eq 1000) "expected 1000 actor primitives using skinning mode 2"
    Assert-Condition ((Get-JsonValue $summary.primitive_bone_count_counts "1") -eq 3535) "expected 3535 actor primitives referencing 1 bone"
    Assert-Condition ((Get-JsonValue $summary.primitive_bone_count_counts "2") -eq 294) "expected 294 actor primitives referencing 2 bones"
    Assert-Condition ((Get-JsonValue $summary.primitive_bone_count_counts "10") -eq 133) "expected 133 actor primitives referencing 10 bones"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=0;bones=1") -eq 3535) "expected all rigid actor primitives to reference 1 bone"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=1;bones=2") -eq 56) "expected 56 mode-1 actor primitives referencing 2 bones"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=2;bones=2") -eq 238) "expected 238 mode-2 actor primitives referencing 2 bones"
    Assert-Condition ((Get-JsonValue $summary.bone_count_counts "1") -eq 974) "expected 974 parsed actor CMBs with one skeleton bone"
    Assert-Condition ((Get-JsonValue $summary.bone_count_counts "48") -eq 10) "expected 10 parsed actor CMBs with 48 skeleton bones"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.chunk_size_delta_counts "0") -eq 1310) "expected all parsed actor SKL chunk sizes to match 0x10 + bones * 0x28"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.header_word_0c_counts "0") -eq 235) "expected 235 parsed actor SKL headers with word 0"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.header_word_0c_counts "2") -eq 1075) "expected 1075 parsed actor SKL headers with word 2"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.root_count_counts "1") -eq 1310) "expected every parsed actor skeleton to have one root"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.max_depth_counts "0") -eq 974) "expected 974 static one-bone actor skeletons at depth 0"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.max_depth_counts "40") -eq 1) "expected one parsed actor skeleton with depth 40"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.bone_index_mismatch_count_counts "0") -eq 1310) "expected all parsed actor skeletons to have sequential bone indices"
    Assert-Condition ((Get-JsonValue $summary.skeleton_metadata_counts.parent_out_of_range_count_counts "0") -eq 1310) "expected all parsed actor skeleton parent indices to stay in range"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_magic_counts.csab "ascii:csab") -eq 2465) "expected 2465 CSAB payloads with csab magic"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_magic_counts.cmab "ascii:cmab") -eq 474) "expected 474 CMAB payloads with cmab magic"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_magic_counts.faceb "hex:666b6201") -eq 1185) "expected 1185 FACEB payloads with fkb01 magic"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_declared_size_match_counts.csab "matches") -eq 2465) "expected all CSAB declared sizes to match embedded size"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_declared_size_match_counts.cmab "matches") -eq 474) "expected all CMAB declared sizes to match embedded size"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_declared_size_match_counts.anb "not_applicable") -eq 1122) "expected ANB size check to remain not applicable"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_declared_size_match_counts.faceb "not_applicable") -eq 1185) "expected FACEB size check to remain not applicable"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_version_counts.csab "3") -eq 2465) "expected all CSAB payloads to report header version 3"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_version_counts.cmab "1") -eq 474) "expected all CMAB payloads to report header version 1"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.csab.word_0c "0") -eq 2465) "expected all CSAB payloads to have header word_0c 0"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.csab.word_10 "1") -eq 2465) "expected all CSAB payloads to have header word_10 1"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.csab.word_14 "24") -eq 2465) "expected all CSAB payloads to have header word_14 24"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.cmab.word_0c "0") -eq 474) "expected all CMAB payloads to have header word_0c 0"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.cmab.word_10 "1") -eq 474) "expected all CMAB payloads to have header word_10 1"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.cmab.word_14 "32") -eq 474) "expected all CMAB payloads to have header word_14 32"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.cmab.word_20 "4294967295") -eq 471) "expected 471 CMAB payloads with word_20 sentinel -1"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_header_field_counts.cmab.word_20 "0") -eq 3) "expected 3 CMAB payloads with word_20 0"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_archive_support_counts.csab "needs_skeleton_skinning_and_animation_support") -eq 2454) "expected 2454 CSAB payloads in archives needing skeleton, skinning, and animation support"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_archive_support_counts.csab "static_models_with_animation_data") -eq 11) "expected 11 CSAB payloads in static-model animation archives"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_archive_support_counts.cmab "needs_skeleton_skinning_and_animation_support") -eq 327) "expected 327 CMAB payloads in archives needing skeleton, skinning, and animation support"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_archive_support_counts.cmab "static_models_with_animation_data") -eq 147) "expected 147 CMAB payloads in static-model animation archives"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_archive_support_counts.anb "needs_skeleton_or_skinning_support") -eq 1122) "expected 1122 ANB payloads in archives needing skeleton or skinning support"
    Assert-Condition ((Get-JsonValue $summary.animation_payload_archive_support_counts.faceb "needs_skeleton_skinning_and_animation_support") -eq 1185) "expected 1185 FACEB payloads in archives needing skeleton, skinning, and animation support"
    Assert-Condition ((Get-JsonValue $summary.csab_node_table_counts "valid_node_tables") -eq 2465) "expected all 2465 CSAB payloads to have valid bone/node tables"
    Assert-Condition ((Get-JsonValue $summary.csab_node_table_counts "invalid_node_tables") -eq 0) "expected 0 invalid CSAB bone/node tables"
    Assert-Condition ((Get-JsonValue $summary.csab_node_table_counts "skeleton_bone_table_entries") -eq 55070) "expected 55070 CSAB skeleton bone-table entries"
    Assert-Condition ((Get-JsonValue $summary.csab_node_table_counts "animated_node_offsets") -eq 48780) "expected 48780 CSAB animated node offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_node_table_counts "unused_skeleton_bone_entries") -eq 6290) "expected 6290 CSAB unused skeleton bone entries"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "valid_record_sets") -eq 2465) "expected all 2465 CSAB payloads to have valid anod record sets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "invalid_record_sets") -eq 0) "expected 0 invalid CSAB anod record sets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "anod_records") -eq 48780) "expected 48780 CSAB anod records"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "channel_offset_entries") -eq 487800) "expected 487800 CSAB anod channel-offset entries"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "active_channel_offsets") -eq 163799) "expected 163799 active CSAB anod channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "zero_channel_offsets") -eq 324001) "expected 324001 zero CSAB anod channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "records_without_active_channel_offsets") -eq 323) "expected 323 CSAB anod records without active channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "payloads_record_low16_bone_indices_match_bone_table") -eq 2465) "expected every CSAB anod record set low16 bone index to match the bone-to-node table"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "payloads_record_field_04_high16_values_are_0_or_1") -eq 2465) "expected every CSAB anod field_04 high16 value to be 0 or 1"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_counts "payloads_channel_offsets_valid") -eq 2465) "expected every CSAB anod record set to have valid channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_active_channel_count_counts "0") -eq 323) "expected 323 CSAB anod records with 0 active channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_active_channel_count_counts "9") -eq 294) "expected 294 CSAB anod records with 9 active channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_field_04_high16_counts "0") -eq 23910) "expected 23910 CSAB anod records with field_04 high16 0"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_record_field_04_high16_counts "1") -eq 24870) "expected 24870 CSAB anod records with field_04 high16 1"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "valid_block_sets") -eq 2465) "expected all 2465 CSAB payloads to have valid anod channel block sets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "invalid_block_sets") -eq 0) "expected 0 invalid CSAB anod channel block sets"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "channel_blocks") -eq 163799) "expected 163799 CSAB anod channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "channel_block_key_entries") -eq 1658761) "expected 1658761 CSAB anod channel block key entries"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "payloads_block_classes_valid") -eq 2465) "expected every CSAB anod channel block set to classify into a known block class"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "payloads_type2_frame_headers_match_csab") -eq 2465) "expected every CSAB anod type-2 channel header to match the CSAB frame-count candidate"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "payloads_type2_key_frames_valid") -eq 2465) "expected every CSAB anod type-2 channel block to have valid key frames"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_counts "payloads_f32_values_finite") -eq 2465) "expected every CSAB anod f32 channel value to be finite"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_class_counts "type1_f32_const_len24") -eq 34966) "expected 34966 CSAB anod type1 f32 constant blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_class_counts "type1_s16_const_len20") -eq 2951) "expected 2951 CSAB anod type1 s16 constant blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_class_counts "type2_f32_keys_len16_plus_count16") -eq 63417) "expected 63417 CSAB anod type2 f32 key blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_class_counts "type2_s16_keys_len16_plus_count8") -eq 62465) "expected 62465 CSAB anod type2 s16 key blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_class_key_entry_counts "type2_f32_keys_len16_plus_count16") -eq 793124) "expected 793124 CSAB anod type2 f32 key entries"
    Assert-Condition ((Get-JsonValue $summary.csab_anod_channel_block_class_key_entry_counts "type2_s16_keys_len16_plus_count8") -eq 827720) "expected 827720 CSAB anod type2 s16 key entries"
    Assert-Condition ((Get-JsonValue $summary.csab_skeleton_match_counts "matches_single_cmb_bone_count") -eq 2275) "expected 2275 CSAB payloads to match a single CMB skeleton bone count in the same archive"
    Assert-Condition ((Get-JsonValue $summary.csab_skeleton_match_counts "matches_multiple_cmb_bone_counts") -eq 179) "expected 179 CSAB payloads to match multiple CMB skeleton bone counts in the same archive"
    Assert-Condition ((Get-JsonValue $summary.csab_skeleton_match_counts "no_matching_cmb_bone_count") -eq 11) "expected 11 CSAB payloads with no same-archive CMB skeleton bone-count match"
    Assert-Condition ((Get-JsonValue $summary.csab_skeleton_match_counts "no_parsed_cmb_model") -eq 0) "expected 0 CSAB payloads without a parsed CMB model in the same archive"
    Assert-Condition ((Get-JsonValue $summary.csab_skeleton_match_counts "animated_bone_count_lte_skeleton_candidate") -eq 2465) "expected every CSAB animated bone-count candidate to stay within the skeleton bone-count candidate"
    Assert-Condition ((Get-JsonValue $summary.csab_skeleton_match_counts "animated_bone_count_gt_skeleton_candidate") -eq 0) "expected no CSAB animated bone-count candidate to exceed the skeleton bone-count candidate"
    Assert-Condition ((Get-JsonValue $summary.csab_target_resolution_counts "single_bone_count_match") -eq 2275) "expected 2275 CSAB target candidates resolved by a single same-archive CMB bone-count match"
    Assert-Condition ((Get-JsonValue $summary.csab_target_resolution_counts "multiple_bone_count_exact_name_match") -eq 10) "expected 10 ambiguous CSAB target candidates with one exact CMB stem-name match"
    Assert-Condition ((Get-JsonValue $summary.csab_target_resolution_counts "multiple_bone_count_contained_name_match") -eq 30) "expected 30 ambiguous CSAB target candidates with one contained CMB stem-name match"
    Assert-Condition ((Get-JsonValue $summary.csab_target_resolution_counts "multiple_bone_count_unresolved") -eq 139) "expected 139 ambiguous CSAB target candidates to remain unresolved by conservative name checks"
    Assert-Condition ((Get-JsonValue $summary.csab_target_resolution_counts "no_matching_cmb_bone_count") -eq 11) "expected 11 CSAB target candidates with no same-archive CMB bone-count match"
    Assert-Condition ((Get-JsonValue $summary.csab_resolved_target_support_counts "static_cmb_export_supported") -eq 7) "expected 7 resolved CSAB targets supported by the static CMB exporter"
    Assert-Condition ((Get-JsonValue $summary.csab_resolved_target_support_counts "rigid_multibone_export_supported") -eq 37) "expected 37 resolved CSAB targets supported by the rigid multi-bone exporter"
    Assert-Condition ((Get-JsonValue $summary.csab_resolved_target_support_counts "needs_skinning_mode_1_support") -eq 46) "expected 46 resolved CSAB targets needing skinning mode 1 support"
    Assert-Condition ((Get-JsonValue $summary.csab_resolved_target_support_counts "needs_skinning_mode_2_support") -eq 1247) "expected 1247 resolved CSAB targets needing skinning mode 2 support"
    Assert-Condition ((Get-JsonValue $summary.csab_resolved_target_support_counts "needs_skinning_mode_1_and_2_support") -eq 978) "expected 978 resolved CSAB targets needing skinning modes 1 and 2"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_blocker_counts "target_export_supported_by_current_rigid_path") -eq 44) "expected 44 CSAB targets already supported by the current rigid export path"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_blocker_counts "target_needs_skinning_support") -eq 2271) "expected 2271 resolved CSAB targets to remain blocked by skinning support"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_blocker_counts "target_unresolved_or_missing") -eq 150) "expected 150 CSAB targets to remain unresolved or missing"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_blocker_counts "target_needs_unknown_support") -eq 0) "expected 0 CSAB targets to need unknown support"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_node_table_counts "valid_node_tables") -eq 44) "expected all 44 CSAB playback candidates to have valid bone/node tables"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_node_table_counts "invalid_node_tables") -eq 0) "expected 0 invalid CSAB playback candidate bone/node tables"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_node_table_counts "skeleton_bone_table_entries") -eq 275) "expected 275 CSAB playback candidate skeleton bone-table entries"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_node_table_counts "animated_node_offsets") -eq 237) "expected 237 CSAB playback candidate animated node offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_node_table_counts "unused_skeleton_bone_entries") -eq 38) "expected 38 CSAB playback candidate unused skeleton bone entries"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "valid_record_sets") -eq 44) "expected all 44 CSAB playback candidates to have valid anod record sets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "invalid_record_sets") -eq 0) "expected 0 invalid CSAB playback candidate anod record sets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "anod_records") -eq 237) "expected 237 CSAB playback candidate anod records"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "active_channel_offsets") -eq 1311) "expected 1311 active CSAB playback candidate anod channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "zero_channel_offsets") -eq 1059) "expected 1059 zero CSAB playback candidate anod channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "records_without_active_channel_offsets") -eq 8) "expected 8 CSAB playback candidate anod records without active channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "payloads_record_low16_bone_indices_match_bone_table") -eq 44) "expected every CSAB playback candidate anod low16 bone index to match the bone-to-node table"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_counts "payloads_channel_offsets_valid") -eq 44) "expected every CSAB playback candidate anod record set to have valid channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_active_channel_count_counts "0") -eq 8) "expected 8 CSAB playback candidate anod records with 0 active channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_active_channel_count_counts "9") -eq 38) "expected 38 CSAB playback candidate anod records with 9 active channel offsets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_field_04_high16_counts "0") -eq 237) "expected every CSAB playback candidate anod field_04 high16 value to be 0"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_record_field_04_high16_counts "1") -eq 0) "expected no CSAB playback candidate anod field_04 high16 value to be 1"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_counts "valid_block_sets") -eq 44) "expected all 44 CSAB playback candidates to have valid anod channel block sets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_counts "invalid_block_sets") -eq 0) "expected 0 invalid CSAB playback candidate anod channel block sets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_counts "channel_blocks") -eq 1311) "expected 1311 CSAB playback candidate anod channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_counts "channel_block_key_entries") -eq 4048) "expected 4048 CSAB playback candidate anod channel block key entries"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_counts "payloads_type2_key_frames_valid") -eq 44) "expected every CSAB playback candidate type-2 block to have valid key frames"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_class_counts "type1_f32_const_len24") -eq 1126) "expected 1126 CSAB playback candidate type1 f32 constant blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_class_counts "type2_f32_keys_len16_plus_count16") -eq 185) "expected 185 CSAB playback candidate type2 f32 key blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_class_counts "type1_s16_const_len20") -eq 0) "expected 0 CSAB playback candidate type1 s16 constant blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_class_counts "type2_s16_keys_len16_plus_count8") -eq 0) "expected 0 CSAB playback candidate type2 s16 key blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_anod_channel_block_class_key_entry_counts "type2_f32_keys_len16_plus_count16") -eq 2922) "expected 2922 CSAB playback candidate type2 f32 key entries"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "valid_sample_sets") -eq 44) "expected all 44 CSAB playback candidates to pass f32 integer-frame sampling"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "invalid_sample_sets") -eq 0) "expected 0 invalid CSAB playback candidate f32 sample sets"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "sampled_payload_frame_slots") -eq 3714) "expected 3714 CSAB playback candidate sampled frame slots"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "sampled_channel_blocks") -eq 1311) "expected 1311 CSAB playback candidate sampled channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "sampled_channel_values") -eq 158688) "expected 158688 CSAB playback candidate sampled channel values"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "finite_sampled_channel_values") -eq 158688) "expected every CSAB playback candidate sampled channel value to be finite"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "const_f32_channel_blocks") -eq 1126) "expected 1126 CSAB playback candidate sampled const f32 channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "keyed_f32_channel_blocks") -eq 185) "expected 185 CSAB playback candidate sampled keyed f32 channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_counts "non_f32_channel_blocks") -eq 0) "expected 0 CSAB playback candidate non-f32 sampled channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_frame_count_counts "85") -eq 5) "expected 5 CSAB playback candidate samples with frame count 85"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_frame_count_counts "88") -eq 5) "expected 5 CSAB playback candidate samples with frame count 88"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_slot_sample_counts "0") -eq 23339) "expected 23339 sampled CSAB playback values for slot 0"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_f32_sampler_slot_sample_counts "8") -eq 9782) "expected 9782 sampled CSAB playback values for slot 8"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "valid_pose_sample_sets") -eq 44) "expected all 44 CSAB playback candidates to pass endpoint pose sampling"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "invalid_pose_sample_sets") -eq 0) "expected 0 invalid CSAB playback candidate endpoint pose samples"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "sampled_pose_frames") -eq 88) "expected 88 CSAB playback candidate endpoint pose frames"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "sampled_skeleton_bone_transforms") -eq 550) "expected 550 CSAB playback candidate sampled skeleton bone transforms"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "sampled_animated_bone_transforms") -eq 474) "expected 474 CSAB playback candidate sampled animated bone transforms"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "sampled_channel_values") -eq 2622) "expected 2622 CSAB playback candidate endpoint pose channel values"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "finite_channel_values") -eq 2622) "expected every CSAB playback candidate endpoint pose channel value to be finite"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "world_matrix_entries") -eq 8800) "expected 8800 CSAB playback candidate endpoint pose world matrix entries"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "finite_world_matrix_entries") -eq 8800) "expected every CSAB playback candidate endpoint pose world matrix entry to be finite"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_counts "non_f32_channel_blocks") -eq 0) "expected 0 CSAB playback candidate endpoint pose non-f32 channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_slot_counts "0") -eq 418) "expected 418 endpoint pose channel values for slot 0"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_pose_sample_slot_counts "8") -eq 76) "expected 76 endpoint pose channel values for slot 8"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "valid_pose_sample_sets") -eq 44) "expected all 44 CSAB playback candidates to pass full-frame pose sampling"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "invalid_pose_sample_sets") -eq 0) "expected 0 invalid CSAB playback candidate full-frame pose samples"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "sampled_pose_frames") -eq 3714) "expected 3714 CSAB playback candidate full-frame pose frames"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "sampled_skeleton_bone_transforms") -eq 29646) "expected 29646 CSAB playback candidate full-frame skeleton bone transforms"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "sampled_animated_bone_transforms") -eq 24631) "expected 24631 CSAB playback candidate full-frame animated bone transforms"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "sampled_channel_values") -eq 158688) "expected 158688 CSAB playback candidate full-frame pose channel values"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "finite_channel_values") -eq 158688) "expected every CSAB playback candidate full-frame pose channel value to be finite"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "world_matrix_entries") -eq 474336) "expected 474336 CSAB playback candidate full-frame world matrix entries"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "finite_world_matrix_entries") -eq 474336) "expected every CSAB playback candidate full-frame world matrix entry to be finite"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_counts "non_f32_channel_blocks") -eq 0) "expected 0 CSAB playback candidate full-frame non-f32 channel blocks"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_slot_counts "0") -eq 23339) "expected 23339 full-frame pose channel values for slot 0"
    Assert-Condition ((Get-JsonValue $summary.csab_playback_candidate_full_pose_sample_slot_counts "8") -eq 9782) "expected 9782 full-frame pose channel values for slot 8"
    Assert-Condition ($summary.animation_payload_size_summary.csab.min -eq 116) "expected CSAB min size 116"
    Assert-Condition ($summary.animation_payload_size_summary.csab.max -eq 120596) "expected CSAB max size 120596"
    Assert-Condition ($summary.animation_payload_size_summary.csab.unique_size_count -eq 1496) "expected 1496 distinct CSAB sizes"
    Assert-Condition ($summary.animation_payload_size_summary.cmab.min -eq 128) "expected CMAB min size 128"
    Assert-Condition ($summary.animation_payload_size_summary.cmab.max -eq 301552) "expected CMAB max size 301552"
    Assert-Condition ($summary.animation_payload_size_summary.anb.count -eq 1122) "expected 1122 ANB payloads"
    Assert-Condition ($summary.animation_payload_size_summary.faceb.max -eq 124) "expected FACEB max size 124"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 known actor inventory parse errors"
}

Write-Host "OOT3D actor inventory: $inventoryOutput"
Write-Host "OOT3D actor inventory summary: $summaryOutput"
