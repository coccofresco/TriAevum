param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$SceneRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 100,
    [switch]$NoRecords,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($SceneRoot)) {
    $SceneRoot = Join-Path $RomFs "scene"
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
        throw "OOT3D ZSI cutscene audit verification failed: $Message"
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
Require-Path $SceneRoot "OOT3D scene directory"

$outputRoot = Join-Path $WorkRoot "zsi_cutscene_metadata_audit"
$auditOutput = Join-Path $outputRoot "oot3d_zsi_cutscene_metadata_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_zsi_cutscene_metadata_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-zsi-cutscene-metadata",
        $SceneRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        ([string]$SampleLimit)
    )
    if ($NoRecords) {
        $auditArgs += "--no-records"
    }
    Invoke-Oot3dTool -Arguments $auditArgs
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    scene_root = (Resolve-Path $SceneRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    zsi_file_count = $audit.zsi_file_count
    role_counts = $audit.role_counts
    cutscene_file_count = $audit.cutscene_file_count
    cutscene_role_counts = $audit.cutscene_role_counts
    cutscene_setup_count = $audit.cutscene_setup_count
    cutscene_command_total = $audit.cutscene_command_total
    unique_cutscene_data_offset_count = $audit.unique_cutscene_data_offset_count
    argument_in_bounds_count = $audit.argument_in_bounds_count
    argument_out_of_bounds_count = $audit.argument_out_of_bounds_count
    cutscene_setup_count_counts = $audit.cutscene_setup_count_counts
    header_candidate_counts_by_delta = $audit.header_candidate_counts_by_delta
    strict_n64_decoded_count = $audit.strict_n64_decoded_count
    strict_n64_decode_error_count = $audit.strict_n64_decode_error_count
    strict_n64_decode_counts_by_delta = $audit.strict_n64_decode_counts_by_delta
    strict_n64_command_total = $audit.strict_n64_command_total
    strict_n64_camera_point_total = $audit.strict_n64_camera_point_total
    strict_n64_command_id_counts = $audit.strict_n64_command_id_counts
    strict_n64_command_category_counts = $audit.strict_n64_command_category_counts
    parse_error_count = $audit.parse_error_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.zsi_file_count -eq 724) "expected 724 scene ZSI files"
    Assert-Condition ($summary.role_counts.scene -eq 114) "expected 114 scene-level ZSI files"
    Assert-Condition ($summary.role_counts.room -eq 610) "expected 610 room ZSI files"
    Assert-Condition ($summary.cutscene_file_count -eq 33) "expected 33 scene files with cutscene command data"
    Assert-Condition ($summary.cutscene_role_counts.scene -eq 33) "expected all cutscene command files to be scene-level ZSI files"
    Assert-Condition ($summary.cutscene_setup_count -eq 112) "expected 112 setups with cutscene command data"
    Assert-Condition ($summary.cutscene_command_total -eq 112) "expected 112 scene command 0x17 references"
    Assert-Condition ($summary.unique_cutscene_data_offset_count -eq 112) "expected 112 unique cutscene data offsets"
    Assert-Condition ($summary.argument_in_bounds_count -eq 112) "expected every cutscene data offset to be in-bounds"
    Assert-Condition ($summary.argument_out_of_bounds_count -eq 0) "expected no out-of-bounds cutscene data offsets"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "1") -eq 9) "expected 9 cutscene files with one setup reference"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "2") -eq 7) "expected 7 cutscene files with two setup references"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "3") -eq 10) "expected 10 cutscene files with three setup references"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "5") -eq 2) "expected 2 cutscene files with five setup references"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "8") -eq 1) "expected 1 cutscene file with eight setup references"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "10") -eq 3) "expected 3 cutscene files with ten setup references"
    Assert-Condition ((Get-JsonValue $summary.cutscene_setup_count_counts "11") -eq 1) "expected 1 cutscene file with eleven setup references"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x14") -eq 110) "expected 110 plausible +0x14 headers"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x18") -eq 109) "expected 109 plausible +0x18 headers"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x20") -eq 89) "expected 89 plausible +0x20 headers"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x24") -eq 81) "expected 81 plausible +0x24 headers"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x28") -eq 78) "expected 78 plausible +0x28 headers"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x2c") -eq 8) "expected 8 plausible +0x2c headers"
    Assert-Condition ((Get-JsonValue $summary.header_candidate_counts_by_delta "+0x30") -eq 16) "expected 16 plausible +0x30 headers"
    Assert-Condition ($summary.strict_n64_decoded_count -eq 9) "expected 9 strict N64-decodable cutscene blocks"
    Assert-Condition ($summary.strict_n64_decode_error_count -eq 103) "expected 103 non-strict/N64 decode queue records"
    Assert-Condition ((Get-JsonValue $summary.strict_n64_decode_counts_by_delta "+0x18") -eq 6) "expected 6 strict decodes at +0x18"
    Assert-Condition ((Get-JsonValue $summary.strict_n64_decode_counts_by_delta "+0x20") -eq 1) "expected 1 strict decode at +0x20"
    Assert-Condition ((Get-JsonValue $summary.strict_n64_decode_counts_by_delta "+0x24") -eq 2) "expected 2 strict decodes at +0x24"
    Assert-Condition ($summary.strict_n64_command_total -eq 36) "expected 36 strict decoded cutscene commands"
    Assert-Condition ($summary.strict_n64_camera_point_total -eq 253) "expected 253 strict decoded camera points"
    Assert-Condition ((Get-JsonValue $summary.strict_n64_command_id_counts "0x0001") -eq 14) "expected 14 CS_CMD_CAM_EYE commands"
    Assert-Condition ((Get-JsonValue $summary.strict_n64_command_id_counts "0x0002") -eq 15) "expected 15 CS_CMD_CAM_AT commands"
    Assert-Condition ((Get-JsonValue $summary.strict_n64_command_id_counts "0x03e8") -eq 1) "expected 1 CS_CMD_TERMINATOR command"
    Assert-Condition ($summary.strict_n64_command_category_counts.camera_list -eq 30) "expected 30 strict camera-list commands"
    Assert-Condition ($summary.strict_n64_command_category_counts.simple_16_byte -eq 1) "expected 1 strict simple 16-byte command"
    Assert-Condition ($summary.strict_n64_command_category_counts.twelve_word_entries -eq 5) "expected 5 strict 12-word-entry commands"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 ZSI cutscene audit parse errors"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D ZSI cutscene metadata audit: $auditOutput"
Write-Host "OOT3D ZSI cutscene metadata summary: $summaryOutput"
