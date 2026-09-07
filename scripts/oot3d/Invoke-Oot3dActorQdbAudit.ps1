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
        throw "OOT3D actor QDB audit verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "actor_qdb_audit"
$auditOutput = Join-Path $outputRoot "oot3d_actor_qdb_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_qdb_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $auditArgs = @(
        "audit-actor-qdb-payloads",
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
    qdb_file_count = $audit.qdb_file_count
    archive_with_qdb_count = $audit.archive_with_qdb_count
    total_size = $audit.total_size
    size_summary = $audit.size_summary
    archive_qdb_counts = $audit.archive_qdb_counts
    qdb_count_distribution = $audit.qdb_count_distribution
    embedded_parent_dir_counts = $audit.embedded_parent_dir_counts
    type_name_counts = $audit.type_name_counts
    magic4_counts = $audit.magic4_counts
    version_counts = $audit.version_counts
    size_mod16_counts = $audit.size_mod16_counts
    header_word_counts = $audit.header_word_counts
    embedded_name_count = $audit.embedded_name_count
    embedded_stem_count = $audit.embedded_stem_count
    duplicate_embedded_names = $audit.duplicate_embedded_names
    duplicate_embedded_stems = $audit.duplicate_embedded_stems
    issue_count = $audit.issue_count
    sample_record_count = $audit.sample_records.Count
    record_count = $audit.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.zar_file_count -eq 348) "expected 348 actor ZAR files"
    Assert-Condition ($summary.parsed_zar_count -eq 348) "expected all actor ZAR files to parse"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 actor ZAR parse errors"
    Assert-Condition ($summary.qdb_file_count -eq 58) "expected 58 actor QDB payloads"
    Assert-Condition ($summary.archive_with_qdb_count -eq 18) "expected 18 actor archives with QDB payloads"
    Assert-Condition ($summary.total_size -eq 173984) "expected QDB total byte size 173984"
    Assert-Condition ($summary.size_summary.min -eq 128) "expected QDB min size 128"
    Assert-Condition ($summary.size_summary.max -eq 10640) "expected QDB max size 10640"
    Assert-Condition ($summary.size_summary.unique_size_count -eq 38) "expected 38 unique QDB sizes"

    Assert-Condition ($summary.qdb_count_distribution.'1' -eq 7) "expected 7 archives with 1 QDB"
    Assert-Condition ($summary.qdb_count_distribution.'2' -eq 4) "expected 4 archives with 2 QDB"
    Assert-Condition ($summary.qdb_count_distribution.'3' -eq 3) "expected 3 archives with 3 QDB"
    Assert-Condition ($summary.qdb_count_distribution.'4' -eq 1) "expected 1 archive with 4 QDB"
    Assert-Condition ($summary.qdb_count_distribution.'6' -eq 1) "expected 1 archive with 6 QDB"
    Assert-Condition ($summary.qdb_count_distribution.'12' -eq 2) "expected 2 archives with 12 QDB"

    Assert-Condition ($summary.archive_qdb_counts.'zelda_keep.zar' -eq 12) "expected 12 QDB in zelda_keep.zar"
    Assert-Condition ($summary.archive_qdb_counts.'zelda_keep_opening.zar' -eq 12) "expected 12 QDB in zelda_keep_opening.zar"
    Assert-Condition ($summary.archive_qdb_counts.'zelda_demo_kekkai.zar' -eq 6) "expected 6 QDB in zelda_demo_kekkai.zar"
    Assert-Condition ($summary.archive_qdb_counts.'zelda_spo04_objects.zar' -eq 4) "expected 4 QDB in zelda_spo04_objects.zar"

    Assert-Condition ($summary.embedded_parent_dir_counts.demo_lowercase -eq 51) "expected 51 lowercase demo QDB paths"
    Assert-Condition ($summary.embedded_parent_dir_counts.Demo_uppercase -eq 7) "expected 7 uppercase Demo QDB paths"
    Assert-Condition ($summary.type_name_counts.qdb -eq 58) "expected every embedded QDB type to be qdb"
    Assert-Condition ((Get-JsonValue $summary.magic4_counts "ascii: BDQ") -eq 58) "expected every QDB magic signature"
    Assert-Condition ($summary.version_counts.'3' -eq 58) "expected every QDB version candidate to be 3"
    Assert-Condition ($summary.size_mod16_counts.'0' -eq 58) "expected every QDB payload to be 16-byte aligned"
    Assert-Condition ($summary.header_word_counts.word_00.'1363427872' -eq 58) "expected every QDB word_00 magic"
    Assert-Condition ($summary.header_word_counts.word_01.'3' -eq 58) "expected every QDB word_01 version"
    Assert-Condition ($summary.embedded_name_count -eq 43) "expected 43 unique embedded QDB names"
    Assert-Condition ($summary.embedded_stem_count -eq 42) "expected 42 unique embedded QDB stems"
    Assert-Condition ($summary.duplicate_embedded_names.'demo/In_Demodt_Kenjyanoma.qdb' -eq 4) "expected repeated In_Demodt_Kenjyanoma QDB name"
    Assert-Condition ($summary.duplicate_embedded_stems.In_Demodt_Kenjyanoma -eq 5) "expected repeated In_Demodt_Kenjyanoma QDB stem"
    Assert-Condition ($summary.issue_count -eq 0) "expected 0 actor QDB audit issues"
    Assert-Condition ($summary.sample_record_count -le $SampleLimit) "sample records exceeded SampleLimit"
}

Write-Host "OOT3D actor QDB audit: $auditOutput"
Write-Host "OOT3D actor QDB summary: $summaryOutput"
