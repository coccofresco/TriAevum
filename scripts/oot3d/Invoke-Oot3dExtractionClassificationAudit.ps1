param(
    [string]$ExtractionRoot = "E:\ppssppvr\oot3d_decomp\work\extract",
    [string]$Output = "I:\oot3dre_work\extraction_classification\oot3d_extraction_classification.json",
    [switch]$NoRecords,
    [switch]$Verify
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..\..")).Path
}

function Require-Path {
    param(
        [string]$Path,
        [string]$Label
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Get-JsonValue {
    param(
        $Object,
        [string]$Name
    )
    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) {
        return $null
    }
    return $prop.Value
}

function Add-Issue {
    param(
        [System.Collections.Generic.List[object]]$Issues,
        [string]$Code,
        $Actual
    )
    [void]$Issues.Add([ordered]@{
        code = $Code
        actual = $Actual
    })
}

$repoRoot = Get-RepoRoot
$toolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
Require-Path $ExtractionRoot "OOT3D extraction root"
Require-Path (Join-Path $ExtractionRoot "romfs") "OOT3D RomFS"
Require-Path (Join-Path $ExtractionRoot "exefs") "OOT3D ExeFS"
Require-Path $toolRoot "oot3d_asset_tool"

$env:PYTHONPATH = Join-Path $toolRoot "src"
$arguments = @(
    "-m", "oot3d_asset_tool",
    "audit-extraction-classification",
    $ExtractionRoot,
    "--output", $Output
)
if ($NoRecords) {
    $arguments += "--no-records"
}

Write-Host "python $($arguments -join ' ')"
& python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
}

$report = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
$summary = Get-JsonValue $report "summary"
$issues = [System.Collections.Generic.List[object]]::new()

if ((Get-JsonValue $report "format") -ne "oot3d_extraction_classification_v1") {
    Add-Issue $issues "invalid_format" (Get-JsonValue $report "format")
}
if ((Get-JsonValue $summary "romfs_interpret_or_convert_file_count") -ne 1974) {
    Add-Issue $issues "unexpected_romfs_file_count" (Get-JsonValue $summary "romfs_interpret_or_convert_file_count")
}
if ((Get-JsonValue $summary "exefs_file_count") -ne 4) {
    Add-Issue $issues "unexpected_exefs_file_count" (Get-JsonValue $summary "exefs_file_count")
}
if ((Get-JsonValue $summary "exefs_decompile_code_file_count") -ne 1) {
    Add-Issue $issues "unexpected_decompile_code_file_count" (Get-JsonValue $summary "exefs_decompile_code_file_count")
}
if ((Get-JsonValue $summary "effective_gameplay_script_file_count") -ne 1) {
    Add-Issue $issues "unexpected_gameplay_script_file_count" (Get-JsonValue $summary "effective_gameplay_script_file_count")
}

$summaryOutput = [ordered]@{
    status = if ($issues.Count -eq 0) { "valid" } else { "invalid" }
    extraction_root = $ExtractionRoot
    output = $Output
    romfs_interpret_or_convert_file_count = Get-JsonValue $summary "romfs_interpret_or_convert_file_count"
    exefs_file_count = Get-JsonValue $summary "exefs_file_count"
    exefs_decompile_code_file_count = Get-JsonValue $summary "exefs_decompile_code_file_count"
    effective_gameplay_script_file_count = Get-JsonValue $summary "effective_gameplay_script_file_count"
    issues = @($issues)
}

$summaryPath = [System.IO.Path]::ChangeExtension($Output, ".summary.json")
$summaryOutput | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host "Wrote extraction classification summary: $summaryPath"

if ($Verify -and $issues.Count -ne 0) {
    throw "Extraction classification audit failed with $($issues.Count) issue(s). See $summaryPath"
}

if ($Verify) {
    Write-Host "Extraction classification audit passed."
}
