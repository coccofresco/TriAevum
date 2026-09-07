param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 100,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($ActorRoot)) {
    $ActorRoot = Join-Path $RomFs "actor"
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
        throw "OOT3D animation-like audit verification failed: $Message"
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
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "animation_like_audit"
$auditOutput = Join-Path $outputRoot "oot3d_actor_animation_like_payload_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_animation_like_payload_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-actor-animation-like-payloads",
        $ActorRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        ([string]$SampleLimit)
    )
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    archive_count = $audit.archive_count
    archive_parse_error_count = $audit.archive_parse_error_count
    cmb_parse_error_count = $audit.cmb_parse_error_count
    payload_type_counts = $audit.payload_type_counts
    archives_with_type = $audit.archives_with_type
    archive_payload_count_counts = $audit.archive_payload_count_counts
    archive_support_counts = $audit.archive_support_counts
    payload_size_summary = $audit.payload_size_summary
    magic_counts = $audit.magic_counts
    first_word_high16_counts = $audit.first_word_high16_counts
    word04_counts = $audit.word04_counts
    anb_format_high16_counts = $audit.anb_format_high16_counts
    anb_word04_signature_counts = $audit.anb_word04_signature_counts
    faceb_magic_status_counts = $audit.faceb_magic_status_counts
    faceb_size_match_counts = $audit.faceb_size_match_counts
    faceb_entry_count_counts = $audit.faceb_entry_count_counts
    faceb_entry_total = $audit.faceb_entry_total
    faceb_entry_frame_counts = $audit.faceb_entry_frame_counts
    faceb_entry_value_pair_counts = $audit.faceb_entry_value_pair_counts
    payload_name_stats = $audit.payload_name_stats
    path_stem_overlap = $audit.path_stem_overlap
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.archive_parse_error_count -eq 0) "expected 0 archive parse errors"
    Assert-Condition ($summary.cmb_parse_error_count -eq 0) "expected 0 embedded CMB parse errors during animation-like audit"

    Assert-Condition ($summary.payload_type_counts.anb -eq 1122) "expected 1122 ANB payloads"
    Assert-Condition ($summary.payload_type_counts.faceb -eq 1185) "expected 1185 FACEB payloads"
    Assert-Condition ($summary.archives_with_type.anb -eq 2) "expected 2 actor archives with ANB payloads"
    Assert-Condition ($summary.archives_with_type.faceb -eq 3) "expected 3 actor archives with FACEB payloads"
    Assert-Condition ($summary.archive_payload_count_counts.anb.'561' -eq 2) "expected two ANB archives with 561 payloads"
    Assert-Condition ($summary.archive_payload_count_counts.faceb.'582' -eq 2) "expected two FACEB archives with 582 payloads"
    Assert-Condition ($summary.archive_payload_count_counts.faceb.'21' -eq 1) "expected one FACEB archive with 21 payloads"

    Assert-Condition ($summary.payload_size_summary.anb.min -eq 276) "expected ANB min size 276"
    Assert-Condition ($summary.payload_size_summary.anb.max -eq 48918) "expected ANB max size 48918"
    Assert-Condition ($summary.payload_size_summary.anb.total -eq 4965636) "expected ANB total byte size 4965636"
    Assert-Condition ($summary.payload_size_summary.anb.unique_size_count -eq 92) "expected 92 unique ANB sizes"
    Assert-Condition ($summary.payload_size_summary.faceb.min -eq 12) "expected FACEB min size 12"
    Assert-Condition ($summary.payload_size_summary.faceb.max -eq 124) "expected FACEB max size 124"
    Assert-Condition ($summary.payload_size_summary.faceb.total -eq 21396) "expected FACEB total byte size 21396"
    Assert-Condition ($summary.payload_size_summary.faceb.unique_size_count -eq 22) "expected 22 unique FACEB sizes"

    Assert-Condition ((Get-JsonValue $summary.magic_counts.faceb "hex:666b6201") -eq 1185) "expected all FACEB payloads to use fkb01 magic"
    Assert-Condition ((Get-JsonValue $summary.first_word_high16_counts.anb "8") -eq 1122) "expected every ANB first word high16 to be 8"
    Assert-Condition ((Get-JsonValue $summary.word04_counts.anb "2425356288") -eq 1122) "expected every ANB word04 baseline signature"
    Assert-Condition ((Get-JsonValue $summary.anb_format_high16_counts "8") -eq 1122) "expected every ANB format high16 candidate to be 8"
    Assert-Condition ((Get-JsonValue $summary.anb_word04_signature_counts "2425356288") -eq 1122) "expected every ANB word04 candidate to match baseline"

    Assert-Condition ($summary.faceb_magic_status_counts.fkb01 -eq 1185) "expected every FACEB payload to parse fkb01 magic"
    Assert-Condition ($summary.faceb_size_match_counts.matches -eq 1185) "expected every FACEB size to match 8 + entry_count * 4"
    Assert-Condition ($summary.faceb_entry_total -eq 2979) "expected 2979 total FACEB entries"
    Assert-Condition ($summary.faceb_entry_count_counts.'1' -eq 917) "expected 917 one-entry FACEB payloads"
    Assert-Condition ($summary.faceb_entry_count_counts.'29' -eq 4) "expected 4 FACEB payloads with 29 entries"
    Assert-Condition ($summary.faceb_entry_frame_counts.'0' -eq 1187) "expected 1187 FACEB entries at frame 0"
    Assert-Condition ((Get-JsonValue $summary.faceb_entry_value_pair_counts "255,255") -eq 893) "expected 893 FACEB sentinel value pairs"

    Assert-Condition ($summary.archive_support_counts.anb.needs_skeleton_or_skinning_support -eq 1122) "expected all ANB payloads in skeleton/skinning archives"
    Assert-Condition ($summary.archive_support_counts.faceb.needs_skeleton_skinning_and_animation_support -eq 1185) "expected all FACEB payloads in skeleton/skinning/animation archives"
    Assert-Condition ($summary.payload_name_stats.anb.unique_name_count -eq 561) "expected 561 unique ANB names"
    Assert-Condition ($summary.payload_name_stats.anb.multiplicity_counts.'2' -eq 561) "expected every ANB name in two archives"
    Assert-Condition ($summary.payload_name_stats.faceb.unique_name_count -eq 583) "expected 583 unique FACEB names"
    Assert-Condition ($summary.payload_name_stats.faceb.multiplicity_counts.'1' -eq 2) "expected 2 singleton FACEB names"
    Assert-Condition ($summary.payload_name_stats.faceb.multiplicity_counts.'2' -eq 560) "expected 560 FACEB names in two archives"
    Assert-Condition ($summary.payload_name_stats.faceb.multiplicity_counts.'3' -eq 21) "expected 21 FACEB names in three archives"
    Assert-Condition ($summary.path_stem_overlap.shared_path_stems -eq 19) "expected 19 shared ANB/FACEB path stems"
    Assert-Condition ($summary.path_stem_overlap.anb_without_faceb_path_stems -eq 542) "expected 542 ANB path stems without FACEB"
    Assert-Condition ($summary.path_stem_overlap.faceb_without_anb_path_stems -eq 564) "expected 564 FACEB path stems without ANB"
}

Write-Host "OOT3D actor animation-like payload audit: $auditOutput"
Write-Host "OOT3D actor animation-like payload summary: $summaryOutput"
