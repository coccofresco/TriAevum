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
        throw "OOT3D skinned animation pilot verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$outputRoot = Join-Path $WorkRoot "skinned_animation_pilot"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$pilots = @(
    [ordered]@{
        id = "dekubaba_mode1"
        zar = Join-Path $RomFs "actor\zelda_dekubaba.zar"
        csab = "Anim\db_P_tobidasu.csab"
        cmb = "Model\dekubaba.cmb"
        output = Join-Path $outputRoot "dekubaba_db_P_tobidasu_tracks.json"
    },
    [ordered]@{
        id = "butterfly_mode2"
        zar = Join-Path $RomFs "actor\zelda_field_keep.zar"
        csab = "Anim\butterfly_fly.csab"
        cmb = "Model\butterfly.cmb"
        output = Join-Path $outputRoot "butterfly_fly_tracks.json"
    },
    [ordered]@{
        id = "kokiripeople_constant_index"
        zar = Join-Path $RomFs "actor\zelda_ec.zar"
        csab = "Anim\km1_dance.csab"
        cmb = "Model\kokiripeople.cmb"
        output = Join-Path $outputRoot "kokiripeople_km1_dance_tracks.json"
    }
)

foreach ($pilot in $pilots) {
    Require-Path $pilot.zar "Actor ZAR"
}

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    foreach ($pilot in $pilots) {
        Invoke-Oot3dTool -Arguments @(
            "export-csab-skeleton-tracks",
            $pilot.zar,
            "--csab-name",
            $pilot.csab,
            "--cmb-name",
            $pilot.cmb,
            "--output",
            $pilot.output
        )
    }
}
finally {
    Pop-Location
}

$records = @()
foreach ($pilot in $pilots) {
    $export = Get-Content -LiteralPath $pilot.output -Raw | ConvertFrom-Json
    $records += [ordered]@{
        id = $pilot.id
        actor_zar = (Resolve-Path $pilot.zar).Path
        csab_name = $pilot.csab
        cmb_name = $pilot.cmb
        export = $pilot.output
        format = $export.format
        target_model_name = $export.target_model_name
        target_support_status = $export.target_support_status
        frame_count_candidate = $export.frame_count_candidate
        frame_slot_count = $export.frame_slot_count
        animated_bone_count_candidate = $export.animated_bone_count_candidate
        skeleton_bone_count_candidate = $export.skeleton_bone_count_candidate
        counts = $export.counts
        validation = $export.validation.full_pose_sample
    }
}

