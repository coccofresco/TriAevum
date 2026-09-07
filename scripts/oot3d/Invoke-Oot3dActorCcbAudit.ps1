param(
    [string]$ToolRoot = "",
    [string]$ActorRoot = "E:\ppssppvr\oot3d_decomp\work\extract\romfs\actor",
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
        throw "OOT3D actor CCB audit verification failed: $Message"
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
Require-Path $ActorRoot "OOT3D actor root"

$outputRoot = Join-Path $WorkRoot "actor_ccb_audit"
$auditOutput = Join-Path $outputRoot "oot3d_actor_ccb_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_ccb_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-actor-ccb-payloads",
        $ActorRoot,
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
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    zar_file_count = $audit.zar_file_count
    parsed_zar_count = $audit.parsed_zar_count
    parse_error_count = $audit.parse_error_count
    ccb_file_count = $audit.ccb_file_count
    archive_with_ccb_count = $audit.archive_with_ccb_count
    total_size = $audit.total_size
    size_summary = $audit.size_summary
    archive_ccb_counts = $audit.archive_ccb_counts
    ccb_count_distribution = $audit.ccb_count_distribution
    embedded_parent_dir_counts = $audit.embedded_parent_dir_counts
    type_name_counts = $audit.type_name_counts
    magic4_counts = $audit.magic4_counts
    version_counts = $audit.version_counts
    declared_size_delta_counts = $audit.declared_size_delta_counts
    size_mod16_counts = $audit.size_mod16_counts
    caad_count_counts = $audit.caad_count_counts
    caad_marker_count_counts = $audit.caad_marker_count_counts
    mads_marker_count_counts = $audit.mads_marker_count_counts
    cmad_marker_count_counts = $audit.cmad_marker_count_counts
    caad_index_sequence_counts = $audit.caad_index_sequence_counts
    issue_count = $audit.issue_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.zar_file_count -eq 348) "expected 348 actor ZAR files"
    Assert-Condition ($summary.parsed_zar_count -eq 348) "expected all actor ZAR files to parse"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 actor ZAR parse errors"
    Assert-Condition ($summary.ccb_file_count -eq 1) "expected 1 actor CCB payload"
    Assert-Condition ($summary.archive_with_ccb_count -eq 1) "expected 1 actor archive with CCB payloads"
    Assert-Condition ($summary.total_size -eq 8304) "expected CCB total byte size 8304"
    Assert-Condition ($summary.size_summary.min -eq 8304) "expected CCB min size 8304"
    Assert-Condition ($summary.size_summary.max -eq 8304) "expected CCB max size 8304"
    Assert-Condition ($summary.size_summary.unique_size_count -eq 1) "expected 1 unique CCB size"
    Assert-Condition ($summary.archive_ccb_counts.'zelda_zl4.zar' -eq 1) "expected CCB in zelda_zl4.zar"
    Assert-Condition ($summary.ccb_count_distribution.'1' -eq 1) "expected one archive with one CCB"
    Assert-Condition ($summary.embedded_parent_dir_counts.ccam -eq 1) "expected CCB under ccam"
    Assert-Condition ($summary.type_name_counts.ccb -eq 1) "expected embedded CCB type to be ccb"
    Assert-Condition ((Get-JsonValue $summary.magic4_counts "hex:63636200") -eq 1) "expected CCB magic signature"
    Assert-Condition ($summary.version_counts.'3' -eq 1) "expected CCB version candidate to be 3"
    Assert-Condition ($summary.declared_size_delta_counts.'48' -eq 1) "expected CCB declared-size delta 48"
    Assert-Condition ($summary.size_mod16_counts.'0' -eq 1) "expected CCB payload to be 16-byte aligned"
    Assert-Condition ($summary.caad_count_counts.'11' -eq 1) "expected CCB caad count 11"
    Assert-Condition ($summary.caad_marker_count_counts.'11' -eq 1) "expected 11 CCB caad markers"
    Assert-Condition ($summary.mads_marker_count_counts.'11' -eq 1) "expected 11 CCB mads markers"
    Assert-Condition ($summary.cmad_marker_count_counts.'27' -eq 1) "expected 27 CCB cmad markers"
    Assert-Condition ($summary.caad_index_sequence_counts.zero_based_contiguous -eq 1) "expected contiguous CCB caad indices"
    Assert-Condition ($summary.issue_count -eq 0) "expected 0 actor CCB audit issues"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D actor CCB audit: $auditOutput"
Write-Host "OOT3D actor CCB summary: $summaryOutput"
