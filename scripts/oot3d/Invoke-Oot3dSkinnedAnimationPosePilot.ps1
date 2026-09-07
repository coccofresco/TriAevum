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
        throw "OOT3D skinned animation pose pilot verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$outputRoot = Join-Path $WorkRoot "skinned_animation_pose_pilot"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$pilots = @(
    [ordered]@{
        id = "dekubaba_mode1"
        zar = Join-Path $RomFs "actor\zelda_dekubaba.zar"
        csab = "Anim\db_P_tobidasu.csab"
        cmb = "Model\dekubaba.cmb"
        frames = "0,12,24"
        output = Join-Path $outputRoot "dekubaba_pose_sample.json"
    },
    [ordered]@{
        id = "butterfly_mode2"
        zar = Join-Path $RomFs "actor\zelda_field_keep.zar"
        csab = "Anim\butterfly_fly.csab"
        cmb = "Model\butterfly.cmb"
        frames = "0,4,9"
        output = Join-Path $outputRoot "butterfly_pose_sample.json"
    },
    [ordered]@{
        id = "kokiripeople_constant_index"
        zar = Join-Path $RomFs "actor\zelda_ec.zar"
        csab = "Anim\km1_dance.csab"
        cmb = "Model\kokiripeople.cmb"
        frames = "0,9,19"
        output = Join-Path $outputRoot "kokiripeople_pose_sample.json"
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
            "export-skinned-animation-pose-samples",
            $pilot.zar,
            "--csab-name",
            $pilot.csab,
            "--cmb-name",
            $pilot.cmb,
            "--frames",
            $pilot.frames,
            "--output",
            $pilot.output,
            "--sample-limit",
            "25"
        )
    }
}
finally {
    Pop-Location
}

$records = @()
$totals = [ordered]@{
    source_vertex_rows = 0
    sampled_vertex_rows = 0
    finite_position_rows = 0
    finite_normal_rows = 0
    changed_position_rows = 0
    validation_error_count = 0
    skinned_primitive_count = 0
    mode_1_primitive_count = 0
    mode_2_primitive_count = 0
    sampled_channel_values = 0
    finite_world_matrix_entries = 0
    non_f32_channel_blocks = 0
}

foreach ($pilot in $pilots) {
    $export = Get-Content -LiteralPath $pilot.output -Raw | ConvertFrom-Json
    $record = [ordered]@{
        id = $pilot.id
        actor_zar = (Resolve-Path $pilot.zar).Path
        csab_name = $pilot.csab
        cmb_name = $pilot.cmb
        frames = $pilot.frames
        export = $pilot.output
        format = $export.format
        target_model_name = $export.target_model_name
        target_support_status = $export.target_support_status
        frame_count_candidate = $export.frame_count_candidate
        sample_frames = $export.sample_frames
        bind_pose_counts = $export.bind_pose_counts
        pose_counts = $export.pose_counts
        counts = $export.counts
        validation = $export.validation
    }
    $records += $record

    $totals.source_vertex_rows += $export.counts.source_vertex_rows
    $totals.sampled_vertex_rows += $export.counts.sampled_vertex_rows
    $totals.finite_position_rows += $export.counts.finite_position_rows
    $totals.finite_normal_rows += $export.counts.finite_normal_rows
    $totals.changed_position_rows += $export.counts.changed_position_rows
    $totals.validation_error_count += $export.counts.validation_error_count
    $totals.skinned_primitive_count += $export.bind_pose_counts.skinned_primitive_count
    $totals.mode_1_primitive_count += $export.bind_pose_counts.mode_1_primitive_count
    $totals.mode_2_primitive_count += $export.bind_pose_counts.mode_2_primitive_count
    $totals.sampled_channel_values += $export.pose_counts.sampled_channel_values
    $totals.finite_world_matrix_entries += $export.pose_counts.finite_world_matrix_entries
    $totals.non_f32_channel_blocks += $export.pose_counts.non_f32_channel_blocks
}

