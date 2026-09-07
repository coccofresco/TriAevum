param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$PrimaryArchive = "",
    [string]$DuplicateArchive = "",
    [string]$CsabBindingManifest = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$Output = "",
    [int]$SampleLimit = 20,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($PrimaryArchive)) {
    $PrimaryArchive = Join-Path $RomFs "actor\zelda_link_child_ultra.zar"
}
if ([string]::IsNullOrWhiteSpace($DuplicateArchive)) {
    $DuplicateArchive = Join-Path $RomFs "actor\zelda_link_boy_ultra.zar"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $WorkRoot "anb_export\link_child_anb_payload_batch.json"
}
if ([string]::IsNullOrWhiteSpace($CsabBindingManifest)) {
    $CsabBindingManifest = Join-Path $WorkRoot "skinned_animation_binding\oot3d_skinned_animation_binding_manifest.json"
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
        throw "OOT3D Link child ANB export verification failed: $Message"
    }
}

function Get-JsonValue([object]$Object, [string]$Name) {
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
Require-Path $PrimaryArchive "Link child ultra ANB archive"
Require-Path $DuplicateArchive "Link boy ultra duplicate ANB archive"
Require-Path $CsabBindingManifest "OOT3D skinned animation binding manifest"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-anb-payload-batch",
        $PrimaryArchive,
        "--output",
        $Output,
        "--duplicate-archive",
        $DuplicateArchive,
        "--csab-binding-manifest",
        $CsabBindingManifest,
        "--csab-target-archive",
        "zelda_link_child_new.zar",
        "--csab-target-cmb",
        "child/model/childlink_v2.cmb",
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $Output "Link child ANB export"
$export = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json

if ($Verify) {
    Assert-Condition ($export.format -eq "oot3d_anb_payload_batch_export_v1") "unexpected ANB batch export format"
    Assert-Condition ($export.payload_count -eq 561) "expected 561 Link child ANB payloads"
    Assert-Condition ($export.duplicate_payload_match_count -eq 561) "expected every Link child ANB payload to match duplicate archive"
    Assert-Condition ($export.duplicate_payload_missing_count -eq 0) "expected zero missing duplicate ANB payloads"
    Assert-Condition ($export.duplicate_payload_mismatch_count -eq 0) "expected zero duplicate ANB payload mismatches"
    Assert-Condition ($export.invalid_header_count -eq 0) "expected zero invalid ANB headers"
    Assert-Condition ($export.decoded_frame_table_match_count -eq 561) "expected every ANB payload to match the decoded frame-table stride"
    Assert-Condition ($export.decoded_frame_table_mismatch_count -eq 0) "expected zero ANB frame-table size mismatches"
    Assert-Condition ($export.total_decoded_frame_count -eq 18495) "expected 18495 decoded ANB frames"
    Assert-Condition ($export.total_s16_sample_count -eq 1239165) "expected 1239165 decoded ANB signed 16-bit samples"
    Assert-Condition ((Get-JsonValue $export.channel_count_candidate_counts "67") -eq 561) "expected every ANB payload to decode as 67 channels per frame"
    Assert-Condition ($export.channel_summary.channel_count -eq 67) "expected ANB channel summary to cover 67 channels"
    Assert-Condition ($export.channel_summary.sample_min -eq -32768) "expected ANB signed sample minimum -32768"
    Assert-Condition ($export.channel_summary.sample_max -eq 32767) "expected ANB signed sample maximum 32767"
    Assert-Condition ($export.channel_summary.varying_channel_count -eq 61) "expected 61 ANB channels to vary across the batch"
    Assert-Condition ($export.channel_summary.always_zero_channel_count -eq 6) "expected 6 ANB channels to remain zero across the batch"
    Assert-Condition ($export.channel_summary.never_zero_channel_count -eq 21) "expected 21 ANB channels to remain nonzero across the batch"
    Assert-Condition ($export.channel_summary.record_active_channel_count_min -eq 12) "expected minimum 12 active ANB channels per record"
    Assert-Condition ($export.channel_summary.record_active_channel_count_max -eq 54) "expected maximum 54 active ANB channels per record"
    Assert-Condition ($export.csab_lookup.status -eq "provided") "expected ANB export to include CSAB lookup"
    Assert-Condition ($export.csab_lookup.csab_stem_count -eq 582) "expected CSAB lookup to cover 582 Link child CSAB stems"
    Assert-Condition ($export.csab_lookup.anb_stem_count -eq 561) "expected ANB lookup to cover 561 ANB stems"
    Assert-Condition ($export.csab_lookup.matched_anb_count -eq 508) "expected 508 ANB payloads with Link child CSAB stem, normalized-leaf, or semantic-alias matches"
    Assert-Condition ($export.csab_lookup.matched_stem_count -eq 508) "expected 508 matched ANB stems"
    Assert-Condition ($export.csab_lookup.matched_csab_stem_count -eq 506) "expected 506 matched CSAB stems"
    Assert-Condition ($export.csab_lookup.match_kind_counts.exact_stem -eq 16) "expected 16 exact ANB/CSAB stem matches"
    Assert-Condition ($export.csab_lookup.match_kind_counts.normalized_leaf_unique -eq 83) "expected 83 unique normalized-leaf ANB/CSAB matches"
    Assert-Condition ($export.csab_lookup.match_kind_counts.semantic_alias_unique -eq 409) "expected 409 unique semantic-alias ANB/CSAB matches"
    Assert-Condition ($export.csab_lookup.matched_frame_count_status_count -eq 454) "expected 454 matched ANB/CSAB records with equal frame counts"
    Assert-Condition ($export.csab_lookup.mismatched_frame_count_status_count -eq 54) "expected 54 matched ANB/CSAB records with differing frame counts"
    Assert-Condition ($export.csab_lookup.unmatched_anb_count -eq 53) "expected 53 ANB payloads without Link child CSAB matches"
    Assert-Condition ($export.csab_lookup.csab_without_anb_count -eq 76) "expected 76 Link child CSAB stems without ANB matches"
    Assert-Condition ($export.issue_count -eq 0) "expected zero ANB export issues"
    Assert-Condition ((Get-JsonValue $export.trailing_byte_count_counts "0") -eq 308) "expected 308 ANB payloads without trailing bytes"
    Assert-Condition ((Get-JsonValue $export.trailing_byte_count_counts "2") -eq 253) "expected 253 ANB payloads with 2 trailing bytes"
    Assert-Condition ((Get-JsonValue $export.frame_count_candidate_counts "20") -eq 58) "expected 58 ANB payloads with frame candidate 20"
}

Write-Host "OOT3D Link child ANB export: $Output"
Write-Host "payloads=$($export.payload_count) duplicateMatches=$($export.duplicate_payload_match_count) issues=$($export.issue_count)"
