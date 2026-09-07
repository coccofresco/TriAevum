param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$AnbExport = "",
    [string]$CharacterManifest = "",
    [string]$TrackRoot = "",
    [string]$Output = "",
    [string]$UnresolvedChannelCsvOutput = "",
    [double]$MinAbsCorrelation = 0.98,
    [int]$MinConsensusCount = 2,
    [int]$SampleLimit = 20,
    [switch]$DisableFrameMismatchResample,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($AnbExport)) {
    $AnbExport = Join-Path $WorkRoot "anb_export\link_child_anb_payload_batch.json"
}
if ([string]::IsNullOrWhiteSpace($CharacterManifest)) {
    $CharacterManifest = Join-Path $WorkRoot "character_conversion\link_child_character_conversion_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($TrackRoot)) {
    $TrackRoot = Join-Path $WorkRoot "skinned_animation_batch\tracks"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $WorkRoot "anb_export\link_child_anb_semantic_candidate_audit.json"
}
if ([string]::IsNullOrWhiteSpace($UnresolvedChannelCsvOutput)) {
    $UnresolvedChannelCsvOutput = Join-Path $WorkRoot "anb_export\link_child_anb_unresolved_channel_worklist.csv"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child ANB semantic candidate audit failed: $Message"
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
Require-Path $AnbExport "Link child ANB export"
Require-Path $CharacterManifest "Link child character conversion manifest"
Require-Path $TrackRoot "CSAB track root"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $UnresolvedChannelCsvOutput) | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "audit-anb-semantic-candidates",
        $AnbExport,
        $CharacterManifest,
        "--output",
        $Output,
        "--unresolved-channel-csv-output",
        $UnresolvedChannelCsvOutput,
        "--track-root",
        $TrackRoot,
        "--min-abs-correlation",
        [string]$MinAbsCorrelation,
        "--min-consensus-count",
        [string]$MinConsensusCount,
        "--sample-limit",
        [string]$SampleLimit
    )
    if (-not $DisableFrameMismatchResample) {
        $arguments += "--include-frame-mismatch-resample"
    }
    Write-Host "python -m oot3d_asset_tool $($arguments -join ' ')"
    & python -m oot3d_asset_tool @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Require-Path $Output "Link child ANB semantic candidate audit"
