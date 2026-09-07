param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ActorObjectSemantics = "",
    [string]$N64Rom = "H:\Rom&Iso\n64\Legend of Zelda, The - Ocarina of Time (U) (V1.2) [!].z64",
    [int]$SampleLimit = 100,
    [switch]$RuntimeEnabled,
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
        throw "OOT3D Kokiri runtime manifest verification failed: $Message"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.ContainsKey($Name)) {
            return $Object[$Name]
        }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Add-Count($Map, [string]$Key) {
    if ($Map.ContainsKey($Key)) {
        $Map[$Key] += 1
    } else {
        $Map[$Key] = 1
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"
Require-Path (Join-Path $RomFs "scene") "OOT3D scene directory"
Require-Path $ActorObjectSemantics "Actor/object semantics header"
if (-not [string]::IsNullOrWhiteSpace($N64Rom)) {
    Require-Path $N64Rom "N64 ROM reference"
}

$auditScript = Join-Path $PSScriptRoot "Invoke-Oot3dKokiriRuntimeRouteAudit.ps1"
Require-Path $auditScript "Kokiri runtime route audit script"

$auditArgs = @{
    ToolRoot = $ToolRoot
    RomFs = $RomFs
    WorkRoot = $WorkRoot
    ActorObjectSemantics = $ActorObjectSemantics
    SampleLimit = $SampleLimit
}
if ($Verify) {
    $auditArgs["Verify"] = $true
}
& $auditScript @auditArgs

$outputRoot = Join-Path $WorkRoot "kokiri_runtime_route"
$auditOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_route_audit.json"
if ($RuntimeEnabled) {
    $manifestOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_manifest_enabled.json"
    $summaryOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_manifest_enabled_summary.json"
    $runtimeEnablementStatus = "oot3d_runtime_enabled"
} else {
    $manifestOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_manifest.json"
    $summaryOutput = Join-Path $outputRoot "oot3d_kokiri_runtime_manifest_summary.json"
    $runtimeEnablementStatus = "n64_fallback_only"
}

Require-Path $auditOutput "Kokiri runtime route audit"
Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-kokiri-runtime-manifest",
        $auditOutput,
        "--output",
        $manifestOutput,
        "--n64-rom",
        $N64Rom,
        "--runtime-enablement-status",
        $runtimeEnablementStatus,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $manifestOutput "Kokiri runtime manifest"
$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json

$resourceKindCounts = @{}
$roomActorTargetEntryTotal = 0
$roomActorTargetObjectTotal = 0
foreach ($target in @($manifest.resource_targets)) {
    Add-Count $resourceKindCounts ([string]$target.kind)
    if ($target.kind -eq "room_actor_list") {
        $roomActorTargetEntryTotal += @($target.actor_entries).Count
        $roomActorTargetObjectTotal += @($target.object_ids).Count
    }
}

$runtimeBlockerTotal = 0
foreach ($property in $manifest.runtime_blocker_counts.PSObject.Properties) {
    $runtimeBlockerTotal += [int]$property.Value
}

$summary = [ordered]@{
    manifest = $manifestOutput
    audit = $auditOutput
    route_id = $manifest.route_id
    runtime_enablement_status = $manifest.runtime_enablement_status
    route_file_count = @($manifest.route_files).Count
    resource_target_count = @($manifest.resource_targets).Count
    resource_target_kind_counts = $resourceKindCounts
    setup_binding_count = @($manifest.setup_bindings).Count
    fallback_count = @($manifest.route_scope.shipwright_n64_fallbacks).Count
    mesh_status = $manifest.readiness.mesh.status
    collision_status = $manifest.readiness.collision.status
    scene_layout_status = $manifest.readiness.scene_layout.status
    spawn_payload_profile_counts = $manifest.readiness.scene_layout.spawn_payload_profile_counts
    actor_status = $manifest.readiness.actors.status
    room_actor_list_candidate_count = $manifest.readiness.actors.room_actor_list_candidate_count
    selected_room_actor_list_count = $manifest.readiness.actors.selected_room_actor_list_count
    selected_room_actor_entry_total = $manifest.readiness.actors.selected_room_actor_entry_total
    room_actor_target_entry_total = $roomActorTargetEntryTotal
    room_actor_target_object_total = $roomActorTargetObjectTotal
    selected_room_actor_name_counts = $manifest.readiness.actors.selected_room_actor_name_counts
    selected_room_actor_object_name_counts = $manifest.readiness.actors.selected_room_actor_object_name_counts
    animation_status = $manifest.readiness.animations.status
    audio_status = $manifest.readiness.audio.status
    audio_mapped_profile_count = $manifest.readiness.audio.mapped_profile_count
    audio_unresolved_profile_count = $manifest.readiness.audio.unresolved_profile_count
    audio_runtime_mapping_status = $manifest.audio_summary.runtime_mapping.status
    sky_environment_status = $manifest.readiness.sky_environment.status
    runtime_blocker_total = $runtimeBlockerTotal
    runtime_blocker_counts = $manifest.runtime_blocker_counts
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($manifest.format -eq "oot3d_kokiri_runtime_manifest_v1") "unexpected manifest format"
    Assert-Condition ($summary.route_id -eq "link_house_to_kokiri_forest") "unexpected route id"
    Assert-Condition ($summary.runtime_enablement_status -eq $runtimeEnablementStatus) "unexpected runtime enablement status"
    Assert-Condition ($summary.route_file_count -eq 6) "expected 6 route files"
    Assert-Condition ($summary.resource_target_count -eq 10) "expected 10 runtime resource targets"
    Assert-Condition ((Get-JsonValue $summary.resource_target_kind_counts "room_mesh") -eq 4) "expected 4 room mesh targets"
    Assert-Condition ((Get-JsonValue $summary.resource_target_kind_counts "room_actor_list") -eq 4) "expected 4 room actor-list targets"
    Assert-Condition ((Get-JsonValue $summary.resource_target_kind_counts "scene_collision") -eq 2) "expected 2 scene collision targets"
    Assert-Condition ($summary.setup_binding_count -eq 17) "expected 17 setup bindings"
    Assert-Condition ($summary.fallback_count -eq 2) "expected 2 N64 fallback scene records"
    Assert-Condition ($summary.mesh_status -eq "source_candidate_ready") "expected mesh source candidates to be ready"
    Assert-Condition ($summary.collision_status -eq "source_candidate_needs_surface_runtime_validation") "expected collision source candidates to need runtime validation"
    Assert-Condition ($summary.scene_layout_status -eq "layout_validated") "expected validated scene layout status"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "validated_n64_player_start_list") -eq $null) "expected no unprefixed N64-compatible player start list profiles"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "validated_prefixed_player_start_list") -eq 17) "expected 17 prefixed player start windows with validated entrance indices"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "prefixed_player_start_window_missing_entrance_indices") -eq $null) "expected no remaining prefixed player start windows with missing entrance indices"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "entrance_overlap_actor_entry_candidate") -eq $null) "expected no remaining entrance-overlap actor-entry profiles"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "path_block_overlap_actor_like_candidate") -eq $null) "expected no remaining path-overlap actor-like profiles"
    Assert-Condition ((Get-JsonValue $summary.spawn_payload_profile_counts "entrance_overlap_mixed_actor_entry_candidate") -eq $null) "expected no remaining mixed Link-house entrance-overlap actor-entry profile"
    Assert-Condition ($summary.actor_status -eq "room_actor_list_runtime_routing_ready") "expected room actor-list runtime routing to be ready"
    Assert-Condition ($summary.room_actor_list_candidate_count -eq 18) "expected 18 room actor-list payload candidates"
    Assert-Condition ($summary.selected_room_actor_list_count -eq 4) "expected one selected room actor-list candidate per route room"
    Assert-Condition ($summary.selected_room_actor_entry_total -eq 104) "expected 104 selected room actor-entry candidates"
    Assert-Condition ($summary.room_actor_target_entry_total -eq 104) "expected 104 actor entries embedded in room actor-list runtime targets"
    Assert-Condition ($summary.room_actor_target_object_total -eq 23) "expected 23 object ids embedded in room actor-list runtime targets"
    Assert-Condition ((Get-JsonValue $summary.selected_room_actor_name_counts "ACTOR_EN_KO") -eq 8) "expected selected room actor candidates to include Kokiri NPCs"
    Assert-Condition ((Get-JsonValue $summary.selected_room_actor_object_name_counts "OBJECT_KANBAN") -eq 3) "expected selected room object prefixes to include kanban objects"
    Assert-Condition ($summary.animation_status -eq "route_actor_bindings_ready_animation_assets_pending") "expected actor bindings ready before animation asset mapping"
    Assert-Condition ($summary.audio_status -eq "sound_settings_mapped_n64_audio_fallback") "expected route audio settings mapped to N64 fallback"
    Assert-Condition ($summary.audio_mapped_profile_count -eq 7) "expected 7 mapped route audio profiles"
    Assert-Condition ($summary.audio_unresolved_profile_count -eq 0) "expected no unresolved route audio profiles"
    Assert-Condition ($summary.audio_runtime_mapping_status -eq "mapped") "expected manifest audio runtime mapping to be mapped"
    Assert-Condition ($summary.sky_environment_status -eq "skybox_runtime_selection_ready") "expected selected sky environment runtime profiles"
    Assert-Condition ($summary.runtime_blocker_total -eq 0) "expected zero known runtime blocker counts"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_oot3d_spawn_list_layout_validation") -eq $null) "expected no spawn layout validation blockers"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_oot3d_entrance_list_layout_validation") -eq $null) "expected no entrance layout validation blockers"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_oot3d_path_list_layout_validation") -eq $null) "expected no path layout validation blockers"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_oot3d_exit_list_layout_validation") -eq $null) "expected no exit layout validation blockers"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_audio_entry_mapping") -eq $null) "expected no audio mapping blocker"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_oot3d_room_actor_runtime_routing") -eq $null) "expected no room actor runtime routing blocker"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "missing_standard_actor_list_commands") -eq $null) "expected no standard actor-list missing blocker after room payload identification"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "missing_object_list_commands") -eq $null) "expected no object list blocker after actor/object binding resolution"
    Assert-Condition ((Get-JsonValue $summary.runtime_blocker_counts "needs_kankyo_runtime_selection") -eq $null) "expected no kankyo selection blocker"
}

Write-Host "OOT3D Kokiri runtime manifest: $manifestOutput"
Write-Host "OOT3D Kokiri runtime manifest summary: $summaryOutput"
