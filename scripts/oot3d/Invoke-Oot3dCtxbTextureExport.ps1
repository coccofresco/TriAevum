param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ResourcePrefix = "textures/oot3d/ctxb",
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
        throw "OOT3D CTXB texture export verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "ctxb_texture_export"
$manifestOutput = Join-Path $outputRoot "ctxb_texture_export_manifest.json"
$summaryOutput = Join-Path $outputRoot "ctxb_texture_export_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $exportArgs = @(
        "export-ctxb-textures",
        $RomFs,
        "--output",
        $outputRoot,
        "--resource-prefix",
        $ResourcePrefix,
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
    romfs_root = (Resolve-Path $RomFs).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    resource_prefix = $manifest.resource_prefix
    ctxb_count = $manifest.ctxb_count
    parsed_count = $manifest.parsed_count
    exported_count = $manifest.exported_count
    parse_error_count = $manifest.parse_error_count
    export_error_count = $manifest.export_error_count
    zar_parse_error_count = $manifest.zar_parse_error_count
    resource_count = $manifest.resource_count
    source_kind_counts = $manifest.source_kind_counts
    top_level_counts = $manifest.top_level_counts
    format_pair_counts = $manifest.format_pair_counts
    format_name_counts = $manifest.format_name_counts
    status_counts = $manifest.status_counts
    decoded_rgba16_size_summary = $manifest.decoded_rgba16_size_summary
    resource_audit_summary = $manifest.resource_audit_summary
    sample_record_count = $manifest.sample_records.Count
    record_count = $manifest.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.ctxb_count -eq 1678) "expected 1678 CTXB sources"
    Assert-Condition ($summary.parsed_count -eq 1678) "expected every CTXB source to parse"
    Assert-Condition ($summary.exported_count -eq 1678) "expected every CTXB source to export"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 CTXB parse errors"
    Assert-Condition ($summary.export_error_count -eq 0) "expected 0 CTXB export errors"
    Assert-Condition ($summary.zar_parse_error_count -eq 0) "expected 0 ZAR parse errors"
    Assert-Condition ($summary.resource_count -eq 1678) "expected 1678 generated Texture resources"
    Assert-Condition ($summary.source_kind_counts.embedded_zar -eq 1090) "expected 1090 embedded CTXB exports"
    Assert-Condition ($summary.source_kind_counts.loose -eq 588) "expected 588 loose CTXB exports"
    Assert-Condition ($summary.status_counts.exported -eq 1678) "expected every CTXB record to have exported status"

    Assert-Condition ($summary.top_level_counts.scene -eq 795) "expected 795 scene CTXB resources"
    Assert-Condition ($summary.top_level_counts.menu -eq 529) "expected 529 menu CTXB resources"
    Assert-Condition ($summary.top_level_counts.actor -eq 277) "expected 277 actor CTXB resources"
    Assert-Condition ($summary.top_level_counts.kankyo -eq 18) "expected 18 kankyo CTXB resources"

    Assert-Condition ($summary.decoded_rgba16_size_summary.total -eq 86911616) "expected decoded RGBA16 byte size 86911616"
    Assert-Condition ($summary.resource_audit_summary.checked_resource_count -eq 1678) "expected 1678 checked Texture resources"
    Assert-Condition ($summary.resource_audit_summary.issue_record_count -eq 0) "expected 0 generated Texture resource issue records"
    Assert-Condition ((Get-JsonValue $summary.resource_audit_summary.issue_counts "none") -eq 1678) "expected every generated Texture resource audit to pass"
    Assert-Condition ($summary.resource_audit_summary.generated_resource_size_summary.total -eq 87065992) "expected generated Texture resource bytes 87065992"

    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6758/0x1401") -eq 900) "expected 900 LA8 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6752/0x8033") -eq 387) "expected 387 RGBA4 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x675b/0x0000") -eq 162) "expected 162 ETC1A4 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x675a/0x0000") -eq 151) "expected 151 ETC1 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6757/0x1401") -eq 35) "expected 35 L8 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6754/0x8363") -eq 18) "expected 18 RGB565 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6752/0x1401") -eq 16) "expected 16 RGBA8 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6752/0x8034") -eq 6) "expected 6 RGBA5551 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6757/0x6761") -eq 2) "expected 2 L4 CTXB exports"
    Assert-Condition ((Get-JsonValue $summary.format_pair_counts "0x6756/0x1401") -eq 1) "expected 1 A8 CTXB export"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D CTXB texture export manifest: $manifestOutput"
Write-Host "OOT3D CTXB texture export summary: $summaryOutput"
