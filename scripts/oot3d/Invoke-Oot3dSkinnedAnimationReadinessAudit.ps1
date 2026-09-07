param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$BindPoseManifest = "",
    [string]$CsabTrackManifest = "",
    [int]$SampleLimit = 100,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($BindPoseManifest)) {
    $BindPoseManifest = Join-Path $WorkRoot "skinned_bind_pose_batch\skinned_bind_pose_batch_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($CsabTrackManifest)) {
    $CsabTrackManifest = Join-Path $WorkRoot "skinned_animation_batch\csab_skeleton_track_batch_manifest.json"
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
        throw "OOT3D skinned animation readiness verification failed: $Message"
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
Require-Path $BindPoseManifest "Skinned bind-pose batch manifest"
Require-Path $CsabTrackManifest "Skinned CSAB track batch manifest"

$outputRoot = Join-Path $WorkRoot "skinned_animation_readiness_audit"
$auditOutput = Join-Path $outputRoot "oot3d_skinned_animation_readiness_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_skinned_animation_readiness_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-skinned-animation-readiness",
        $BindPoseManifest,
        $CsabTrackManifest,
        "--output",
        $auditOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $auditOutput "Skinned animation readiness audit"
$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    bind_pose_manifest = (Resolve-Path $BindPoseManifest).Path
    csab_track_manifest = (Resolve-Path $CsabTrackManifest).Path
    audit = $auditOutput
    bind_pose_export_count = $audit.bind_pose_export_count
    csab_track_export_count = $audit.csab_track_export_count
    unique_csab_target_count = $audit.unique_csab_target_count
    csab_targets_with_bind_pose_count = $audit.csab_targets_with_bind_pose_count
    unused_bind_pose_export_count = $audit.unused_bind_pose_export_count
    issue_counts = $audit.issue_counts
    support_status_counts = $audit.support_status_counts
    target_resolution_status_counts = $audit.target_resolution_status_counts
    unique_model_support_status_counts = $audit.unique_model_support_status_counts
    animation_count_per_target_counts = $audit.animation_count_per_target_counts
    max_animation_count_per_target = $audit.max_animation_count_per_target
    aggregate_track_counts = $audit.aggregate_track_counts
    referenced_bind_pose_counts = $audit.referenced_bind_pose_counts
    unused_bind_pose_counts = $audit.unused_bind_pose_counts
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($audit.format -eq "oot3d_skinned_animation_readiness_audit_v1") "unexpected audit format"
    Assert-Condition ($summary.bind_pose_export_count -eq 202) "expected 202 skinned bind-pose exports"
    Assert-Condition ($summary.csab_track_export_count -eq 2271) "expected 2271 skinned CSAB track exports"
    Assert-Condition ($summary.unique_csab_target_count -eq 156) "expected 156 unique skinned CSAB target models"
    Assert-Condition ($summary.csab_targets_with_bind_pose_count -eq 156) "expected every unique CSAB target to have a bind-pose export"
    Assert-Condition ($summary.unused_bind_pose_export_count -eq 46) "expected 46 skinned bind-pose exports without resolved CSAB tracks"

    Assert-Condition ($summary.issue_counts.duplicate_bind_pose_key -eq 0) "expected 0 duplicate bind-pose target keys"
    Assert-Condition ($summary.issue_counts.missing_bind_pose_target -eq 0) "expected 0 CSAB targets missing bind-pose exports"
    Assert-Condition ($summary.issue_counts.bone_count_mismatch -eq 0) "expected 0 target/bind-pose bone-count mismatches"
    Assert-Condition ($summary.issue_counts.non_skinned_exported_track -eq 0) "expected 0 non-skinned exported CSAB tracks"
    Assert-Condition ($summary.issue_counts.missing_bind_pose_export_file -eq 0) "expected 0 missing bind-pose export files"
    Assert-Condition ($summary.issue_counts.missing_track_export_file -eq 0) "expected 0 missing CSAB track export files"
    Assert-Condition ($summary.issue_counts.total -eq 0) "expected zero readiness issues"

    Assert-Condition ((Get-JsonValue $summary.support_status_counts "needs_skinning_mode_1_support") -eq 46) "expected 46 mode-1 CSAB exports"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "needs_skinning_mode_2_support") -eq 1247) "expected 1247 mode-2 CSAB exports"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "needs_skinning_mode_1_and_2_support") -eq 978) "expected 978 mixed-mode CSAB exports"
    Assert-Condition ((Get-JsonValue $summary.unique_model_support_status_counts "needs_skinning_mode_1_support") -eq 9) "expected 9 unique mode-1 target models"
    Assert-Condition ((Get-JsonValue $summary.unique_model_support_status_counts "needs_skinning_mode_2_support") -eq 105) "expected 105 unique mode-2 target models"
    Assert-Condition ((Get-JsonValue $summary.unique_model_support_status_counts "needs_skinning_mode_1_and_2_support") -eq 42) "expected 42 unique mixed-mode target models"

    Assert-Condition ((Get-JsonValue $summary.target_resolution_status_counts "single_bone_count_match") -eq 2244) "expected 2244 single bone-count matches"
    Assert-Condition ((Get-JsonValue $summary.target_resolution_status_counts "multiple_bone_count_contained_name_match") -eq 26) "expected 26 contained-name target matches"
    Assert-Condition ((Get-JsonValue $summary.target_resolution_status_counts "multiple_bone_count_exact_name_match") -eq 1) "expected 1 exact-name target match"

    Assert-Condition ($summary.max_animation_count_per_target -eq 582) "expected max 582 CSAB exports per target"
    Assert-Condition ((Get-JsonValue $summary.animation_count_per_target_counts "1") -eq 39) "expected 39 targets with one CSAB export"
    Assert-Condition ((Get-JsonValue $summary.animation_count_per_target_counts "582") -eq 2) "expected 2 targets with 582 CSAB exports"

    Assert-Condition ($summary.aggregate_track_counts.track_count -eq 46274) "expected 46274 total exported bone tracks"
    Assert-Condition ($summary.aggregate_track_counts.channel_count -eq 154303) "expected 154303 decoded CSAB channels"
    Assert-Condition ($summary.aggregate_track_counts.const_channel_count -eq 33998) "expected 33998 constant CSAB channels"
    Assert-Condition ($summary.aggregate_track_counts.keyed_channel_count -eq 120305) "expected 120305 keyed CSAB channels"
    Assert-Condition ($summary.aggregate_track_counts.keyframe_count -eq 1538116) "expected 1538116 CSAB keyframes"

    Assert-Condition ($summary.referenced_bind_pose_counts.mesh_count -eq 478) "expected 478 referenced skinned meshes"
    Assert-Condition ($summary.referenced_bind_pose_counts.skinned_primitive_count -eq 896) "expected 896 referenced skinned primitives"
    Assert-Condition ($summary.referenced_bind_pose_counts.skinned_vertex_rows -eq 86239) "expected 86239 referenced skinned vertex rows"
    Assert-Condition ($summary.referenced_bind_pose_counts.skeleton_bone_count -eq 2640) "expected 2640 referenced skeleton bones"
    Assert-Condition ($summary.referenced_bind_pose_counts.validation_error_count -eq 0) "expected 0 referenced bind-pose validation errors"

    Assert-Condition ($summary.unused_bind_pose_counts.mesh_count -eq 105) "expected 105 unused skinned meshes"
    Assert-Condition ($summary.unused_bind_pose_counts.skinned_primitive_count -eq 201) "expected 201 unused skinned primitives"
    Assert-Condition ($summary.unused_bind_pose_counts.skinned_vertex_rows -eq 21265) "expected 21265 unused skinned vertex rows"
    Assert-Condition ($summary.unused_bind_pose_counts.skeleton_bone_count -eq 640) "expected 640 unused skeleton bones"
    Assert-Condition ($summary.unused_bind_pose_counts.validation_error_count -eq 0) "expected 0 unused bind-pose validation errors"
}

Write-Host "OOT3D skinned animation readiness audit: $auditOutput"
Write-Host "OOT3D skinned animation readiness summary: $summaryOutput"
