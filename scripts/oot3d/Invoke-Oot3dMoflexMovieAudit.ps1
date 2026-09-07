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
        throw "OOT3D Moflex movie audit verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "moflex_movie_audit"
$auditOutput = Join-Path $outputRoot "oot3d_moflex_movie_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_moflex_movie_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-moflex-movies",
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
    size_summary = $audit.size_summary
    size_class_counts = $audit.size_class_counts
    magic4_counts = $audit.magic4_counts
    signature16_counts = $audit.signature16_counts
    dimension_candidate_counts = $audit.dimension_candidate_counts
    header_field_counts = $audit.header_field_counts
    timing_profile_counts = $audit.timing_profile_counts
    hint_movie_numbering = $audit.hint_movie_numbering
    duplicate_hint_movie_numbers = $audit.duplicate_hint_movie_numbers
    issue_count = $audit.issue_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.file_count -eq 138) "expected 138 Moflex movies"
    Assert-Condition ($summary.total_size -eq 148292280) "expected total Moflex byte size 148292280"
    Assert-Condition ((Get-JsonValue $summary.extension_counts ".moflex") -eq 138) "expected 138 .moflex files"
    Assert-Condition ($summary.category_counts.moflex_movie -eq 138) "expected 138 Moflex movie assets"
    Assert-Condition ($summary.top_level_counts.misc -eq 138) "expected all Moflex files under misc"
    Assert-Condition ((Get-JsonValue $summary.parent_dir_counts "misc/hint/movie") -eq 138) "expected all Moflex files under misc/hint/movie"

    Assert-Condition ($summary.size_summary.min -eq 1004402) "expected Moflex min size 1004402"
    Assert-Condition ($summary.size_summary.max -eq 1528620) "expected Moflex max size 1528620"
    Assert-Condition ($summary.size_summary.unique_size_count -eq 137) "expected 137 unique Moflex sizes"
    Assert-Condition ($summary.size_class_counts.small -eq 123) "expected 123 small Moflex movies"
    Assert-Condition ($summary.size_class_counts.large -eq 15) "expected 15 large Moflex movies"

    Assert-Condition ((Get-JsonValue $summary.magic4_counts "hex:4c32aaab") -eq 138) "expected every Moflex magic4 signature"
    Assert-Condition ((Get-JsonValue $summary.signature16_counts "4c 32 aa ab 00 00 00 00 00 00 00 01 0f ff 03 0d") -eq 138) "expected every Moflex signature16"
    Assert-Condition ((Get-JsonValue $summary.dimension_candidate_counts "400x240") -eq 138) "expected every Moflex dimension candidate to be 400x240"
    Assert-Condition ((Get-JsonValue $summary.timing_profile_counts "50000/835") -eq 132) "expected 132 Moflex 50000/835 timing profile candidates"
    Assert-Condition ((Get-JsonValue $summary.timing_profile_counts "39062/652") -eq 6) "expected 6 Moflex 39062/652 timing profile candidates"

    Assert-Condition ((Get-JsonValue $summary.header_field_counts.u32_04 "0") -eq 138) "expected header u32_04 zero"
    Assert-Condition ((Get-JsonValue $summary.header_field_counts.u32_08 "1") -eq 138) "expected header u32_08 one"
    Assert-Condition ((Get-JsonValue $summary.header_field_counts.u32_0c "268370701") -eq 138) "expected header u32_0c baseline"
    Assert-Condition ((Get-JsonValue $summary.header_field_counts.u16_10 "0") -eq 138) "expected header u16_10 zero"
    Assert-Condition ((Get-JsonValue $summary.header_field_counts.u16_1a "257") -eq 138) "expected header u16_1a baseline"
    Assert-Condition ((Get-JsonValue $summary.header_field_counts.u32_1c "9") -eq 138) "expected header u32_1c baseline"

    Assert-Condition ($summary.hint_movie_numbering.count -eq 138) "expected 138 numbered hint movies"
    Assert-Condition ($summary.hint_movie_numbering.unique_count -eq 138) "expected 138 unique hint movie numbers"
    Assert-Condition ($summary.hint_movie_numbering.min -eq 0) "expected first hint movie number 0"
    Assert-Condition ($summary.hint_movie_numbering.max -eq 191) "expected last hint movie number 191"
    Assert-Condition ($summary.hint_movie_numbering.missing_count -eq 54) "expected 54 missing hint movie numbers"
    Assert-Condition ($summary.duplicate_hint_movie_numbers.Count -eq 0) "expected 0 duplicate hint movie numbers"
    Assert-Condition ($summary.issue_count -eq 0) "expected 0 Moflex audit issues"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D Moflex movie audit: $auditOutput"
Write-Host "OOT3D Moflex movie summary: $summaryOutput"
