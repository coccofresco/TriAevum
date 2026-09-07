param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$RuntimeManifest = "",
    [string]$ActorAssetReadiness = "",
    [string]$StaticBatchManifest = "",
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
$actorAssetReadinessWasProvided = -not [string]::IsNullOrWhiteSpace($ActorAssetReadiness)
if (-not $actorAssetReadinessWasProvided) {
    $ActorAssetReadiness = Join-Path $WorkRoot "kokiri_actor_asset_readiness\oot3d_kokiri_actor_asset_readiness_audit.json"
}
if ([string]::IsNullOrWhiteSpace($StaticBatchManifest)) {
    $StaticBatchManifest = Join-Path $WorkRoot "static_actor_export\converted\batch_manifest.json"
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
        throw "OOT3D Kokiri static actor binding plan verification failed: $Message"
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

Require-Path $ToolRoot "Tool root"
Require-Path $RuntimeManifest "Kokiri runtime manifest"
Require-Path $StaticBatchManifest "OOT3D static actor batch manifest"

if (-not $actorAssetReadinessWasProvided) {
    $assetReadinessScript = Join-Path $PSScriptRoot "Invoke-Oot3dKokiriActorAssetReadinessAudit.ps1"
    Require-Path $assetReadinessScript "Kokiri actor asset readiness script"
    $assetReadinessArgs = @{
        ToolRoot = $ToolRoot
        WorkRoot = $WorkRoot
        RuntimeManifest = $RuntimeManifest
        StaticBatchManifest = $StaticBatchManifest
        SampleLimit = $SampleLimit
    }
    if ($Verify) {
        $assetReadinessArgs["Verify"] = $true
    }
    & $assetReadinessScript @assetReadinessArgs
}

Require-Path $ActorAssetReadiness "Kokiri actor asset readiness audit"

$outputRoot = Join-Path $WorkRoot "kokiri_static_actor_binding"
$planOutput = Join-Path $outputRoot "oot3d_kokiri_static_actor_binding_plan.json"
$summaryOutput = Join-Path $outputRoot "oot3d_kokiri_static_actor_binding_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-kokiri-static-actor-binding-plan",
        $RuntimeManifest,
        $ActorAssetReadiness,
        $StaticBatchManifest,
        "--output",
        $planOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $planOutput "Kokiri static actor binding plan"
$plan = Get-Content -LiteralPath $planOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    runtime_manifest = (Resolve-Path $RuntimeManifest).Path
    actor_asset_readiness = (Resolve-Path $ActorAssetReadiness).Path
    static_batch_manifest = (Resolve-Path $StaticBatchManifest).Path
    plan = $planOutput
    route_id = $plan.route_id
    runtime_enablement_status = $plan.runtime_enablement_status
    route_room_actor_target_count = $plan.route_room_actor_target_count
    route_actor_entry_count = $plan.route_actor_entry_count
    unique_route_actor_count = $plan.unique_route_actor_count
    visual_actor_entry_count = $plan.visual_actor_entry_count
    behavior_actor_entry_count = $plan.behavior_actor_entry_count
    route_object_with_static_exports_count = $plan.route_object_with_static_exports_count
    route_static_exported_model_count = $plan.route_static_exported_model_count
    ready_static_binding_count = $plan.ready_static_binding_count
    ready_static_actor_entry_count = $plan.ready_static_actor_entry_count
    ready_static_top_display_list_count = $plan.ready_static_top_display_list_count
    actor_binding_status_counts = $plan.actor_binding_status_counts
    actor_entry_binding_status_counts = $plan.actor_entry_binding_status_counts
    binding_issue_counts = $plan.binding_issue_counts
    ready_binding_issue_total = $plan.ready_binding_issue_total
    ready_binding_actor_names = @($plan.ready_binding_records | ForEach-Object { $_.actor_name })
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($plan.format -eq "oot3d_kokiri_static_actor_binding_plan_v1") "unexpected plan format"
    Assert-Condition ($summary.route_id -eq "link_house_to_kokiri_forest") "unexpected route id"
    Assert-Condition ($summary.runtime_enablement_status -eq "n64_fallback_only") "unexpected runtime enablement status"
    Assert-Condition ($summary.route_room_actor_target_count -eq 4) "expected 4 route room actor-list targets"
    Assert-Condition ($summary.route_actor_entry_count -eq 104) "expected 104 route actor entries"
    Assert-Condition ($summary.unique_route_actor_count -eq 25) "expected 25 unique route actors"
    Assert-Condition ($summary.visual_actor_entry_count -eq 69) "expected 69 visual actor entries"
    Assert-Condition ($summary.behavior_actor_entry_count -eq 35) "expected 35 behavior/fallback actor entries"
    Assert-Condition ($summary.route_object_with_static_exports_count -eq 12) "expected 12 route objects with static exports"
    Assert-Condition ($summary.route_static_exported_model_count -eq 44) "expected 44 static exported route object models"
    Assert-Condition ($summary.ready_static_binding_count -eq 12) "expected 12 ready static actor bindings"
    Assert-Condition ($summary.ready_static_actor_entry_count -eq 52) "expected 52 ready actor entries"
    Assert-Condition ($summary.ready_static_top_display_list_count -eq 17) "expected 17 ready top display-list resources"
    Assert-Condition ($summary.ready_binding_issue_total -eq 0) "expected no first-wave ready binding issues"
    Assert-Condition ((Get-JsonValue $summary.binding_issue_counts "actor_room_object_prefix_gap") -eq $null) "expected no actor/object prefix gaps"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "ready_single_display_list_binding") -eq 6) "expected 6 ready actor-family bindings"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "ready_param_selected_display_list_binding") -eq 4) "expected four ready param-selected actor-family bindings"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "ready_stateful_display_list_binding") -eq 2) "expected two ready stateful actor-family bindings"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "needs_actor_param_model_selector") -eq $null) "expected no actor-family param selector blockers"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "needs_multi_part_actor_draw_binding") -eq $null) "expected no multi-part draw blockers"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "needs_dekubaba_actor_family_mapping") -eq 1) "expected one Deku Baba family mapping blocker"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "blocked_skinned_animation_track_resolution") -eq 2) "expected two skinned track-resolution blockers"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "needs_skinned_actor_runtime_binding") -eq 3) "expected three skinned runtime binding blockers"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "needs_actor_object_semantic_mapping") -eq $null) "expected no actor/object semantic mapping blockers"
    Assert-Condition ((Get-JsonValue $summary.actor_binding_status_counts "n64_behavior_fallback") -eq 7) "expected seven behavior fallback actor families"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "ready_single_display_list_binding") -eq 8) "expected 8 single-display-list actor entries"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "ready_param_selected_display_list_binding") -eq 33) "expected 33 param-selected actor entries"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "ready_stateful_display_list_binding") -eq 11) "expected 11 stateful actor entries"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "needs_actor_param_model_selector") -eq $null) "expected no actor entries needing param selector"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "needs_multi_part_actor_draw_binding") -eq $null) "expected no actor entries needing multi-part draw binding"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "n64_behavior_fallback") -eq 35) "expected 35 behavior fallback actor entries"
    Assert-Condition ((Get-JsonValue $summary.actor_entry_binding_status_counts "needs_actor_object_semantic_mapping") -eq $null) "expected no actor entries needing semantic mapping"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_OBJ_TSUBO") "expected pot binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_BOX") "expected chest binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_GOROIWA") "expected rolling rock binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_GS") "expected gossip stone binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_BG_TREEMOUTH") "expected tree mouth binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_KUSA") "expected param-selected grass binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_DOOR_ANA") "expected grotto binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_A_OBJ") "expected Kokiri arrow sign binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_ISHI") "expected liftable rock binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_OBJ_HANA") "expected field prop binding in first wave"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_EN_KANBAN") "expected stateful Kokiri rectangle sign binding"
    Assert-Condition ($summary.ready_binding_actor_names -contains "ACTOR_OBJ_BEAN") "expected stateful magic bean binding"
}

Write-Host "OOT3D Kokiri static actor binding plan: $planOutput"
Write-Host "OOT3D Kokiri static actor binding summary: $summaryOutput"
