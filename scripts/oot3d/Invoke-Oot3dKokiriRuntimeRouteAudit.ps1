param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ActorObjectSemantics = "",
    [int]$SampleLimit = 100,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($ActorObjectSemantics)) {
    $ActorObjectSemantics = Join-Path $repoRoot "tools\oot3d\decomp_support\include\oot3d\actor_object_semantics.h"
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
        throw "OOT3D Kokiri runtime route audit verification failed: $Message"
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
Require-Path $RomFs "OOT3D RomFS"
Require-Path (Join-Path $RomFs "scene") "OOT3D scene directory"
Require-Path $ActorObjectSemantics "Actor/object semantics header"

$outputRoot = Join-Path $WorkRoot "kokiri_runtime_route"
$auditOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_route_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_route_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-kokiri-runtime-route",
        $RomFs,
        "--output",
        $auditOutput,
        "--actor-object-semantics",
        $ActorObjectSemantics,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $auditOutput "Kokiri runtime route audit"
$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    romfs = (Resolve-Path $RomFs).Path
    actor_object_semantics = (Resolve-Path $ActorObjectSemantics).Path
    audit = $auditOutput
    route_stems = $audit.route_stems
    zsi_file_count = $audit.zsi_file_count
    scene_file_count = $audit.scene_file_count
    room_file_count = $audit.room_file_count
    setup_total = $audit.setup_total
    command_total = $audit.command_total
    embedded_cmb_total = $audit.embedded_cmb_total
    collision_candidate_total = $audit.collision_candidate_total
    collision_vertex_total = $audit.collision_vertex_total
    collision_effective_polygon_total = $audit.collision_effective_polygon_total
    collision_surface_type_total = $audit.collision_surface_type_total
    bgcam_total = $audit.bgcam_total
    water_box_total = $audit.water_box_total
    room_reference_count = $audit.room_reference_count
    unique_room_references = $audit.unique_room_references
    spawn_list_command_count = $audit.spawn_list_command_count
    spawn_candidate_command_count = $audit.spawn_candidate_command_count
    spawn_entry_candidate_total = $audit.spawn_entry_candidate_total
    spawn_player_candidate_total = $audit.spawn_player_candidate_total
    spawn_payload_profile_counts = $audit.spawn_payload_profile_counts
    entrance_entry_candidate_total = $audit.entrance_entry_candidate_total
    path_block_candidate_count = $audit.path_block_candidate_count
    path_file_offset_candidate_total = $audit.path_file_offset_candidate_total
    exit_value_candidate_total = $audit.exit_value_candidate_total
    layout_validation_counts = $audit.layout_validation_counts
    standard_actor_list_command_count = $audit.standard_actor_list_command_count
    object_list_command_count = $audit.object_list_command_count
    room_actor_payload_status_counts = $audit.room_actor_payload_status_counts
    room_actor_list_candidate_count = $audit.room_actor_list_candidate_count
    selected_room_actor_list_count = $audit.selected_room_actor_list_count
    selected_room_actor_entry_total = $audit.selected_room_actor_entry_total
    selected_room_actor_name_counts = $audit.selected_room_actor_name_counts
    selected_room_actor_object_name_counts = $audit.selected_room_actor_object_name_counts
    actor_object_binding_status = $audit.actor_object_binding_summary.status
    actor_object_list_dependency_status = $audit.actor_object_binding_summary.object_list_dependency_status
    unresolved_transition_actor_binding_count = $audit.actor_object_binding_summary.unresolved_transition_actor_binding_count
    transition_actor_required_object_counts = $audit.actor_object_binding_summary.transition_actor_required_object_counts
    special_keep_object_counts = $audit.actor_object_binding_summary.special_keep_object_counts
    player_object_binding_status = $audit.actor_object_binding_summary.player_object_binding.status
    transition_actor_command_count = $audit.transition_actor_command_count
    transition_actor_count = $audit.transition_actor_count
    sound_setting_count = $audit.sound_setting_count
    audio_mapping_status = $audit.audio_runtime_mapping.status
    audio_mapping_profile_count = $audit.audio_runtime_mapping.profile_count
    audio_mapped_profile_count = $audit.audio_runtime_mapping.mapped_profile_count
    audio_unresolved_profile_count = $audit.audio_runtime_mapping.unresolved_profile_count
    audio_mapped_record_count = $audit.audio_runtime_mapping.mapped_record_count
    skybox_setting_count = $audit.skybox_setting_count
    cutscene_reference_count = $audit.cutscene_reference_count
    audio_file_count = $audit.audio_asset_summary.file_count
    kankyo_archive_count = $audit.kankyo_asset_summary.archive_count
    kankyo_runtime_selection_status = $audit.kankyo_runtime_selection.status
    kankyo_runtime_selection_profile_count = $audit.kankyo_runtime_selection.profile_count
    kankyo_unresolved_selection_count = $audit.kankyo_runtime_selection.unresolved_selection_count
    kankyo_runtime_selection_status_counts = $audit.kankyo_runtime_selection.status_counts
    runtime_gap_counts = $audit.runtime_gap_counts
    parse_error_count = $audit.parse_error_count
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($audit.format -eq "oot3d_kokiri_runtime_route_audit_v3") "unexpected audit format"
    Assert-Condition ($audit.missing_required_file_count -eq 0) "expected no missing required route scene files"
    Assert-Condition ($summary.zsi_file_count -eq 6) "expected 6 route ZSI files"
    Assert-Condition ($summary.scene_file_count -eq 2) "expected 2 route scene ZSI files"
    Assert-Condition ($summary.room_file_count -eq 4) "expected 4 route room ZSI files"
    Assert-Condition ($summary.setup_total -eq 17) "expected 17 scene setup lists"
    Assert-Condition ($summary.command_total -eq 219) "expected 219 scene setup commands"
    Assert-Condition ((Get-JsonValue $audit.command_id_counts "0x15") -eq 17) "expected 17 sound setting commands"
    Assert-Condition ((Get-JsonValue $audit.command_id_counts "0x04") -eq 17) "expected 17 room list commands"
    Assert-Condition ((Get-JsonValue $audit.command_id_counts "0x0e") -eq 13) "expected 13 transition actor list commands"
    Assert-Condition ($summary.embedded_cmb_total -eq 4) "expected 4 embedded route room CMBs"
    Assert-Condition ($summary.collision_candidate_total -eq 2) "expected 2 scene collision candidates"
    Assert-Condition ($summary.collision_vertex_total -eq 2450) "expected 2450 collision vertices"
    Assert-Condition ($summary.collision_effective_polygon_total -eq 4007) "expected 4007 effective collision polygons"
    Assert-Condition ($summary.collision_surface_type_total -eq 54) "expected 54 collision surface types"
    Assert-Condition ($summary.bgcam_total -eq 18) "expected 18 bg camera records"
    Assert-Condition ($summary.water_box_total -eq 1) "expected 1 water box"
    Assert-Condition ($summary.room_reference_count -eq 43) "expected 43 room reference string hits"
    Assert-Condition ($summary.unique_room_references.Count -eq 4) "expected 4 unique room references"
    Assert-Condition ($summary.unique_room_references -contains "link_0_info.zsi") "expected Link house room reference"
    Assert-Condition ($summary.unique_room_references -contains "spot04_2_info.zsi") "expected Kokiri room 2 reference"
    Assert-Condition ($summary.spawn_list_command_count -eq 17) "expected 17 spawn list commands"
    Assert-Condition ($summary.spawn_candidate_command_count -eq 17) "expected 17 decoded spawn candidate commands"
    Assert-Condition ($summary.spawn_entry_candidate_total -eq 50) "expected 50 OOT3D spawn entry candidates"
    Assert-Condition ($summary.spawn_player_candidate_total -eq 39) "expected 39 OOT3D player spawn candidates"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "validated_n64_player_start_list") -eq $null) "expected no unprefixed N64-compatible player start list profiles"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "validated_prefixed_player_start_list") -eq 17) "expected 17 prefixed player start windows with validated entrance indices"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "prefixed_player_start_window_missing_entrance_indices") -eq $null) "expected no remaining prefixed player start windows with missing entrance indices"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "entrance_overlap_actor_entry_candidate") -eq $null) "expected no remaining entrance-overlap actor-entry profiles"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "path_block_overlap_actor_like_candidate") -eq $null) "expected no remaining path-overlap actor-like profiles"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "entrance_overlap_mixed_actor_entry_candidate") -eq $null) "expected no remaining mixed entrance-overlap actor-entry profile"
    Assert-Condition ($summary.entrance_entry_candidate_total -eq 53) "expected 53 OOT3D entrance entry candidates"
    Assert-Condition ($summary.path_block_candidate_count -eq 6) "expected 6 OOT3D path block candidates"
    Assert-Condition ($summary.path_file_offset_candidate_total -eq 12) "expected 12 OOT3D path pointer-like file offsets"
    Assert-Condition ($summary.exit_value_candidate_total -eq 56) "expected 56 OOT3D exit value candidates after the OoT3D prefix"
    Assert-Condition ($summary.standard_actor_list_command_count -eq 0) "expected no standard actor list commands in current OOT3D route data"
    Assert-Condition ($summary.object_list_command_count -eq 0) "expected no standard object list commands in current OOT3D route data"
    Assert-Condition ((Get-JsonValue $summary.room_actor_payload_status_counts "object_prefixed_room_actor_list_identified") -eq 4) "expected 4 object-prefixed room actor payloads"
    Assert-Condition ($summary.room_actor_list_candidate_count -eq 18) "expected 18 room actor-list payload candidates"
    Assert-Condition ($summary.selected_room_actor_list_count -eq 4) "expected one selected room actor-list candidate per route room"
    Assert-Condition ($summary.selected_room_actor_entry_total -eq 104) "expected 104 selected room actor-entry candidates"
    Assert-Condition ((Get-JsonValue $summary.selected_room_actor_name_counts "ACTOR_EN_KO") -eq 8) "expected selected Kokiri Forest room actors to include 8 Kokiri NPC candidates"
    Assert-Condition ((Get-JsonValue $summary.selected_room_actor_object_name_counts "OBJECT_KANBAN") -eq 3) "expected selected room object prefixes to include 3 kanban object references"
    Assert-Condition ($summary.actor_object_binding_status -eq "route_actor_object_bindings_resolved") "expected resolved route actor/object bindings"
    Assert-Condition ($summary.actor_object_list_dependency_status -eq "covered_without_scene_object_list") "expected actor/object bindings to be covered without scene object list"
    Assert-Condition ($summary.unresolved_transition_actor_binding_count -eq 0) "expected no unresolved transition actor object bindings"
    Assert-Condition ((Get-JsonValue $summary.transition_actor_required_object_counts "OBJECT_GAMEPLAY_KEEP") -eq 16) "expected EN_HOLL to require OBJECT_GAMEPLAY_KEEP 16 times"
    Assert-Condition ((Get-JsonValue $summary.special_keep_object_counts "OBJECT_GAMEPLAY_DANGEON_KEEP") -eq 4) "expected 4 dungeon keep special file bindings"
    Assert-Condition ((Get-JsonValue $summary.special_keep_object_counts "OBJECT_GAMEPLAY_FIELD_KEEP") -eq 13) "expected 13 field keep special file bindings"
    Assert-Condition ($summary.player_object_binding_status -eq "runtime_binding_identified") "expected player object runtime binding to be identified"
    Assert-Condition ($summary.transition_actor_command_count -eq 13) "expected 13 transition actor commands"
    Assert-Condition ($summary.transition_actor_count -eq 29) "expected 29 decoded transition actor entries"
    Assert-Condition ((Get-JsonValue $audit.transition_actor_name_counts "ACTOR_EN_HOLL") -eq 16) "expected 16 EN_HOLL transition actor references"
    Assert-Condition ($summary.sound_setting_count -eq 17) "expected 17 sound setting records"
    Assert-Condition ($summary.audio_mapping_status -eq "mapped") "expected mapped route audio settings"
    Assert-Condition ($summary.audio_mapping_profile_count -eq 7) "expected 7 route audio profiles"
    Assert-Condition ($summary.audio_mapped_profile_count -eq 7) "expected all route audio profiles mapped"
    Assert-Condition ($summary.audio_unresolved_profile_count -eq 0) "expected no unresolved route audio profiles"
    Assert-Condition ($summary.audio_mapped_record_count -eq 17) "expected all 17 route sound settings mapped"
    Assert-Condition ($summary.skybox_setting_count -eq 17) "expected 17 skybox setting records"
    Assert-Condition ($summary.cutscene_reference_count -eq 13) "expected 13 cutscene references"
    Assert-Condition ($summary.audio_file_count -eq 3) "expected 3 OOT3D audio support files"
    Assert-Condition ($summary.kankyo_archive_count -eq 6) "expected 6 kankyo archives"
    Assert-Condition ($summary.kankyo_runtime_selection_status -eq "selected") "expected selected kankyo runtime profiles"
    Assert-Condition ($summary.kankyo_runtime_selection_profile_count -eq 4) "expected 4 kankyo skybox profiles"
    Assert-Condition ($summary.kankyo_unresolved_selection_count -eq 0) "expected no unresolved kankyo runtime selections"
    Assert-Condition ((Get-JsonValue $summary.kankyo_runtime_selection_status_counts "selected_kankyo_archive_candidate") -eq 2) "expected 2 selected normal sky kankyo profiles"
    Assert-Condition ((Get-JsonValue $summary.kankyo_runtime_selection_status_counts "n64_filter_only_no_kankyo_resource_needed") -eq 2) "expected 2 filter-only skybox profiles"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_oot3d_spawn_list_layout_validation") -eq $null) "expected no OOT3D spawn layout validation gaps"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_player_spawn_indices") -eq $null) "expected no unprefixed OOT3D player spawn layouts"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_prefixed_player_spawn_indices") -eq 17) "expected 17 validated prefixed OOT3D player spawn layouts"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_route_room_indices") -eq $null) "expected no unprefixed OOT3D entrance layouts"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_prefixed_route_room_indices") -eq 17) "expected 17 validated prefixed OOT3D entrance layouts"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_s16_exit_payload_window") -eq $null) "expected no unprefixed OOT3D exit payload windows"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_prefixed_s16_exit_payload_window") -eq 17) "expected 17 validated prefixed OOT3D exit payload windows"
    Assert-Condition ((Get-JsonValue $summary.layout_validation_counts "validated_n64_path_record_window") -eq 6) "expected 6 validated OOT3D path record windows"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_oot3d_entrance_list_layout_validation") -eq $null) "expected no OOT3D entrance layout validation gaps"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_oot3d_path_list_layout_validation") -eq $null) "expected no OOT3D path layout validation gaps"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_oot3d_exit_list_layout_validation") -eq $null) "expected no OOT3D exit layout validation gaps"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_audio_entry_mapping") -eq $null) "expected no audio entry mapping gap"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_oot3d_room_actor_runtime_routing") -eq $null) "expected no room actor runtime routing blocker"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "missing_standard_actor_list_commands") -eq $null) "expected no standard actor-list missing blocker after room payload identification"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "missing_object_list_commands") -eq $null) "expected no object list blocker after actor/object binding resolution"
    Assert-Condition ((Get-JsonValue $summary.runtime_gap_counts "needs_kankyo_runtime_selection") -eq $null) "expected no kankyo runtime selection gap"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected zero parse errors"
}

Write-Host "OOT3D Kokiri runtime route audit: $auditOutput"
Write-Host "OOT3D Kokiri runtime route summary: $summaryOutput"
