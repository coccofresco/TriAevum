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
        throw "OOT3D CTXB texture audit verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "ctxb_texture_audit"
$auditOutput = Join-Path $outputRoot "oot3d_ctxb_texture_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_ctxb_texture_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-ctxb-textures",
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
    ctxb_count = $audit.ctxb_count
    loose_ctxb_count = $audit.loose_ctxb_count
    embedded_ctxb_count = $audit.embedded_ctxb_count
    parsed_ctxb_count = $audit.parsed_ctxb_count
    parse_error_count = $audit.parse_error_count
    decode_error_count = $audit.decode_error_count
    zar_archive_count = $audit.zar_archive_count
    zar_with_ctxb_count = $audit.zar_with_ctxb_count
    zar_parse_error_count = $audit.zar_parse_error_count
    source_kind_counts = $audit.source_kind_counts
    top_level_counts = $audit.top_level_counts
    size_summary = $audit.size_summary
    payload_size_summary = $audit.payload_size_summary
    decoded_rgba16_size_summary = $audit.decoded_rgba16_size_summary
    format_pair_counts = $audit.format_pair_counts
    format_name_counts = $audit.format_name_counts
    expected_payload_size_match_counts = $audit.expected_payload_size_match_counts
    decoded_rgba16_status_counts = $audit.decoded_rgba16_status_counts
    decoded_rgba16_size_match_counts = $audit.decoded_rgba16_size_match_counts
    flags_counts = $audit.flags_counts
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.ctxb_count -eq 1678) "expected 1678 CTXB sources"
    Assert-Condition ($summary.loose_ctxb_count -eq 588) "expected 588 loose CTXB files"
    Assert-Condition ($summary.embedded_ctxb_count -eq 1090) "expected 1090 embedded ZAR CTXB files"
    Assert-Condition ($summary.parsed_ctxb_count -eq 1678) "expected every CTXB source to parse"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 CTXB parse errors"
    Assert-Condition ($summary.decode_error_count -eq 0) "expected 0 CTXB decode errors"
    Assert-Condition ($summary.zar_archive_count -eq 461) "expected 461 ZAR archives to be scanned"
    Assert-Condition ($summary.zar_with_ctxb_count -eq 101) "expected 101 ZAR archives with CTXB files"
    Assert-Condition ($summary.zar_parse_error_count -eq 0) "expected 0 ZAR parse errors"

    Assert-Condition ($summary.source_kind_counts.embedded_zar -eq 1090) "expected 1090 embedded CTXB source records"
    Assert-Condition ($summary.source_kind_counts.loose -eq 588) "expected 588 loose CTXB source records"
    Assert-Condition ($summary.top_level_counts.scene -eq 795) "expected 795 scene CTXB records"
    Assert-Condition ($summary.top_level_counts.menu -eq 529) "expected 529 menu CTXB records"
    Assert-Condition ($summary.top_level_counts.actor -eq 277) "expected 277 actor CTXB records"
    Assert-Condition ($summary.top_level_counts.kankyo -eq 18) "expected 18 kankyo CTXB records"

    Assert-Condition ($summary.size_summary.total -eq 69670256) "expected CTXB total byte size 69670256"
    Assert-Condition ($summary.payload_size_summary.total -eq 69549440) "expected CTXB payload byte size 69549440"
    Assert-Condition ($summary.decoded_rgba16_size_summary.total -eq 86911616) "expected decoded RGBA16 byte size 86911616"
    Assert-Condition ((Get-JsonValue $summary.expected_payload_size_match_counts "matches") -eq 1678) "expected all CTXB payload sizes to match format/dimensions"
    Assert-Condition ((Get-JsonValue $summary.decoded_rgba16_status_counts "decoded") -eq 1678) "expected all CTXB textures to decode"
    Assert-Condition ((Get-JsonValue $summary.decoded_rgba16_size_match_counts "matches") -eq 1678) "expected all decoded RGBA16 sizes to match dimensions"

    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6758/0x1401") -eq 900) "expected 900 LA8 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6752/0x8033") -eq 387) "expected 387 RGBA4 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x675b/0x0000") -eq 162) "expected 162 ETC1A4 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x675a/0x0000") -eq 151) "expected 151 ETC1 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6757/0x1401") -eq 35) "expected 35 L8 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6754/0x8363") -eq 18) "expected 18 RGB565 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6752/0x1401") -eq 16) "expected 16 RGBA8 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6752/0x8034") -eq 6) "expected 6 RGBA5551 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6757/0x6761") -eq 2) "expected 2 L4 CTXB textures"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6756/0x1401") -eq 1) "expected 1 A8 CTXB texture"

    Assert-Condition ((Get-JsonValue $summary.flags_counts "0x00000001") -eq 1365) "expected 1365 CTXB flag 0x00000001 records"
    Assert-Condition ((Get-JsonValue $summary.flags_counts "0x00010001") -eq 313) "expected 313 CTXB flag 0x00010001 records"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D CTXB texture audit: $auditOutput"
Write-Host "OOT3D CTXB texture summary: $summaryOutput"
