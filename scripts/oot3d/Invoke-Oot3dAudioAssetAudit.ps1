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
        throw "OOT3D audio asset audit verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "audio_asset_audit"
$auditOutput = Join-Path $outputRoot "oot3d_audio_asset_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_audio_asset_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-audio-assets",
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
    size_summary_by_extension = $audit.size_summary_by_extension
    magic4_counts = $audit.magic4_counts
    bom_counts = $audit.bom_counts
    byte_order_counts = $audit.byte_order_counts
    version_counts = $audit.version_counts
    header_size_counts = $audit.header_size_counts
    section_count_counts = $audit.section_count_counts
    section_type_counts = $audit.section_type_counts
    section_magic_counts = $audit.section_magic_counts
    section_layout_counts = $audit.section_layout_counts
    section_size_summary_by_type = $audit.section_size_summary_by_type
    declared_size_match_count = $audit.declared_size_match_count
    section_record_count = $audit.section_record_count
    section_declared_size_match_count = $audit.section_declared_size_match_count
    issue_count = $audit.issue_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.file_count -eq 3) "expected 3 audio assets"
    Assert-Condition ($summary.total_size -eq 12734648) "expected total audio byte size 12734648"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".bcsar") -eq 2) "expected 2 BCSAR files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".bcstm") -eq 1) "expected 1 BCSTM file"
    Assert-Condition ($summary.category_counts.audio_archive -eq 2) "expected 2 audio archive assets"
    Assert-Condition ($summary.category_counts.audio_stream -eq 1) "expected 1 audio stream asset"
    Assert-Condition ($summary.top_level_counts.sound -eq 3) "expected all audio assets under sound"
    Assert-Condition ($summary.parent_dir_counts.sound -eq 2) "expected 2 sound root audio assets"
    Assert-Condition ((Get-JsonValue $summary.parent_dir_counts "sound/stream") -eq 1) "expected 1 sound/stream audio asset"

    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".bcsar") "ascii:CSAR") -eq 2) "expected all BCSAR magic4 signatures"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".bcstm") "ascii:CSTM") -eq 1) "expected all BCSTM magic4 signatures"
    Assert-Condition ((Get-JsonValue $summary.bom_counts "fffe") -eq 3) "expected all audio assets to use fffe BOM"
    Assert-Condition ($summary.byte_order_counts.little -eq 3) "expected all audio assets to be little endian"
    Assert-Condition ((Get-JsonValue $summary.version_counts "0x02000000") -eq 3) "expected all audio assets to use version 0x02000000"
    Assert-Condition ((Get-JsonValue $summary.header_size_counts "64") -eq 3) "expected all audio headers to be 64 bytes"
    Assert-Condition ((Get-JsonValue $summary.section_count_counts "3") -eq 3) "expected every audio asset to have 3 sections"

    Assert-Condition ($summary.declared_size_match_count -eq 3) "expected every audio declared size to match file size"
    Assert-Condition ($summary.section_record_count -eq 9) "expected 9 audio section records"
    Assert-Condition ($summary.section_declared_size_match_count -eq 9) "expected every section declared size to match table size"
    Assert-Condition ($summary.issue_count -eq 0) "expected 0 audio audit issues"

    Assert-Condition ((Get-JsonValue $summary.section_type_counts "0x2000") -eq 2) "expected 2 CSAR STRG sections"
    Assert-Condition ((Get-JsonValue $summary.section_type_counts "0x2001") -eq 2) "expected 2 CSAR INFO sections"
    Assert-Condition ((Get-JsonValue $summary.section_type_counts "0x2002") -eq 2) "expected 2 CSAR FILE sections"
    Assert-Condition ((Get-JsonValue $summary.section_type_counts "0x4000") -eq 1) "expected 1 CSTM INFO section"
    Assert-Condition ((Get-JsonValue $summary.section_type_counts "0x4001") -eq 1) "expected 1 CSTM SEEK section"
    Assert-Condition ((Get-JsonValue $summary.section_type_counts "0x4002") -eq 1) "expected 1 CSTM DATA section"

    Assert-Condition ($summary.section_magic_counts.STRG -eq 2) "expected 2 STRG sections"
    Assert-Condition ($summary.section_magic_counts.INFO -eq 3) "expected 3 INFO sections"
    Assert-Condition ($summary.section_magic_counts.FILE -eq 2) "expected 2 FILE sections"
    Assert-Condition ($summary.section_magic_counts.SEEK -eq 1) "expected 1 SEEK section"
    Assert-Condition ($summary.section_magic_counts.DATA -eq 1) "expected 1 DATA section"

    Assert-Condition ((Get-JsonValue $summary.section_layout_counts "0x2000:STRG,0x2001:INFO,0x2002:FILE") -eq 2) "expected CSAR section layout"
    Assert-Condition ((Get-JsonValue $summary.section_layout_counts "0x4000:INFO,0x4001:SEEK,0x4002:DATA") -eq 1) "expected CSTM section layout"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.section_size_summary_by_type "0x4002") "total") -eq 5707040) "expected CSTM DATA section size 5707040"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.size_summary_by_extension ".bcsar") "total") -eq 7024472) "expected BCSAR total byte size 7024472"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.size_summary_by_extension ".bcstm") "total") -eq 5710176) "expected BCSTM total byte size 5710176"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D audio asset audit: $auditOutput"
Write-Host "OOT3D audio asset summary: $summaryOutput"