Require-Path $UnresolvedChannelCsvOutput "Link child unresolved ANB channel worklist"
$audit = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
if ($Verify) {
    Assert-Condition ($audit.format -eq "oot3d_anb_semantic_candidate_audit_v1") "unexpected audit format"
    Assert-Condition ([int]$audit.matched_anb_record_count -eq 508) "expected 508 matched ANB records"
    Assert-Condition ([int]$audit.compared_record_count -eq 508) "expected 508 compared ANB/CSAB records"
    Assert-Condition ($audit.include_frame_mismatch_resample -eq $true) "expected frame-count mismatch resampling enabled"
    Assert-Condition ([int]$audit.status_counts.direct_frame_aligned -eq 454) "expected 454 direct frame-aligned ANB/CSAB comparisons"
    Assert-Condition ([int]$audit.status_counts.resampled_frame_count_mismatch -eq 54) "expected 54 resampled frame-count mismatch ANB/CSAB comparisons"
    Assert-Condition ([int]$audit.channel_count -eq 67) "expected 67 ANB candidate channels"
    Assert-Condition ([int]$audit.candidate_channel_count -gt 0) "expected at least one correlated ANB channel candidate"
    Assert-Condition ([int]$audit.min_consensus_count -eq $MinConsensusCount) "expected configured ANB semantic consensus threshold"
    Assert-Condition ([int]$audit.candidate_channel_count -eq 58) "expected 58 ANB channels with at least one semantic candidate"
    Assert-Condition ([int]$audit.strong_candidate_count -eq 5826) "expected 5826 strong ANB channel/component candidates"
    Assert-Condition ([int]$audit.stable_candidate_channel_count -eq 44) "expected 44 stable ANB semantic channel mappings"
    Assert-Condition ([int]$audit.unstable_candidate_channel_count -eq 14) "expected 14 ANB channels with candidates but no stable consensus"
    Assert-Condition ([int]$audit.zero_candidate_channel_count -eq 9) "expected 9 ANB channels without CSAB component candidates"
    Assert-Condition ([int]$audit.resolved_zero_candidate_channel_count -eq 9) "expected 9 ANB zero-candidate channels resolved as non-transform fields"
    Assert-Condition ([int]$audit.resolved_static_zero_candidate_channel_count -eq 6) "expected 6 ANB zero-candidate channels resolved as static zero padding/reserved"
    Assert-Condition ([int]$audit.resolved_constant_control_tuple_channel_count -eq 3) "expected 3 ANB zero-candidate channels resolved as constant per-record control tuples"
    Assert-Condition ([int]$audit.unresolved_zero_candidate_channel_count -eq 0) "expected 0 ANB zero-candidate channels still requiring semantic source evidence"
    Assert-Condition ([int]$audit.unresolved_channel_count -eq 14) "expected 14 ANB channels unresolved for a final semantic channel contract after non-transform resolution"
    Assert-Condition ([int]$audit.unresolved_channel_worklist_count -eq 14) "expected 14 unresolved ANB channel worklist records"
    Assert-Condition ([int]$audit.zero_candidate_channel_class_counts.always_zero_static -eq 6) "expected 6 always-zero ANB channels without CSAB component candidates"
    Assert-Condition ([int]$audit.zero_candidate_channel_class_counts.low_cardinality_control_or_event -eq 3) "expected 3 low-cardinality ANB channels without CSAB component candidates"
    Assert-Condition ([int](Get-JsonValue $audit.zero_candidate_channel_class_counts "varying_unmatched_signal") -eq 0) "expected 0 varying unmatched ANB channels without CSAB component candidates"
    Assert-Condition ([int]$audit.zero_candidate_semantic_class_counts.static_zero_padding_or_reserved -eq 6) "expected 6 static-zero ANB padding/reserved candidates"
    Assert-Condition ([int]$audit.zero_candidate_semantic_class_counts.constant_per_record_control_tuple -eq 3) "expected 3 constant per-record ANB control tuple candidates"
    Assert-Condition ([int](Get-JsonValue $audit.zero_candidate_semantic_class_counts "varying_non_csab_curve_signal") -eq 0) "expected 0 varying non-CSAB ANB curve signal candidates"
    Assert-Condition ([int](Get-JsonValue $audit.zero_candidate_semantic_class_counts "varying_quantized_control_or_index") -eq 0) "expected 0 varying quantized ANB control/index candidates"
    Assert-Condition ([int]$audit.zero_candidate_resolution_status_counts.resolved_static_zero_padding_or_reserved -eq 6) "expected 6 ANB zero-candidate channels resolved as static-zero padding/reserved"
    Assert-Condition ([int]$audit.zero_candidate_resolution_status_counts.resolved_constant_per_record_control_tuple -eq 3) "expected 3 ANB zero-candidate channels resolved as constant per-record control tuples"
    Assert-Condition ([int](Get-JsonValue $audit.zero_candidate_resolution_status_counts "unresolved_requires_semantic_source") -eq 0) "expected 0 ANB zero-candidate channels still requiring semantic source evidence"
    Assert-Condition ([int](Get-JsonValue $audit.unresolved_zero_candidate_channel_class_counts "low_cardinality_control_or_event") -eq 0) "expected 0 unresolved low-cardinality ANB zero-candidate channels"
    Assert-Condition ([int](Get-JsonValue $audit.unresolved_zero_candidate_channel_class_counts "varying_unmatched_signal") -eq 0) "expected 0 unresolved varying ANB zero-candidate channels"
    Assert-Condition ([int]$audit.unstable_candidate_channel_class_counts.tied_top_component_consensus -eq 8) "expected 8 unstable ANB channels with tied top component consensus"
    Assert-Condition ([int]$audit.unstable_candidate_channel_class_counts.low_evidence_component_pair -eq 1) "expected 1 unstable ANB channel with low-evidence component pair"
    Assert-Condition ([int]$audit.unstable_candidate_channel_class_counts.diffuse_single_hit_components -eq 4) "expected 4 unstable ANB channels with diffuse single-hit components"
    Assert-Condition ([int]$audit.unstable_candidate_channel_class_counts.single_record_candidate -eq 1) "expected 1 unstable ANB channel with a single record candidate"
    Assert-Condition ([int]$audit.unstable_candidate_semantic_class_counts.component_collision_requires_disambiguation -eq 8) "expected 8 unstable ANB component-collision channels"
    Assert-Condition ([int]$audit.unstable_candidate_semantic_class_counts.diffuse_component_family_requires_constraints -eq 4) "expected 4 unstable ANB diffuse component-family channels"
    Assert-Condition ([int]$audit.unstable_candidate_semantic_class_counts.insufficient_record_support -eq 2) "expected 2 unstable ANB channels with insufficient record support"
    Assert-Condition ($audit.semantic_status.channel_contract_status -eq "partial") "expected partial ANB channel contract"
    Assert-Condition ($audit.semantic_status.promotion_status -eq "blocked") "expected ANB promotion blocked until all channels are stable or classified"
    $worklist = Import-Csv -LiteralPath $UnresolvedChannelCsvOutput
    Assert-Condition ($worklist.Count -eq 14) "expected 14 unresolved ANB channel CSV rows"
}

Write-Host "OOT3D Link child ANB semantic candidate audit: $Output"
Write-Host "OOT3D Link child unresolved ANB channel worklist: $UnresolvedChannelCsvOutput"
Write-Host "comparedRecords=$($audit.compared_record_count) candidateChannels=$($audit.candidate_channel_count) strongCandidates=$($audit.strong_candidate_count) stableCandidateChannels=$($audit.stable_candidate_channel_count)"
