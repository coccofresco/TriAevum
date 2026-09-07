param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$CharacterProfileO2r = "",
    [string]$ArchiveName = "oot3d_link_child_pose_source_risk_diagnostic_candidates.o2r",
    [string]$Name = "OOT3D Link Child Pose Source-Risk Diagnostic Candidates",
    [string]$Author = "local",
    [string]$Version = "0.1.0",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}

$characterRoot = Join-Path $WorkRoot "character_conversion"
if ([string]::IsNullOrWhiteSpace($CharacterProfileO2r)) {
    $CharacterProfileO2r = Join-Path $characterRoot "oot3d_link_child_character_profile.o2r"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child pose/source-risk package verification failed: $Message"
    }
}

function Normalize-ResourcePath([string]$Path) {
    return $Path.Replace("\", "/").Trim("/")
}

function Invoke-Oot3dTool([string[]]$Arguments) {
    Write-Host "python -m oot3d_asset_tool $($Arguments -join ' ')"
    & python -m oot3d_asset_tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $CharacterProfileO2r "Link child character profile O2R"

$frontierCsv = Join-Path $characterRoot "link_child_animation_semantic_blocked_resolution_frontier.csv"
Require-Path $frontierCsv "Link child semantic blocked frontier CSV"

$outputRoot = Join-Path $characterRoot "pose_source_risk_diagnostic_package"
$generatedTrackRoot = Join-Path $outputRoot "generated_tracks"
$diagnosticPlanOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_plan.json"
$diagnosticPlanCsvOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_plan.csv"
$diagnosticTrackManifestOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_track_manifest.json"
$diagnosticTrackCsvOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_track_manifest.csv"
$diagnosticBuildSummaryOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_build_summary.json"
$manifestOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_track_batch_manifest.json"
$archiveOutput = Join-Path $outputRoot $ArchiveName
$packageAuditOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_package_audit.json"
$summaryOutput = Join-Path $outputRoot "link_child_pose_source_risk_diagnostic_package_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot, $generatedTrackRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "build-link-child-pose-source-risk-diagnostic-tracks",
        $frontierCsv,
        $CharacterProfileO2r,
        "--output-dir",
        $generatedTrackRoot,
        "--summary-output",
        $diagnosticBuildSummaryOutput,
        "--plan-output",
        $diagnosticPlanOutput,
        "--plan-csv-output",
        $diagnosticPlanCsvOutput,
        "--track-manifest-output",
        $diagnosticTrackManifestOutput,
        "--track-csv-output",
        $diagnosticTrackCsvOutput
    )
}
finally {
    Pop-Location
}

Require-Path $diagnosticBuildSummaryOutput "Pose/source-risk diagnostic build summary"
Require-Path $diagnosticTrackManifestOutput "Pose/source-risk diagnostic track manifest"

$frontierRowsByName = @{}
foreach ($row in @(Import-Csv -LiteralPath $frontierCsv | Where-Object { $_.frontier_gate -eq "pose_metric_or_source_identity" })) {
    $frontierRowsByName[[string]$row.n64_name] = $row
}
$diagnosticBuildSummary = Get-Content -LiteralPath $diagnosticBuildSummaryOutput -Raw | ConvertFrom-Json
$diagnosticTrackManifest = Get-Content -LiteralPath $diagnosticTrackManifestOutput -Raw | ConvertFrom-Json

$records = @()
$totalTrackCount = 0
$totalChannelCount = 0
$totalConstChannelCount = 0
$totalKeyedChannelCount = 0
$totalKeyframeCount = 0
$totalFrameSlotCount = 0
$totalFrameSampleCount = 0
$totalSourceSampleParityCheckedChannelCount = 0
$totalSourceSampleParityCheckedKeyCount = 0

