param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ArchiveName = "oot3d_rigid_animation_candidates.o2r",
    [string]$ArchivePrefix = "animations/oot3d/csab/rigid",
    [string]$Name = "OOT3D Rigid Animation Candidates",
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
        throw "OOT3D rigid animation package verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "rigid_animation_batch"
$manifestOutput = Join-Path $outputRoot "csab_rigid_track_batch_manifest.json"
$archiveOutput = Join-Path $outputRoot $ArchiveName
$packageAuditOutput = Join-Path $outputRoot "rigid_animation_package_audit.json"
$summaryOutput = Join-Path $outputRoot "rigid_animation_package_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "batch-csab-rigid-tracks",
        $ActorRoot,
        "--output",
        $outputRoot
    )

    Invoke-Oot3dTool -Arguments @(
        "pack-csab-track-batch",
        $manifestOutput,
        "--output",
        $archiveOutput,
        "--archive-prefix",
        $ArchivePrefix,
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
        $ArchivePrefix
    )
}
finally {
    Pop-Location
}

Require-Path $manifestOutput "Rigid CSAB track batch manifest"
Require-Path $archiveOutput "Rigid CSAB animation package archive"
Require-Path $packageAuditOutput "Rigid CSAB animation package audit"

$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$packageAudit = Get-Content -LiteralPath $packageAuditOutput -Raw | ConvertFrom-Json
$archiveItem = Get-Item -LiteralPath $archiveOutput
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    archive = $archiveOutput
    package_audit = $packageAuditOutput
    archive_prefix = $ArchivePrefix
    archive_byte_length = $archiveItem.Length
    considered_csab = $manifest.considered_csab
    exported = $manifest.exported
    failed = $manifest.failed
    target_unresolved_or_missing = $manifest.target_unresolved_or_missing
    target_needs_skinning_support = $manifest.target_needs_skinning_support
    target_needs_unknown_support = $manifest.target_needs_unknown_support
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
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.considered_csab -eq 2465) "expected 2465 considered CSAB payloads"
    Assert-Condition ($summary.exported -eq 44) "expected 44 rigid CSAB track exports"
    Assert-Condition ($summary.failed -eq 0) "expected 0 failed rigid CSAB track exports"
    Assert-Condition ($summary.target_unresolved_or_missing -eq 150) "expected 150 unresolved or missing CSAB targets"
    Assert-Condition ($summary.target_needs_skinning_support -eq 2271) "expected 2271 CSAB targets blocked by skinning"
    Assert-Condition ($summary.target_needs_unknown_support -eq 0) "expected 0 CSAB targets blocked by unknown support"
    Assert-Condition ($summary.counts.track_count -eq 237) "expected 237 exported bone tracks"
    Assert-Condition ($summary.counts.channel_count -eq 1311) "expected 1311 exported f32 channels"
    Assert-Condition ($summary.counts.keyframe_count -eq 2922) "expected 2922 f32 keyframes"
    Assert-Condition ($summary.counts.frame_slot_count -eq 3714) "expected 3714 frame slots"
    Assert-Condition ($summary.counts.non_f32_channel_blocks -eq 0) "expected 0 non-f32 channel blocks"

    Assert-Condition ($summary.package_audit_counts.archive_entry_count -eq 46) "expected 44 tracks plus 2 manifests"
    Assert-Condition ($summary.package_audit_counts.has_manifest) "expected archive manifest.json"
    Assert-Condition ($summary.package_audit_counts.has_csab_track_batch_manifest) "expected archived CSAB track batch manifest"
    Assert-Condition ($summary.package_audit_counts.archived_csab_track_batch_manifest_matches) "expected archived CSAB track manifest to match source"
    Assert-Condition ($summary.package_audit_counts.exported_record_count -eq 44) "expected 44 exported records"
    Assert-Condition ($summary.package_audit_counts.expected_track_count -eq 44) "expected 44 expected track JSON entries"
    Assert-Condition ($summary.package_audit_counts.expected_unique_track_count -eq 44) "expected unique track archive paths"
    Assert-Condition ($summary.package_audit_counts.track_entry_count -eq 44) "expected every track in archive"
    Assert-Condition ($summary.package_audit_counts.missing_source_track_file_count -eq 0) "expected no missing generated track files"
    Assert-Condition ($summary.package_audit_counts.invalid_source_track_json_count -eq 0) "expected no invalid source track JSON"
    Assert-Condition ($summary.package_audit_counts.missing_track_entry_count -eq 0) "expected no missing archived track entries"
    Assert-Condition ($summary.package_audit_counts.extra_archive_entry_count -eq 0) "expected no extra archive entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_archive_entry_count -eq 0) "expected no duplicate archive entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_expected_track_path_count -eq 0) "expected no duplicate expected track paths"
    Assert-Condition ($summary.package_audit_counts.invalid_archived_track_json_count -eq 0) "expected no invalid archived track JSON"
    Assert-Condition ((Get-JsonValue $summary.package_audit_counts.archived_track_format_counts "oot3d_csab_rigid_track_export_v1") -eq 44) "expected 44 archived rigid track exports"
    Assert-Condition ($summary.package_audit_counts.issue_counts.total -eq 0) "expected zero package audit issues"
}

Write-Host "OOT3D rigid animation package archive: $archiveOutput"
Write-Host "OOT3D rigid animation package audit: $packageAuditOutput"
Write-Host "OOT3D rigid animation package summary: $summaryOutput"
