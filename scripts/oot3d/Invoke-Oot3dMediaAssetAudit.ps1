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
        throw "OOT3D media asset audit verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "media_asset_audit"
$auditOutput = Join-Path $outputRoot "oot3d_media_asset_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_media_asset_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-media-assets",
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
    signature16_counts = $audit.signature16_counts
    moflex_dimension_candidate_counts = $audit.moflex_dimension_candidate_counts
    moflex_hint_numbering = $audit.moflex_hint_numbering
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.file_count -eq 781) "expected 781 media/support assets"
    Assert-Condition ($summary.total_size -eq 212415588) "expected total media/support byte size 212415588"

    Assert-Condition ((Get-JsonValue $summary.extension_counts ".ctxb") -eq 588) "expected 588 CTXB files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".moflex") -eq 138) "expected 138 Moflex files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qan") -eq 12) "expected 12 QAN files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qbf") -eq 2) "expected 2 QBF files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qbr") -eq 1) "expected 1 QBR file"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qcl") -eq 1) "expected 1 QCL file"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qhm") -eq 1) "expected 1 QHM file"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qly") -eq 23) "expected 23 QLY files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".qsp") -eq 12) "expected 12 QSP files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".bcsar") -eq 2) "expected 2 BCSAR files"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".bcstm") -eq 1) "expected 1 BCSTM file"

    Assert-Condition ($summary.category_counts.external_texture -eq 588) "expected 588 external texture assets"
    Assert-Condition ($summary.category_counts.moflex_movie -eq 138) "expected 138 movie assets"
    Assert-Condition ($summary.category_counts.layout_animation -eq 12) "expected 12 layout animation assets"
    Assert-Condition ($summary.category_counts.layout -eq 23) "expected 23 layout assets"
    Assert-Condition ($summary.category_counts.sprite -eq 12) "expected 12 sprite assets"
    Assert-Condition ($summary.category_counts.bitmap_font -eq 2) "expected 2 bitmap font assets"
    Assert-Condition ($summary.category_counts.audio_archive -eq 2) "expected 2 audio archives"
    Assert-Condition ($summary.category_counts.audio_stream -eq 1) "expected 1 audio stream"
    Assert-Condition ($summary.category_counts.boss_rush_metadata -eq 1) "expected 1 boss-rush metadata file"
    Assert-Condition ($summary.category_counts.hint_metadata -eq 1) "expected 1 hint metadata file"
    Assert-Condition ($summary.category_counts.layout_color -eq 1) "expected 1 layout color file"

    Assert-Condition ($summary.top_level_counts.menu -eq 529) "expected 529 menu support assets"
    Assert-Condition ($summary.top_level_counts.misc -eq 240) "expected 240 misc support assets"
    Assert-Condition ($summary.top_level_counts.message -eq 8) "expected 8 message support assets"
    Assert-Condition ($summary.top_level_counts.sound -eq 3) "expected 3 sound support assets"
    Assert-Condition ($summary.top_level_counts.ending -eq 1) "expected 1 ending support asset"

    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".moflex") "hex:4c32aaab") -eq 138) "expected all Moflex magic4 signatures"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".ctxb") "ascii:ctxb") -eq 588) "expected all CTXB magic4 signatures"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".bcsar") "ascii:CSAR") -eq 2) "expected all BCSAR magic4 signatures"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".bcstm") "ascii:CSTM") -eq 1) "expected all BCSTM magic4 signatures"
    Assert-Condition ((Get-JsonValue (Get-JsonValue $summary.magic4_counts ".qbf") "ascii:QBF1") -eq 2) "expected all QBF magic4 signatures"

    Assert-Condition ((Get-JsonValue $summary.moflex_dimension_candidate_counts "400x240") -eq 138) "expected every Moflex to expose 400x240 header candidates"
    Assert-Condition ($summary.moflex_hint_numbering.count -eq 138) "expected 138 numbered hint Moflex files"
    Assert-Condition ($summary.moflex_hint_numbering.unique_count -eq 138) "expected 138 unique hint Moflex numbers"
    Assert-Condition ($summary.moflex_hint_numbering.min -eq 0) "expected first hint Moflex number 0"
    Assert-Condition ($summary.moflex_hint_numbering.max -eq 191) "expected last hint Moflex number 191"
    Assert-Condition ($summary.moflex_hint_numbering.missing_count -eq 54) "expected 54 missing hint movie numbers"

    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".moflex").total -eq 148292280) "expected Moflex total byte size 148292280"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".moflex").min -eq 1004402) "expected Moflex min size 1004402"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".moflex").max -eq 1528620) "expected Moflex max size 1528620"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".ctxb").total -eq 50812896) "expected CTXB total byte size 50812896"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".bcsar").total -eq 7024472) "expected BCSAR total byte size 7024472"
    Assert-Condition ((Get-JsonValue $summary.size_summary_by_extension ".bcstm").total -eq 5710176) "expected BCSTM total byte size 5710176"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D media asset audit: $auditOutput"
Write-Host "OOT3D media asset summary: $summaryOutput"
