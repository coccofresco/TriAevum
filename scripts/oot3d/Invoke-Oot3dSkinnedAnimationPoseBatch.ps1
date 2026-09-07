param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
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
        throw "OOT3D skinned animation pose batch verification failed: $Message"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$actorRoot = Join-Path $RomFs "actor"
Require-Path $actorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "skinned_animation_pose_batch"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "batch-skinned-animation-pose-samples",
        $actorRoot,
        "--output",
        $outputRoot,
        "--sample-limit",
        "0"
    )
}
finally {
    Pop-Location
}

$manifestPath = Join-Path $outputRoot "skinned_animation_pose_batch_manifest.json"
Require-Path $manifestPath "Skinned animation pose batch manifest"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

$poseSampleDir = Join-Path $outputRoot "pose_samples"
$poseSampleFileCount = @(Get-ChildItem -LiteralPath $poseSampleDir -Filter "*.json").Count
$exportedRecordCount = @($manifest.records | Where-Object { $_.status -eq "exported" }).Count
$failedRecordCount = @($manifest.records | Where-Object { $_.status -eq "failed" }).Count

$summaryOutput = Join-Path $outputRoot "skinned_animation_pose_batch_summary.json"
$summary = [ordered]@{
    format = "oot3d_skinned_animation_pose_batch_summary_v1"
    output_root = $outputRoot
    manifest = $manifestPath
    pose_sample_dir = $poseSampleDir
    pose_sample_file_count = $poseSampleFileCount
    considered_csab = $manifest.considered_csab
    archive_parse_failed = $manifest.archive_parse_failed
    target_unresolved_or_missing = $manifest.target_unresolved_or_missing
    target_not_skinned = $manifest.target_not_skinned
    resolved_skinned_targets = $manifest.resolved_skinned_targets
    exported = $manifest.exported
    failed = $manifest.failed
    exported_record_count = $exportedRecordCount
    failed_record_count = $failedRecordCount
    support_status_counts = $manifest.support_status_counts
    exported_support_status_counts = $manifest.exported_support_status_counts
    counts = $manifest.counts
    pose_counts = $manifest.pose_counts
    bind_pose_counts = $manifest.bind_pose_counts
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.considered_csab -eq 2465) "expected 2465 CSAB payloads"
    Assert-Condition ($summary.archive_parse_failed -eq 0) "expected 0 archive parse failures"
    Assert-Condition ($summary.target_unresolved_or_missing -eq 150) "expected 150 unresolved or missing targets"
    Assert-Condition ($summary.target_not_skinned -eq 44) "expected 44 non-skinned resolved targets"
    Assert-Condition ($summary.resolved_skinned_targets -eq 2271) "expected 2271 resolved skinned targets"
    Assert-Condition ($summary.exported -eq 2271) "expected 2271 exported pose samples"
    Assert-Condition ($summary.failed -eq 0) "expected 0 export failures"
    Assert-Condition ($summary.exported_record_count -eq 2271) "expected 2271 exported records"
    Assert-Condition ($summary.failed_record_count -eq 0) "expected 0 failed records"
    Assert-Condition ($summary.pose_sample_file_count -eq 2271) "expected 2271 pose sample files"

    Assert-Condition ((Get-JsonValue $summary.support_status_counts "needs_skinning_mode_1_support") -eq 46) "expected 46 mode-1 skinned targets"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "needs_skinning_mode_2_support") -eq 1247) "expected 1247 mode-2 skinned targets"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "needs_skinning_mode_1_and_2_support") -eq 978) "expected 978 mixed mode skinned targets"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "rigid_multibone_export_supported") -eq 37) "expected 37 rigid multibone non-skinned targets"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "static_cmb_export_supported") -eq 7) "expected 7 static non-skinned targets"

    Assert-Condition ($summary.counts.source_vertex_rows -eq 6701306) "expected 6701306 source skinned rows"
    Assert-Condition ($summary.counts.sampled_vertex_rows -eq 20078329) "expected 20078329 sampled vertex rows"
    Assert-Condition ($summary.counts.finite_position_rows -eq 20078329) "expected all sampled positions finite"
    Assert-Condition ($summary.counts.finite_normal_rows -eq 20078329) "expected all sampled normals finite"
    Assert-Condition ($summary.counts.finite_pose_rows -eq 20078329) "expected all sampled poses finite"
    Assert-Condition ($summary.counts.validation_error_count -eq 0) "expected 0 deformation validation errors"

    Assert-Condition ($summary.pose_counts.sampled_pose_frames -eq 6803) "expected 6803 sampled pose frames"
    Assert-Condition ($summary.pose_counts.sampled_channel_values -eq 462336) "expected 462336 sampled channel values"
    Assert-Condition ($summary.pose_counts.finite_channel_values -eq 462336) "expected all sampled channel values finite"
    Assert-Condition ($summary.pose_counts.non_f32_channel_blocks -eq 0) "expected 0 unsupported channel blocks"
    Assert-Condition ($summary.pose_counts.finite_world_matrix_entries -eq 2515216) "expected 2515216 finite pose matrix entries"

    Assert-Condition ($summary.bind_pose_counts.skinned_primitive_count -eq 37178) "expected 37178 bound skinned primitives across target uses"
    Assert-Condition ($summary.bind_pose_counts.mode_1_primitive_count -eq 1293) "expected 1293 mode-1 primitive uses"
    Assert-Condition ($summary.bind_pose_counts.mode_2_primitive_count -eq 35885) "expected 35885 mode-2 primitive uses"
    Assert-Condition ($summary.bind_pose_counts.skinned_vertex_rows -eq 6701306) "expected bind row total to match source rows"
    Assert-Condition ($summary.bind_pose_counts.validation_error_count -eq 0) "expected 0 bind-pose validation errors"
}

Write-Host "OOT3D skinned animation pose batch manifest: $manifestPath"
Write-Host "OOT3D skinned animation pose batch summary: $summaryOutput"
