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

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D skinned bind-pose pilot verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$outputRoot = Join-Path $WorkRoot "skinned_bind_pose_pilot"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$pilots = @(
    [ordered]@{
        id = "dekubaba_mode1"
        zar = Join-Path $RomFs "actor\zelda_dekubaba.zar"
        cmb = "Model\dekubaba.cmb"
        output = Join-Path $outputRoot "dekubaba_skinned_bind_pose.json"
    },
    [ordered]@{
        id = "butterfly_mode2"
        zar = Join-Path $RomFs "actor\zelda_field_keep.zar"
        cmb = "Model\butterfly.cmb"
        output = Join-Path $outputRoot "butterfly_skinned_bind_pose.json"
    },
    [ordered]@{
        id = "kokiripeople_constant_index"
        zar = Join-Path $RomFs "actor\zelda_ec.zar"
        cmb = "Model\kokiripeople.cmb"
        output = Join-Path $outputRoot "kokiripeople_skinned_bind_pose.json"
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
            "export-skinned-bind-pose",
            $pilot.zar,
            "--cmb-name",
            $pilot.cmb,
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
foreach ($pilot in $pilots) {
    $export = Get-Content -LiteralPath $pilot.output -Raw | ConvertFrom-Json
    $records += [ordered]@{
        id = $pilot.id
        actor_zar = (Resolve-Path $pilot.zar).Path
        cmb_name = $pilot.cmb
        export = $pilot.output
        format = $export.format
        model_name = $export.model_name
        bone_count = $export.bone_count
        counts = $export.counts
        sample_count = $export.vertex_samples.Count
    }
}

$summaryOutput = Join-Path $outputRoot "skinned_bind_pose_pilot_summary.json"
$summary = [ordered]@{
    format = "oot3d_skinned_bind_pose_pilot_summary_v1"
    output_root = $outputRoot
    pilot_count = $records.Count
    records = $records
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.pilot_count -eq 3) "expected 3 skinned pilot exports"

    $mode1 = $summary.records[0]
    Assert-Condition ($mode1.format -eq "oot3d_skinned_bind_pose_export_v1") "mode1 export format mismatch"
    Assert-Condition ($mode1.model_name -eq "dekubaba") "expected dekubaba mode1 model"
    Assert-Condition ($mode1.bone_count -eq 3) "expected dekubaba bone count 3"
    Assert-Condition ($mode1.counts.mesh_count -eq 1) "expected dekubaba 1 skinned mesh"
    Assert-Condition ($mode1.counts.skinned_primitive_count -eq 1) "expected dekubaba 1 skinned primitive"
    Assert-Condition ($mode1.counts.mode_1_primitive_count -eq 1) "expected dekubaba mode-1 primitive"
    Assert-Condition ($mode1.counts.mode_2_primitive_count -eq 0) "expected dekubaba 0 mode-2 primitives"
    Assert-Condition ($mode1.counts.skinned_vertex_rows -eq 91) "expected dekubaba 91 skinned vertex rows"
    Assert-Condition ((Get-JsonValue $mode1.counts.influence_width_rows "1") -eq 91) "expected dekubaba width-1 rows"
    Assert-Condition ((Get-JsonValue $mode1.counts.nonzero_influence_rows "1") -eq 91) "expected dekubaba one nonzero influence per row"
    Assert-Condition ($mode1.counts.skeleton_bone_count -eq 3) "expected dekubaba 3 skeleton bones"
    Assert-Condition ($mode1.counts.finite_bind_world_matrix_entries -eq 48) "expected dekubaba 48 finite bind matrix entries"
    Assert-Condition ($mode1.counts.validation_error_count -eq 0) "expected dekubaba 0 validation errors"

    $mode2 = $summary.records[1]
    Assert-Condition ($mode2.model_name -eq "butterfly") "expected butterfly mode2 model"
    Assert-Condition ($mode2.bone_count -eq 3) "expected butterfly bone count 3"
    Assert-Condition ($mode2.counts.skinned_primitive_count -eq 1) "expected butterfly 1 skinned primitive"
    Assert-Condition ($mode2.counts.mode_1_primitive_count -eq 0) "expected butterfly 0 mode-1 primitives"
    Assert-Condition ($mode2.counts.mode_2_primitive_count -eq 1) "expected butterfly mode-2 primitive"
    Assert-Condition ($mode2.counts.skinned_vertex_rows -eq 28) "expected butterfly 28 skinned vertex rows"
    Assert-Condition ((Get-JsonValue $mode2.counts.influence_width_rows "2") -eq 28) "expected butterfly width-2 rows"
    Assert-Condition ((Get-JsonValue $mode2.counts.nonzero_influence_rows "1") -eq 24) "expected butterfly 24 single-weight rows"
    Assert-Condition ((Get-JsonValue $mode2.counts.nonzero_influence_rows "2") -eq 4) "expected butterfly 4 blended rows"
    Assert-Condition ($mode2.counts.skeleton_bone_count -eq 3) "expected butterfly 3 skeleton bones"
    Assert-Condition ($mode2.counts.finite_bind_world_matrix_entries -eq 48) "expected butterfly 48 finite bind matrix entries"
    Assert-Condition ($mode2.counts.validation_error_count -eq 0) "expected butterfly 0 validation errors"

    $constant = $summary.records[2]
    Assert-Condition ($constant.model_name -eq "kokiripeople") "expected kokiripeople constant-index model"
    Assert-Condition ($constant.counts.skinned_primitive_count -eq 11) "expected kokiripeople 11 skinned primitives"
    Assert-Condition ($constant.counts.mode_2_primitive_count -eq 11) "expected kokiripeople mode-2 primitives"
    Assert-Condition ($constant.counts.skinned_vertex_rows -eq 843) "expected kokiripeople 843 skinned vertex rows"
    Assert-Condition ((Get-JsonValue $constant.counts.influence_width_rows "2") -eq 323) "expected kokiripeople width-2 rows"
    Assert-Condition ((Get-JsonValue $constant.counts.influence_width_rows "3") -eq 476) "expected kokiripeople width-3 rows"
    Assert-Condition ((Get-JsonValue $constant.counts.influence_width_rows "4") -eq 44) "expected kokiripeople width-4 rows"
    Assert-Condition ((Get-JsonValue $constant.counts.nonzero_influence_rows "1") -eq 487) "expected kokiripeople one-weight rows"
    Assert-Condition ((Get-JsonValue $constant.counts.nonzero_influence_rows "2") -eq 273) "expected kokiripeople two-weight rows"
    Assert-Condition ((Get-JsonValue $constant.counts.nonzero_influence_rows "3") -eq 83) "expected kokiripeople three-weight rows"
    Assert-Condition ($constant.counts.skeleton_bone_count -eq 19) "expected kokiripeople 19 skeleton bones"
    Assert-Condition ($constant.counts.finite_bind_world_matrix_entries -eq 304) "expected kokiripeople 304 finite bind matrix entries"
    Assert-Condition ($constant.counts.validation_error_count -eq 0) "expected kokiripeople 0 validation errors"
}

Write-Host "OOT3D skinned bind-pose pilot summary: $summaryOutput"
