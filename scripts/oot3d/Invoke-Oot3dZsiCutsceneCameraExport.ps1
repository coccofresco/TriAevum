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
        throw "OOT3D ZSI cutscene camera export verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "zsi_cutscene_camera_export"
$manifestOutput = Join-Path $outputRoot "zsi_cutscene_camera_export_manifest.json"
$summaryOutput = Join-Path $outputRoot "zsi_cutscene_camera_export_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $exportArgs = @(
        "export-zsi-cutscene-cameras",
        $SceneRoot,
        "--output",
        $outputRoot,
        "--sample-limit",
        ([string]$SampleLimit)
    )
    if ($NoRecords) {
        $exportArgs += "--no-records"
    }
    Invoke-Oot3dTool -Arguments $exportArgs
}
finally {
    Pop-Location
}

$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    scene_root = (Resolve-Path $SceneRoot).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    cutscene_output_dir = $manifest.cutscene_output_dir
    source_cutscene_command_total = $manifest.source_cutscene_command_total
    source_strict_n64_decoded_count = $manifest.source_strict_n64_decoded_count
    source_strict_n64_decode_error_count = $manifest.source_strict_n64_decode_error_count
    exported_cutscene_count = $manifest.exported_cutscene_count
    exported_command_count = $manifest.exported_command_count
    exported_camera_command_count = $manifest.exported_camera_command_count
    exported_camera_point_count = $manifest.exported_camera_point_count
    export_issue_count = $manifest.export_issue_count
    issue_counts = $manifest.issue_counts
    per_scene_counts = $manifest.per_scene_counts
    command_id_counts = $manifest.command_id_counts
    command_category_counts = $manifest.command_category_counts
    export_file_size_summary = $manifest.export_file_size_summary
    sample_record_count = $manifest.sample_records.Count
    record_count = $manifest.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.source_cutscene_command_total -eq 112) "expected 112 source cutscene command references"
    Assert-Condition ($summary.source_strict_n64_decoded_count -eq 9) "expected 9 strict N64-decodable source cutscene blocks"
    Assert-Condition ($summary.source_strict_n64_decode_error_count -eq 103) "expected 103 non-strict/N64 source cutscene blocks"
    Assert-Condition ($summary.exported_cutscene_count -eq 9) "expected 9 exported strict cutscene blocks"
    Assert-Condition ($summary.exported_command_count -eq 36) "expected 36 exported strict cutscene commands"
    Assert-Condition ($summary.exported_camera_command_count -eq 30) "expected 30 exported camera commands"
    Assert-Condition ($summary.exported_camera_point_count -eq 253) "expected 253 exported camera points"
    Assert-Condition ($summary.export_issue_count -eq 0) "expected 0 export validation issues"
    Assert-Condition ((Get-JsonValue $summary.issue_counts "none") -eq 9) "expected every exported cutscene block to validate"
    Assert-Condition ($summary.export_file_size_summary.count -eq 9) "expected 9 exported cutscene JSON files"

    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0001") -eq 14) "expected 14 CS_CMD_CAM_EYE commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x0002") -eq 15) "expected 15 CS_CMD_CAM_AT commands"
    Assert-Condition ((Get-JsonValue $summary.command_id_counts "0x03e8") -eq 1) "expected 1 CS_CMD_TERMINATOR command"
    Assert-Condition ($summary.command_category_counts.camera_list -eq 30) "expected 30 camera-list commands"
    Assert-Condition ($summary.command_category_counts.simple_16_byte -eq 1) "expected 1 simple 16-byte command"
    Assert-Condition ($summary.command_category_counts.twelve_word_entries -eq 5) "expected 5 12-word-entry commands"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D ZSI cutscene camera export manifest: $manifestOutput"
Write-Host "OOT3D ZSI cutscene camera export summary: $summaryOutput"
