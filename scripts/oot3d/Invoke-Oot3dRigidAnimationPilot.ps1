param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ActorZar = "",
    [string]$CsabName = "Anim/cdemo_box_boxA.csab",
    [string]$CmbName = "Model/tr_box.cmb",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($ActorZar)) {
    $ActorZar = Join-Path $RomFs "actor\zelda_box.zar"
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
        throw "OOT3D rigid animation pilot verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorZar "Actor ZAR"

$outputRoot = Join-Path $WorkRoot "rigid_animation_pilot"
$trackOutput = Join-Path $outputRoot "cdemo_box_boxA_tracks.json"
$summaryOutput = Join-Path $outputRoot "rigid_animation_pilot_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-csab-rigid-tracks",
        $ActorZar,
        "--csab-name",
        $CsabName,
        "--cmb-name",
        $CmbName,
        "--output",
        $trackOutput
    )
}
finally {
    Pop-Location
}

$trackExport = Get-Content -LiteralPath $trackOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_zar = (Resolve-Path $ActorZar).Path
    csab_name = $CsabName
    cmb_name = $CmbName
    track_export = $trackOutput
    format = $trackExport.format
    frame_count_candidate = $trackExport.frame_count_candidate
    frame_slot_count = $trackExport.frame_slot_count
    track_count = $trackExport.counts.track_count
    channel_count = $trackExport.counts.channel_count
    const_channel_count = $trackExport.counts.const_channel_count
    keyed_channel_count = $trackExport.counts.keyed_channel_count
    keyframe_count = $trackExport.counts.keyframe_count
    validation = [ordered]@{
        full_pose_valid = $trackExport.validation.full_pose_sample.valid
        sampled_pose_frames = $trackExport.validation.full_pose_sample.sampled_pose_frames
        sampled_channel_values = $trackExport.validation.full_pose_sample.sampled_channel_values
        finite_world_matrix_entries = $trackExport.validation.full_pose_sample.finite_world_matrix_entries
        non_f32_channel_blocks = $trackExport.validation.full_pose_sample.non_f32_channel_blocks
    }
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.format -eq "oot3d_csab_rigid_track_export_v1") "unexpected track export format"
    Assert-Condition ($summary.frame_count_candidate -eq 130) "expected frame count 130"
    Assert-Condition ($summary.frame_slot_count -eq 131) "expected 131 frame slots"
    Assert-Condition ($summary.track_count -eq 3) "expected 3 exported bone tracks"
    Assert-Condition ($summary.channel_count -eq 18) "expected 18 exported f32 channels"
    Assert-Condition ($summary.const_channel_count -eq 17) "expected 17 constant f32 channels"
    Assert-Condition ($summary.keyed_channel_count -eq 1) "expected 1 keyed f32 channel"
    Assert-Condition ($summary.keyframe_count -eq 28) "expected 28 keyed f32 frames"
    Assert-Condition ($summary.validation.full_pose_valid) "expected full-frame pose validation to pass"
    Assert-Condition ($summary.validation.sampled_pose_frames -eq 131) "expected 131 validated full-frame poses"
    Assert-Condition ($summary.validation.sampled_channel_values -eq 2358) "expected 2358 sampled channel values"
    Assert-Condition ($summary.validation.finite_world_matrix_entries -eq 6288) "expected 6288 finite world matrix entries"
    Assert-Condition ($summary.validation.non_f32_channel_blocks -eq 0) "expected 0 non-f32 channel blocks"
}

Write-Host "OOT3D rigid animation track export: $trackOutput"
Write-Host "OOT3D rigid animation pilot summary: $summaryOutput"
