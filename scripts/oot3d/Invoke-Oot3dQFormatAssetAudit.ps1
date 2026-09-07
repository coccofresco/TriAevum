param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
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
        throw "OOT3D Q-format asset audit verification failed: $Message"
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
Require-Path $RomFs "OOT3D RomFS"

$outputRoot = Join-Path $WorkRoot "q_format_asset_audit"
$auditOutput = Join-Path $outputRoot "oot3d_q_format_asset_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_q_format_asset_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-q-format-assets",
        $RomFs,
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
    romfs_root = (Resolve-Path $RomFs).Path
    output_root = $outputRoot
    audit = $auditOutput
    file_count = $audit.file_count
    total_size = $audit.total_size
    extension_counts = $audit.extension_counts
    category_counts = $audit.category_counts
    top_level_counts = $audit.top_level_counts
    parent_dir_counts = $audit.parent_dir_counts
    role_counts = $audit.role_counts
    language_counts = $audit.language_counts
    screen_counts = $audit.screen_counts
    group_kind_counts = $audit.group_kind_counts
    group_completion_counts = $audit.group_completion_counts
    screen_layout_set_completion_counts = $audit.screen_layout_set_completion_counts
    size_summary_by_extension = $audit.size_summary_by_extension
    magic4_counts = $audit.magic4_counts
    qbf_font_records = $audit.qbf_font_records
    issue_count = $audit.issues.Count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.file_count -eq 52) "expected 52 Q-format assets"
    Assert-Condition ($summary.total_size -eq 575764) "expected total Q-format byte size 575764"

    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qan") -eq 12) "expected 12 QAN files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qbf") -eq 2) "expected 2 QBF files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qbr") -eq 1) "expected 1 QBR file"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qcl") -eq 1) "expected 1 QCL file"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qhm") -eq 1) "expected 1 QHM file"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qly") -eq 23) "expected 23 QLY files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qsp") -eq 12) "expected 12 QSP files"

    Assert-Condition ($summary.category_counts.layout_animation -eq 12) "expected 12 layout animation assets"
    Assert-Condition ($summary.category_counts.layout -eq 23) "expected 23 layout assets"
    Assert-Condition ($summary.category_counts.sprite -eq 12) "expected 12 sprite assets"
    Assert-Condition ($summary.category_counts.bitmap_font -eq 2) "expected 2 bitmap font assets"
    Assert-Condition ($summary.category_counts.layout_color -eq 1) "expected 1 layout color asset"
    Assert-Condition ($summary.category_counts.boss_rush_metadata -eq 1) "expected 1 boss-rush metadata asset"
    Assert-Condition ($summary.category_counts.hint_metadata -eq 1) "expected 1 hint metadata asset"

    Assert-Condition ($summary.top_level_counts.message -eq 6) "expected 6 message Q-format assets"
    Assert-Condition ($summary.top_level_counts.misc -eq 46) "expected 46 misc Q-format assets"

    Assert-Condition ($summary.group_kind_counts.message_system -eq 1) "expected 1 message-system group"
    Assert-Condition ($summary.group_kind_counts.screen_layout_set -eq 11) "expected 11 screen layout groups"
    Assert-Condition ($summary.group_kind_counts.metadata_singleton -eq 2) "expected 2 metadata singleton groups"
    Assert-Condition ($summary.group_completion_counts.complete -eq 14) "expected 14 complete Q-format groups"
    Assert-Condition ($summary.screen_layout_set_completion_counts.complete -eq 11) "expected 11 complete screen layout groups"
    Assert-Condition ($summary.issue_count -eq 0) "expected 0 Q-format audit issues"

    foreach ($language in @("english", "french", "german", "italian", "spanish")) {
        Assert-Condition ((Get-JsonValue $summary.language_counts $language) -eq 8) "expected 8 localized Q-format files for $language"
    }
    Assert-Condition ($summary.screen_counts.challenge -eq 20) "expected 20 challenge screen files"
    Assert-Condition ($summary.screen_counts.gameover -eq 20) "expected 20 gameover screen files"
    Assert-Condition ($summary.screen_counts.ending -eq 4) "expected 4 ending screen files"
    Assert-Condition ($summary.screen_counts.message -eq 6) "expected 6 message screen files"
    Assert-Condition ($summary.screen_counts.bossRush -eq 1) "expected 1 bossRush metadata screen file"
    Assert-Condition ($summary.screen_counts.hint -eq 1) "expected 1 hint metadata screen file"

    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".qan").total -eq 126508) "expected QAN total byte size 126508"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".qbf").total -eq 55128) "expected QBF total byte size 55128"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".qly").total -eq 259072) "expected QLY total byte size 259072"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".qsp").total -eq 122880) "expected QSP total byte size 122880"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".qbf") "ascii:QBF1") -eq 2) "expected all QBF magic4 signatures"

    $sys8 = $summary.qbf_font_records | Where-Object { $_.path -eq "message/sys8.qbf" } | Select-Object -First 1
    $ltn16 = $summary.qbf_font_records | Where-Object { $_.path -eq "message/eu/ltn16.qbf" } | Select-Object -First 1
    Assert-Condition ($null -ne $sys8) "expected sys8 QBF font record"
    Assert-Condition ($null -ne $ltn16) "expected ltn16 QBF font record"
    Assert-Condition ($sys8.texture_width_candidate_le -eq 288) "expected sys8 width candidate 288"
    Assert-Condition ($sys8.texture_height_candidate_le -eq 192) "expected sys8 height candidate 192"
    Assert-Condition ($sys8.glyph_count_candidate_le -eq 42) "expected sys8 glyph count candidate 42"
    Assert-Condition ($ltn16.texture_width_candidate_le -eq 199) "expected ltn16 width candidate 199"
    Assert-Condition ($ltn16.texture_height_candidate_le -eq 208) "expected ltn16 height candidate 208"
    Assert-Condition ($ltn16.glyph_count_candidate_le -eq 42) "expected ltn16 glyph count candidate 42"

    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D Q-format asset audit: $auditOutput"
Write-Host "OOT3D Q-format asset summary: $summaryOutput"
