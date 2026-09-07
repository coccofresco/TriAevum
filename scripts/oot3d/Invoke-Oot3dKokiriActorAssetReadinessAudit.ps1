param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$RuntimeManifest = "",
    [string]$ActorInventory = "",
    [string]$StaticBatchManifest = "",
    [string]$SkinnedBindingManifest = "",
    [int]$SampleLimit = 100,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($RuntimeManifest)) {
    $RuntimeManifest = Join-Path $WorkRoot "kokiri_runtime_route\oot3d_kokiri_runtime_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($ActorInventory)) {
    $ActorInventory = Join-Path $WorkRoot "actor_inventory\oot3d_actor_inventory.json"
}
if ([string]::IsNullOrWhiteSpace($StaticBatchManifest)) {
    $StaticBatchManifest = Join-Path $WorkRoot "static_actor_export\converted\batch_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($SkinnedBindingManifest)) {
    $SkinnedBindingManifest = Join-Path $WorkRoot "skinned_animation_binding\oot3d_skinned_animation_binding_manifest.json"
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
        throw "OOT3D Kokiri actor asset readiness verification failed: $Message"
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

function Get-JsonPropertyTotal($Object) {
    if ($null -eq $Object) {
        return 0
    }
    $total = 0
    foreach ($property in $Object.PSObject.Properties) {
        $total += [int]$property.Value
    }
    return $total
}

Require-Path $ToolRoot "Tool root"
Require-Path $RuntimeManifest "Kokiri runtime manifest"
Require-Path $ActorInventory "OOT3D actor inventory"
Require-Path $StaticBatchManifest "OOT3D static actor batch manifest"
Require-Path $SkinnedBindingManifest "OOT3D skinned animation binding manifest"

$outputRoot = Join-Path $WorkRoot "kokiri_actor_asset_readiness"
$auditOutput = Join-Path $outputRoot "oot3d_kokiri_actor_asset_readiness_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_kokiri_actor_asset_readiness_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-kokiri-actor-asset-readiness",
        $RuntimeManifest,
        $ActorInventory,
        $StaticBatchManifest,
        $SkinnedBindingManifest,
        "--output",
        $auditOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $auditOutput "Kokiri actor asset readiness audit"
$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    runtime_manifest = (Resolve-Path $RuntimeManifest).Path
    actor_inventory = (Resolve-Path $ActorInventory).Path
    static_batch_manifest = (Resolve-Path $StaticBatchManifest).Path
    skinned_binding_manifest = (Resolve-Path $SkinnedBindingManifest).Path
    audit = $auditOutput
    route_id = $audit.route_id
    runtime_enablement_status = $audit.runtime_enablement_status
    route_room_actor_target_count = $audit.route_room_actor_target_count
    route_actor_entry_count = $audit.route_actor_entry_count
    unique_route_actor_count = $audit.unique_route_actor_count
    route_object_prefix_reference_count = $audit.route_object_prefix_reference_count
    unique_route_object_count = $audit.unique_route_object_count
    mapped_route_object_count = $audit.mapped_route_object_count
    object_asset_readiness_status_counts = $audit.object_asset_readiness_status_counts
    actor_behavior_status_counts = $audit.actor_behavior_status_counts
    archive_mapping_issue_counts = $audit.archive_mapping_issue_counts
    asset_blocker_counts = $audit.asset_blocker_counts
    object_record_count = @($audit.object_records).Count
    sampled_actor_record_count = @($audit.actor_records).Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($audit.format -eq "oot3d_kokiri_actor_asset_readiness_v1") "unexpected audit format"
    Assert-Condition ($summary.route_id -eq "link_house_to_kokiri_forest") "unexpected route id"
    Assert-Condition ($summary.runtime_enablement_status -eq "n64_fallback_only") "unexpected runtime enablement status"
    Assert-Condition ($summary.route_room_actor_target_count -eq 4) "expected 4 room actor-list targets"
    Assert-Condition ($summary.route_actor_entry_count -eq 104) "expected 104 route actor entries"
    Assert-Condition ($summary.unique_route_actor_count -eq 25) "expected 25 unique route actor names"
    Assert-Condition ($summary.route_object_prefix_reference_count -eq 23) "expected 23 route object-prefix references"
    Assert-Condition ($summary.unique_route_object_count -eq 19) "expected 19 unique route object names"
    Assert-Condition ($summary.mapped_route_object_count -eq 19) "expected all 19 route objects to map to actor archives"
    Assert-Condition ($summary.object_record_count -eq 19) "expected one object record per unique route object"
    Assert-Condition ($summary.sampled_actor_record_count -le $SampleLimit) "actor record sample exceeded SampleLimit"
    Assert-Condition ((Get-JsonPropertyTotal $summary.archive_mapping_issue_counts) -eq 0) "expected no archive mapping issues"
    Assert-Condition ((Get-JsonValue $summary.object_asset_readiness_status_counts "mixed_static_and_skinned_animation_bindings_ready") -eq 2) "expected 2 mixed static/skinned object archives"
    Assert-Condition ((Get-JsonValue $summary.object_asset_readiness_status_counts "skinned_animation_bindings_ready") -eq 6) "expected 6 skinned animation binding-ready object archives"
    Assert-Condition ((Get-JsonValue $summary.object_asset_readiness_status_counts "skinned_bind_pose_ready_animation_tracks_unresolved") -eq 2) "expected 2 object archives with bind poses but unresolved animation tracks"
    Assert-Condition ((Get-JsonValue $summary.object_asset_readiness_status_counts "static_or_rigid_exports_ready") -eq 9) "expected 9 static/rigid object archives"
    Assert-Condition ((Get-JsonValue $summary.asset_blocker_counts "needs_shipwright_runtime_actor_asset_binding") -eq 17) "expected 17 runtime actor asset binding blockers"
    Assert-Condition ((Get-JsonValue $summary.asset_blocker_counts "needs_skinned_animation_track_resolution") -eq 2) "expected 2 skinned animation track resolution blockers"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_audio_trigger_behavior_fallback") -eq 1) "expected one audio trigger behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_collectible_item_behavior_fallback") -eq 1) "expected one collectible item behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_dialog_trigger_behavior_fallback") -eq 1) "expected one dialog trigger behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_environment_actor_behavior_fallback") -eq 1) "expected one environment actor behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_hidden_item_trigger_behavior_fallback") -eq 1) "expected one hidden item trigger behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_soil_spawner_behavior_fallback") -eq 1) "expected one soil spawner behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_spawn_controller_behavior_fallback") -eq 1) "expected one spawn controller behavior fallback"
    Assert-Condition ((Get-JsonValue $summary.actor_behavior_status_counts "n64_visual_actor_behavior_fallback") -eq 18) "expected 18 visual actor behavior fallbacks"
}

Write-Host "OOT3D Kokiri actor asset readiness audit: $auditOutput"
Write-Host "OOT3D Kokiri actor asset readiness summary: $summaryOutput"
