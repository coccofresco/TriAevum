param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
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
        throw "OOT3D skinned animation batch verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "skinned_animation_batch"
$manifestOutput = Join-Path $outputRoot "csab_skeleton_track_batch_manifest.json"
$summaryOutput = Join-Path $outputRoot "skinned_animation_batch_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "batch-csab-skeleton-tracks",
        $ActorRoot,
        "--output",
        $outputRoot
    )
}
finally {
    Pop-Location
}

$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    considered_csab = $manifest.considered_csab
    target_unresolved_or_missing = $manifest.target_unresolved_or_missing
    target_not_skinned = $manifest.target_not_skinned
    resolved_skinned_targets = $manifest.resolved_skinned_targets
    target_unsupported_non_f32_channels = $manifest.target_unsupported_non_f32_channels
    exported = $manifest.exported
    failed = $manifest.failed
    track_file_count = (Get-ChildItem -LiteralPath (Join-Path $outputRoot "tracks") -Filter *.json).Count
    support_status_counts = $manifest.support_status_counts
    exported_support_status_counts = $manifest.exported_support_status_counts
    unsupported_support_status_counts = $manifest.unsupported_support_status_counts
    encoding_counts = $manifest.encoding_counts
    counts = $manifest.counts
    unsupported_counts = $manifest.unsupported_counts
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.considered_csab -eq 2465) "expected 2465 considered CSAB payloads"
    Assert-Condition ($summary.target_unresolved_or_missing -eq 150) "expected 150 unresolved or missing CSAB targets"
    Assert-Condition ($summary.target_not_skinned -eq 44) "expected 44 non-skinned CSAB targets"
    Assert-Condition ($summary.resolved_skinned_targets -eq 2271) "expected 2271 resolved skinned CSAB targets"
    Assert-Condition ($summary.target_unsupported_non_f32_channels -eq 0) "expected 0 skinned CSAB targets blocked by non-f32 channels"
    Assert-Condition ($summary.exported -eq 2271) "expected 2271 skinned CSAB track exports"
    Assert-Condition ($summary.failed -eq 0) "expected 0 failed skinned CSAB track exports"
    Assert-Condition ($summary.track_file_count -eq 2271) "expected 2271 generated track JSON files"

    Assert-Condition ($summary.support_status_counts.needs_skinning_mode_1_support -eq 46) "expected 46 mode-1 skinned targets"
    Assert-Condition ($summary.support_status_counts.needs_skinning_mode_2_support -eq 1247) "expected 1247 mode-2 skinned targets"
    Assert-Condition ($summary.support_status_counts.needs_skinning_mode_1_and_2_support -eq 978) "expected 978 mixed mode-1/mode-2 skinned targets"
    Assert-Condition ($summary.exported_support_status_counts.needs_skinning_mode_1_support -eq 46) "expected 46 exported mode-1 skinned targets"
    Assert-Condition ($summary.exported_support_status_counts.needs_skinning_mode_2_support -eq 1247) "expected 1247 exported mode-2 skinned targets"
    Assert-Condition ($summary.exported_support_status_counts.needs_skinning_mode_1_and_2_support -eq 978) "expected 978 exported mixed skinned targets"
    Assert-Condition (@($summary.unsupported_support_status_counts.PSObject.Properties).Count -eq 0) "expected no unsupported skinned target support statuses"

    Assert-Condition ($summary.encoding_counts.constant_f32 -eq 31047) "expected 31047 constant f32 channels"
    Assert-Condition ($summary.encoding_counts.keyed_f32_hermite -eq 57840) "expected 57840 keyed f32 channels"
    Assert-Condition ($summary.encoding_counts.constant_s16_rotation -eq 2951) "expected 2951 constant s16 rotation channels"
    Assert-Condition ($summary.encoding_counts.keyed_s16_rotation_hermite -eq 62465) "expected 62465 keyed s16 rotation channels"

    Assert-Condition ($summary.counts.track_count -eq 46274) "expected 46274 exported bone tracks"
    Assert-Condition ($summary.counts.channel_count -eq 154303) "expected 154303 exported channels"
    Assert-Condition ($summary.counts.const_channel_count -eq 33998) "expected 33998 constant channels"
    Assert-Condition ($summary.counts.keyed_channel_count -eq 120305) "expected 120305 keyed channels"
    Assert-Condition ($summary.counts.keyframe_count -eq 1538116) "expected 1538116 keyframes"
    Assert-Condition ($summary.counts.frame_slot_count -eq 71831) "expected 71831 frame slots"
    Assert-Condition ($summary.counts.sampled_pose_frames -eq 71831) "expected 71831 sampled pose frames"
    Assert-Condition ($summary.counts.sampled_channel_values -eq 4957593) "expected 4957593 sampled channel values"
    Assert-Condition ($summary.counts.finite_world_matrix_entries -eq 27006064) "expected 27006064 finite world matrix entries"
    Assert-Condition ($summary.counts.non_f32_channel_blocks -eq 0) "expected 0 non-f32 blocks in exported tracks"

    Assert-Condition ($summary.unsupported_counts.non_f32_channel_blocks -eq 0) "expected 0 unsupported non-f32 channel samples"
    Assert-Condition ($summary.unsupported_counts.sampled_channel_values -eq $summary.unsupported_counts.finite_channel_values) "expected all supported-channel samples in unsupported records to be finite"
    Assert-Condition ($summary.unsupported_counts.world_matrix_entries -eq $summary.unsupported_counts.finite_world_matrix_entries) "expected all unsupported-record world matrices to stay finite"
}

Write-Host "OOT3D skinned animation batch manifest: $manifestOutput"
Write-Host "OOT3D skinned animation batch summary: $summaryOutput"