$summaryOutput = Join-Path $outputRoot "skinned_animation_pose_pilot_summary.json"
$summary = [ordered]@{
    format = "oot3d_skinned_animation_pose_pilot_summary_v1"
    output_root = $outputRoot
    pilot_count = $records.Count
    totals = $totals
    records = $records
}
$summary | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.pilot_count -eq 3) "expected 3 skinned animation pose pilots"
    Assert-Condition ($summary.totals.source_vertex_rows -eq 962) "expected 962 source skinned vertex rows"
    Assert-Condition ($summary.totals.sampled_vertex_rows -eq 2886) "expected 2886 sampled vertex rows"
    Assert-Condition ($summary.totals.finite_position_rows -eq 2886) "expected 2886 finite sampled positions"
    Assert-Condition ($summary.totals.finite_normal_rows -eq 2886) "expected 2886 finite sampled normals"
    Assert-Condition ($summary.totals.changed_position_rows -eq 2886) "expected all sampled rows to differ from bind pose"
    Assert-Condition ($summary.totals.validation_error_count -eq 0) "expected 0 deformation validation errors"
    Assert-Condition ($summary.totals.skinned_primitive_count -eq 13) "expected 13 skinned primitives"
    Assert-Condition ($summary.totals.mode_1_primitive_count -eq 1) "expected 1 mode-1 primitive"
    Assert-Condition ($summary.totals.mode_2_primitive_count -eq 12) "expected 12 mode-2 primitives"
    Assert-Condition ($summary.totals.sampled_channel_values -eq 228) "expected 228 sampled CSAB channel values"
    Assert-Condition ($summary.totals.finite_world_matrix_entries -eq 1200) "expected 1200 finite pose matrix entries"
    Assert-Condition ($summary.totals.non_f32_channel_blocks -eq 0) "expected 0 unsupported channel blocks"

    $mode1 = $summary.records[0]
    Assert-Condition ($mode1.format -eq "oot3d_skinned_animation_pose_sample_v1") "mode1 export format mismatch"
    Assert-Condition ($mode1.target_model_name -eq "dekubaba") "expected dekubaba target"
    Assert-Condition ($mode1.target_support_status -eq "needs_skinning_mode_1_support") "expected dekubaba mode-1 target"
    Assert-Condition ($mode1.frame_count_candidate -eq 24) "expected dekubaba frame count 24"
    Assert-Condition ($mode1.bind_pose_counts.skinned_vertex_rows -eq 91) "expected dekubaba 91 skinned rows"
    Assert-Condition ($mode1.counts.sampled_vertex_rows -eq 273) "expected dekubaba 273 sampled rows"
    Assert-Condition ($mode1.pose_counts.finite_world_matrix_entries -eq 144) "expected dekubaba 144 finite matrix entries"
    Assert-Condition ($mode1.validation.valid) "expected dekubaba validation"

    $mode2 = $summary.records[1]
    Assert-Condition ($mode2.target_model_name -eq "butterfly") "expected butterfly target"
    Assert-Condition ($mode2.target_support_status -eq "needs_skinning_mode_2_support") "expected butterfly mode-2 target"
    Assert-Condition ($mode2.frame_count_candidate -eq 9) "expected butterfly frame count 9"
    Assert-Condition ($mode2.bind_pose_counts.skinned_vertex_rows -eq 28) "expected butterfly 28 skinned rows"
    Assert-Condition ($mode2.counts.sampled_vertex_rows -eq 84) "expected butterfly 84 sampled rows"
    Assert-Condition ($mode2.validation.valid) "expected butterfly validation"

    $constant = $summary.records[2]
    Assert-Condition ($constant.target_model_name -eq "kokiripeople") "expected kokiripeople target"
    Assert-Condition ($constant.target_support_status -eq "needs_skinning_mode_2_support") "expected kokiripeople mode-2 target"
    Assert-Condition ($constant.frame_count_candidate -eq 19) "expected kokiripeople frame count 19"
    Assert-Condition ($constant.bind_pose_counts.skinned_vertex_rows -eq 843) "expected kokiripeople 843 skinned rows"
    Assert-Condition ($constant.counts.sampled_vertex_rows -eq 2529) "expected kokiripeople 2529 sampled rows"
    Assert-Condition ($constant.pose_counts.finite_world_matrix_entries -eq 912) "expected kokiripeople 912 finite matrix entries"
    Assert-Condition ($constant.validation.valid) "expected kokiripeople validation"
}

Write-Host "OOT3D skinned animation pose pilot summary: $summaryOutput"