$summaryOutput = Join-Path $outputRoot "skinned_animation_pilot_summary.json"
$summary = [ordered]@{
    format = "oot3d_skinned_animation_pilot_summary_v1"
    output_root = $outputRoot
    pilot_count = $records.Count
    records = $records
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.pilot_count -eq 3) "expected 3 skinned animation pilot exports"

    $mode1 = $summary.records[0]
    Assert-Condition ($mode1.format -eq "oot3d_csab_skeleton_track_export_v1") "mode1 export format mismatch"
    Assert-Condition ($mode1.target_model_name -eq "dekubaba") "expected dekubaba target"
    Assert-Condition ($mode1.target_support_status -eq "needs_skinning_mode_1_support") "expected dekubaba mode-1 skinning target"
    Assert-Condition ($mode1.frame_count_candidate -eq 24) "expected dekubaba frame count 24"
    Assert-Condition ($mode1.frame_slot_count -eq 25) "expected dekubaba 25 frame slots"
    Assert-Condition ($mode1.animated_bone_count_candidate -eq 3) "expected dekubaba 3 animated bones"
    Assert-Condition ($mode1.skeleton_bone_count_candidate -eq 3) "expected dekubaba 3 skeleton bones"
    Assert-Condition ($mode1.counts.track_count -eq 3) "expected dekubaba 3 tracks"
    Assert-Condition ($mode1.counts.channel_count -eq 12) "expected dekubaba 12 channels"
    Assert-Condition ($mode1.counts.const_channel_count -eq 8) "expected dekubaba 8 const channels"
    Assert-Condition ($mode1.counts.keyed_channel_count -eq 4) "expected dekubaba 4 keyed channels"
    Assert-Condition ($mode1.counts.keyframe_count -eq 42) "expected dekubaba 42 keyframes"
    Assert-Condition ($mode1.validation.valid) "expected dekubaba full-frame validation"
    Assert-Condition ($mode1.validation.finite_world_matrix_entries -eq 1200) "expected dekubaba finite world matrices"
    Assert-Condition ($mode1.validation.non_f32_channel_blocks -eq 0) "expected dekubaba f32-only channels"

    $mode2 = $summary.records[1]
    Assert-Condition ($mode2.target_model_name -eq "butterfly") "expected butterfly target"
    Assert-Condition ($mode2.target_support_status -eq "needs_skinning_mode_2_support") "expected butterfly mode-2 skinning target"
    Assert-Condition ($mode2.frame_count_candidate -eq 9) "expected butterfly frame count 9"
    Assert-Condition ($mode2.frame_slot_count -eq 10) "expected butterfly 10 frame slots"
    Assert-Condition ($mode2.counts.track_count -eq 3) "expected butterfly 3 tracks"
    Assert-Condition ($mode2.counts.channel_count -eq 8) "expected butterfly 8 channels"
    Assert-Condition ($mode2.counts.const_channel_count -eq 0) "expected butterfly 0 const channels"
    Assert-Condition ($mode2.counts.keyed_channel_count -eq 8) "expected butterfly 8 keyed channels"
    Assert-Condition ($mode2.counts.keyframe_count -eq 76) "expected butterfly 76 keyframes"
    Assert-Condition ($mode2.validation.valid) "expected butterfly full-frame validation"
    Assert-Condition ($mode2.validation.finite_world_matrix_entries -eq 480) "expected butterfly finite world matrices"
    Assert-Condition ($mode2.validation.non_f32_channel_blocks -eq 0) "expected butterfly f32-only channels"

    $constant = $summary.records[2]
    Assert-Condition ($constant.target_model_name -eq "kokiripeople") "expected kokiripeople target"
    Assert-Condition ($constant.target_support_status -eq "needs_skinning_mode_2_support") "expected kokiripeople mode-2 skinning target"
    Assert-Condition ($constant.frame_count_candidate -eq 19) "expected kokiripeople frame count 19"
    Assert-Condition ($constant.frame_slot_count -eq 20) "expected kokiripeople 20 frame slots"
    Assert-Condition ($constant.animated_bone_count_candidate -eq 18) "expected kokiripeople 18 animated bones"
    Assert-Condition ($constant.skeleton_bone_count_candidate -eq 19) "expected kokiripeople 19 skeleton bones"
    Assert-Condition ($constant.counts.track_count -eq 18) "expected kokiripeople 18 tracks"
    Assert-Condition ($constant.counts.channel_count -eq 56) "expected kokiripeople 56 channels"
    Assert-Condition ($constant.counts.const_channel_count -eq 6) "expected kokiripeople 6 const channels"
    Assert-Condition ($constant.counts.keyed_channel_count -eq 50) "expected kokiripeople 50 keyed channels"
    Assert-Condition ($constant.counts.keyframe_count -eq 859) "expected kokiripeople 859 keyframes"
    Assert-Condition ($constant.validation.valid) "expected kokiripeople full-frame validation"
    Assert-Condition ($constant.validation.finite_world_matrix_entries -eq 6080) "expected kokiripeople finite world matrices"
    Assert-Condition ($constant.validation.non_f32_channel_blocks -eq 0) "expected kokiripeople f32-only channels"
}

Write-Host "OOT3D skinned animation pilot summary: $summaryOutput"