foreach ($trackRow in @($diagnosticTrackManifest.rows)) {
    $n64Name = [string]$trackRow.n64_name
    Assert-Condition ($frontierRowsByName.ContainsKey($n64Name)) "missing frontier row for $n64Name"
    Assert-Condition ($trackRow.materialization_status -eq "materialized_track_written") "expected materialized track for $n64Name"
    $frontierRow = $frontierRowsByName[$n64Name]
    $sourceTrackFile = [string]$trackRow.output_track_file
    $resourcePath = Normalize-ResourcePath ([string]$trackRow.output_track_resource_path)
    Require-Path $sourceTrackFile "Pose/source-risk diagnostic track for $n64Name"

    $targetTrackPath = Join-Path $outputRoot ($resourcePath.Replace("/", "\"))
    $targetTrackDir = Split-Path -Parent $targetTrackPath
    New-Item -ItemType Directory -Force -Path $targetTrackDir | Out-Null
    Copy-Item -LiteralPath $sourceTrackFile -Destination $targetTrackPath -Force

    $record = [ordered]@{
        status = "exported"
        n64_name = $n64Name
        n64_stem = [string]$frontierRow.n64_stem
        n64_data_name = [string]$frontierRow.n64_data_name
        source_csab_name = [string]$frontierRow.source_csab_name
        output_csab_name = [string]$trackRow.output_csab_name
        source_contract_kind = [string]$frontierRow.source_contract_kind
        frontier_class = [string]$frontierRow.frontier_class
        frontier_gate = [string]$frontierRow.frontier_gate
        reference_envelope_status = [string]$frontierRow.reference_envelope_status
        materialization_class = [string]$trackRow.materialization_class
        track_export = $resourcePath
        counts = [ordered]@{
            track_count = [int]$trackRow.output_track_count
            channel_count = [int]$trackRow.output_channel_count
            const_channel_count = [int]$trackRow.output_const_channel_count
            keyed_channel_count = [int]$trackRow.output_keyed_channel_count
            keyframe_count = [int]$trackRow.output_keyframe_count
        }
        frame_slot_count = [int]$trackRow.output_frame_slot_count
        frame_sample_count = [int]$trackRow.frame_sample_count
        source_sample_parity_status = [string]$trackRow.source_sample_parity_status
        output_frame_dense_validation_status = [string]$trackRow.output_frame_dense_validation_status
        runtime_acceptance_status = "pose_source_risk_diagnostic_package_candidate_not_runtime_accepted"
    }
    $records += [pscustomobject]$record

    $totalTrackCount += [int]$trackRow.output_track_count
    $totalChannelCount += [int]$trackRow.output_channel_count
    $totalConstChannelCount += [int]$trackRow.output_const_channel_count
    $totalKeyedChannelCount += [int]$trackRow.output_keyed_channel_count
    $totalKeyframeCount += [int]$trackRow.output_keyframe_count
    $totalFrameSlotCount += [int]$trackRow.output_frame_slot_count
    $totalFrameSampleCount += [int]$trackRow.frame_sample_count
    $totalSourceSampleParityCheckedChannelCount += [int]$trackRow.source_sample_parity_checked_channel_count
    $totalSourceSampleParityCheckedKeyCount += [int]$trackRow.source_sample_parity_checked_key_count
}

$manifest = [ordered]@{
    format = "oot3d_link_child_pose_source_risk_diagnostic_track_batch_v1"
    base_track_batch_format = "oot3d_csab_skeleton_track_batch_v1"
    status = "pose_source_risk_diagnostic_track_batch_ready"
    policy = [ordered]@{
        scope = "Link child outside-envelope pose/source identity diagnostic derivatives"
        semantic_effect = "diagnostic package only; it does not accept source identity or promote mappings"
        runtime_acceptance_policy = "rows remain diagnostic until alternate-source, route, or installed-runtime evidence is accepted"
    }
    source_frontier_csv = $frontierCsv
    source_diagnostic_build_summary = $diagnosticBuildSummaryOutput
    source_diagnostic_track_manifest = $diagnosticTrackManifestOutput
    exported = $records.Count
    failed = 0
    counts = [ordered]@{
        track_count = $totalTrackCount
        channel_count = $totalChannelCount
        const_channel_count = $totalConstChannelCount
        keyed_channel_count = $totalKeyedChannelCount
        keyframe_count = $totalKeyframeCount
        frame_slot_count = $totalFrameSlotCount
        frame_sample_count = $totalFrameSampleCount
        source_sample_parity_checked_channel_count = $totalSourceSampleParityCheckedChannelCount
        source_sample_parity_checked_key_count = $totalSourceSampleParityCheckedKeyCount
    }
    records = $records
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestOutput -Encoding UTF8

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "pack-csab-track-batch",
        $manifestOutput,
        "--output",
        $archiveOutput,
        "--archive-prefix",
        "",
        "--name",
        $Name,
        "--author",
        $Author,
        "--version",
        $Version
    )

    Invoke-Oot3dTool -Arguments @(
        "audit-csab-track-batch-package",
        $manifestOutput,
        $archiveOutput,
        "--output",
        $packageAuditOutput,
        "--archive-prefix",
        ""
    )
}
finally {
    Pop-Location
}

Require-Path $archiveOutput "Link child pose/source-risk diagnostic package archive"
Require-Path $packageAuditOutput "Link child pose/source-risk diagnostic package audit"
$packageAudit = Get-Content -LiteralPath $packageAuditOutput -Raw | ConvertFrom-Json
$archiveItem = Get-Item -LiteralPath $archiveOutput

$summary = [ordered]@{
    format = "oot3d_link_child_pose_source_risk_diagnostic_package_summary_v1"
    status = if ([int]$packageAudit.issue_counts.total -eq 0 -and $diagnosticBuildSummary.status -eq "pose_source_risk_diagnostic_tracks_ready") { "package_ready_pending_runtime_skinned_parity" } else { "package_invalid" }
    output_root = $outputRoot
    diagnostic_build_summary = $diagnosticBuildSummaryOutput
    diagnostic_plan = $diagnosticPlanOutput
    diagnostic_track_manifest = $diagnosticTrackManifestOutput
    manifest = $manifestOutput
    archive = $archiveOutput
    package_audit = $packageAuditOutput
    archive_byte_length = $archiveItem.Length
    exported = $manifest.exported
    failed = $manifest.failed
    counts = $manifest.counts
    package_audit_counts = [ordered]@{
        archive_entry_count = $packageAudit.archive_entry_count
        has_manifest = $packageAudit.has_manifest
        has_csab_track_batch_manifest = $packageAudit.has_csab_track_batch_manifest
        archived_csab_track_batch_manifest_matches = $packageAudit.archived_csab_track_batch_manifest_matches
        exported_record_count = $packageAudit.exported_record_count
        expected_track_count = $packageAudit.expected_track_count
        expected_unique_track_count = $packageAudit.expected_unique_track_count
        track_entry_count = $packageAudit.track_entry_count
        missing_source_track_file_count = $packageAudit.missing_source_track_file_count
        invalid_source_track_json_count = $packageAudit.invalid_source_track_json_count
        missing_track_entry_count = $packageAudit.missing_track_entry_count
        extra_archive_entry_count = $packageAudit.extra_archive_entry_count
        duplicate_archive_entry_count = $packageAudit.duplicate_archive_entry_count
        duplicate_expected_track_path_count = $packageAudit.duplicate_expected_track_path_count
        invalid_archived_track_json_count = $packageAudit.invalid_archived_track_json_count
        expected_track_format_counts = $packageAudit.expected_track_format_counts
        archived_track_format_counts = $packageAudit.archived_track_format_counts
        issue_counts = $packageAudit.issue_counts
    }
    runtime_acceptance_status = "pending_runtime_skinned_pose_parity"
    next_gate = "Run offline runtime/skinned pose parity, then find alternate source or installed runtime evidence before promotion."
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($diagnosticBuildSummary.status -eq "pose_source_risk_diagnostic_tracks_ready") "expected diagnostic build ready"
    Assert-Condition ($diagnosticBuildSummary.pose_source_risk_frontier_row_count -eq 16) "expected sixteen pose/source-risk frontier rows"
    Assert-Condition ($summary.status -eq "package_ready_pending_runtime_skinned_parity") "expected ready package summary"
    Assert-Condition ($summary.exported -eq 16) "expected sixteen pose/source-risk tracks"
    Assert-Condition ($summary.failed -eq 0) "expected zero failed pose/source-risk tracks"
    Assert-Condition ($summary.package_audit_counts.archive_entry_count -eq 18) "expected 16 tracks plus 2 manifests"
    Assert-Condition ($summary.package_audit_counts.has_manifest) "expected archive manifest"
    Assert-Condition ($summary.package_audit_counts.has_csab_track_batch_manifest) "expected archived track batch manifest"
    Assert-Condition ($summary.package_audit_counts.archived_csab_track_batch_manifest_matches) "expected archived batch manifest to match"
    Assert-Condition ($summary.package_audit_counts.exported_record_count -eq 16) "expected sixteen exported package records"
    Assert-Condition ($summary.package_audit_counts.expected_track_count -eq 16) "expected sixteen package track entries"
    Assert-Condition ($summary.package_audit_counts.expected_unique_track_count -eq 16) "expected sixteen unique package track entries"
    Assert-Condition ($summary.package_audit_counts.track_entry_count -eq 16) "expected sixteen archived track entries"
    Assert-Condition ($summary.package_audit_counts.issue_counts.total -eq 0) "expected zero package audit issues"
    Assert-Condition ($summary.counts.track_count -gt 0) "expected generated tracks"
    Assert-Condition ($summary.counts.channel_count -gt 0) "expected generated channels"
    Assert-Condition ($summary.counts.keyframe_count -gt 0) "expected generated keyframes"
}

Write-Host "OOT3D Link child pose/source-risk package: $archiveOutput"
Write-Host "OOT3D Link child pose/source-risk package summary: $summaryOutput"
Write-Host "exported=$($summary.exported) issues=$($summary.package_audit_counts.issue_counts.total)"
