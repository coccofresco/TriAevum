param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$BindPoseManifest = "",
    [string]$CsabTrackManifest = "",
    [string]$BindPoseArchivePrefix = "objects/oot3d/skinned_bind_pose",
    [string]$CsabTrackArchivePrefix = "animations/oot3d/csab/skinned",
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
        throw "OOT3D skinned animation binding manifest verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "skinned_animation_binding"
$manifestOutput = Join-Path $outputRoot "oot3d_skinned_animation_binding_manifest.json"
$summaryOutput = Join-Path $outputRoot "oot3d_skinned_animation_binding_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-skinned-animation-binding-manifest",
        $BindPoseManifest,
        $CsabTrackManifest,
        "--output",
        $manifestOutput,
        "--bind-pose-archive-prefix",
        $BindPoseArchivePrefix,
        "--csab-track-archive-prefix",
        $CsabTrackArchivePrefix,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $manifestOutput "Skinned animation binding manifest"
$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$firstTarget = $manifest.targets[0]
$topTarget = ($manifest.targets | Sort-Object -Property animation_count -Descending | Select-Object -First 1)
$summary = [ordered]@{
    bind_pose_manifest = (Resolve-Path $BindPoseManifest).Path
    csab_track_manifest = (Resolve-Path $CsabTrackManifest).Path
    binding_manifest = $manifestOutput
    bind_pose_archive_prefix = $manifest.bind_pose_archive_prefix
    csab_track_archive_prefix = $manifest.csab_track_archive_prefix
    target_count = $manifest.target_count
    animation_count = $manifest.animation_count
    unused_bind_pose_target_count = $manifest.unused_bind_pose_target_count
    readiness_summary = $manifest.readiness_summary
    first_target = [ordered]@{
        target_id = $firstTarget.target_id
        archive_path = $firstTarget.archive_path
        target_cmb_name = $firstTarget.target_cmb_name
        animation_count = $firstTarget.animation_count
        bind_pose_package_entry = $firstTarget.bind_pose.package_entry
        first_animation_track_package_entry = $firstTarget.animations[0].track_package_entry
    }
    top_target = [ordered]@{
        target_id = $topTarget.target_id
        archive_path = $topTarget.archive_path
        target_cmb_name = $topTarget.target_cmb_name
        animation_count = $topTarget.animation_count
        bind_pose_package_entry = $topTarget.bind_pose.package_entry
        first_animation_track_package_entry = $topTarget.animations[0].track_package_entry
    }
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($manifest.format -eq "oot3d_skinned_animation_binding_manifest_v1") "unexpected manifest format"
    Assert-Condition ($summary.target_count -eq 156) "expected 156 skinned target binding records"
    Assert-Condition ($summary.animation_count -eq 2271) "expected 2271 skinned animation binding records"
    Assert-Condition ($summary.unused_bind_pose_target_count -eq 46) "expected 46 unused bind-pose targets"
    Assert-Condition ($manifest.targets.Count -eq 156) "expected 156 target records"
    Assert-Condition ($manifest.unused_bind_pose_targets.Count -eq 46) "expected 46 unused bind-pose records"
    Assert-Condition ($summary.readiness_summary.issue_counts.total -eq 0) "expected zero embedded readiness issues"
    Assert-Condition ($summary.readiness_summary.csab_targets_with_bind_pose_count -eq 156) "expected every target to have bind-pose coverage"
    Assert-Condition ($summary.readiness_summary.csab_track_export_count -eq 2271) "expected 2271 CSAB exports in readiness summary"
    Assert-Condition ($summary.readiness_summary.bind_pose_export_count -eq 202) "expected 202 bind-pose exports in readiness summary"
    Assert-Condition ($summary.readiness_summary.referenced_bind_pose_counts.skinned_vertex_rows -eq 107716) "expected 107716 referenced skinned vertex rows"
    Assert-Condition ($summary.readiness_summary.aggregate_track_counts.track_count -eq 46274) "expected 46274 total CSAB bone tracks"
    Assert-Condition ((Get-JsonValue $summary.readiness_summary.animation_count_per_target_counts "582") -eq 2) "expected 2 targets with 582 CSAB animations"
    Assert-Condition ($summary.top_target.animation_count -eq 582) "expected top target animation count 582"
    Assert-Condition ([string]$summary.first_target.bind_pose_package_entry -like "$BindPoseArchivePrefix/*") "expected bind-pose package entry prefix"
    Assert-Condition ([string]$summary.first_target.first_animation_track_package_entry -like "$CsabTrackArchivePrefix/*") "expected CSAB track package entry prefix"
}

Write-Host "OOT3D skinned animation binding manifest: $manifestOutput"
Write-Host "OOT3D skinned animation binding summary: $summaryOutput"
