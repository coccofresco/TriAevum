param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 10,
    [string]$ArchiveName = "oot3d_skinned_bind_pose_candidates.o2r",
    [string]$ArchivePrefix = "objects/oot3d/skinned_bind_pose",
    [string]$Name = "OOT3D Skinned Bind-Pose Candidates",
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

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D skinned bind-pose package verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "skinned_bind_pose_batch"
$manifestPath = Join-Path $outputRoot "skinned_bind_pose_batch_manifest.json"
$archiveOutput = Join-Path $outputRoot $ArchiveName
$packageAuditOutput = Join-Path $outputRoot "skinned_bind_pose_package_audit.json"
$summaryOutput = Join-Path $outputRoot "skinned_bind_pose_package_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "batch-skinned-bind-poses",
        $ActorRoot,
        "--output",
        $outputRoot,
        "--sample-limit",
        [string]$SampleLimit
    )

    Invoke-Oot3dTool -Arguments @(
        "pack-skinned-bind-pose-batch",
        $manifestPath,
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
        "audit-skinned-bind-pose-batch-package",
        $manifestPath,
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

Require-Path $manifestPath "Skinned bind-pose batch manifest"
Require-Path $archiveOutput "Skinned bind-pose package archive"
Require-Path $packageAuditOutput "Skinned bind-pose package audit"

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$packageAudit = Get-Content -LiteralPath $packageAuditOutput -Raw | ConvertFrom-Json
$archiveItem = Get-Item -LiteralPath $archiveOutput
$missingExports = @()
foreach ($record in $manifest.records) {
    if ($record.status -eq "exported" -and -not (Test-Path -LiteralPath $record.output)) {
        $missingExports += $record.output
    }
}

$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    manifest = $manifestPath
    export_dir = $manifest.export_dir
    archive = $archiveOutput
    package_audit = $packageAuditOutput
    archive_prefix = $ArchivePrefix
    archive_byte_length = $archiveItem.Length
    sample_limit = $SampleLimit
    file_count = $manifest.file_count
    archive_count = $manifest.archive_count
    loose_cmb_count = $manifest.loose_cmb_count
    counts = $manifest.counts
    influence_width_rows = $manifest.influence_width_rows
    nonzero_influence_rows = $manifest.nonzero_influence_rows
    record_count = $manifest.records.Count
    parse_error_count = $manifest.parse_error_count
    missing_export_count = $missingExports.Count
    package_audit_counts = [ordered]@{
        archive_entry_count = $packageAudit.archive_entry_count
        has_manifest = $packageAudit.has_manifest
        has_skinned_bind_pose_batch_manifest = $packageAudit.has_skinned_bind_pose_batch_manifest
        archived_skinned_bind_pose_batch_manifest_matches = $packageAudit.archived_skinned_bind_pose_batch_manifest_matches
        exported_record_count = $packageAudit.exported_record_count
        expected_export_count = $packageAudit.expected_export_count
        expected_unique_export_count = $packageAudit.expected_unique_export_count
        export_entry_count = $packageAudit.export_entry_count
        missing_source_export_file_count = $packageAudit.missing_source_export_file_count
        invalid_source_export_json_count = $packageAudit.invalid_source_export_json_count
        missing_export_entry_count = $packageAudit.missing_export_entry_count
        extra_archive_entry_count = $packageAudit.extra_archive_entry_count
        duplicate_archive_entry_count = $packageAudit.duplicate_archive_entry_count
        duplicate_expected_export_path_count = $packageAudit.duplicate_expected_export_path_count
        invalid_archived_export_json_count = $packageAudit.invalid_archived_export_json_count
        expected_export_format_counts = $packageAudit.expected_export_format_counts
        archived_export_format_counts = $packageAudit.archived_export_format_counts
        issue_counts = $packageAudit.issue_counts
    }
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($manifest.format -eq "oot3d_skinned_bind_pose_batch_v1") "unexpected manifest format"
    Assert-Condition ($summary.file_count -eq 349) "expected 349 actor files"
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.loose_cmb_count -eq 1) "expected 1 loose actor CMB"
    Assert-Condition ($summary.counts.considered_cmb -eq 1310) "expected 1310 considered actor CMB models"
    Assert-Condition ($summary.counts.parsed -eq 1310) "expected 1310 parsed actor CMB models"
    Assert-Condition ($summary.counts.exported -eq 202) "expected 202 skinned bind-pose exports"
    Assert-Condition ($summary.counts.skipped_unskinned -eq 1108) "expected 1108 unskinned actor CMB skips"
    Assert-Condition ($summary.counts.failed -eq 0) "expected 0 failed CMB exports"
    Assert-Condition ($summary.counts.failed_archives -eq 0) "expected 0 failed actor archives"
    Assert-Condition ($summary.counts.mesh_count -eq 583) "expected 583 skinned exported meshes"
    Assert-Condition ($summary.counts.skinned_primitive_count -eq 1097) "expected 1097 skinned primitives"
    Assert-Condition ($summary.counts.mode_1_primitive_count -eq 97) "expected 97 mode-1 skinned primitives"
    Assert-Condition ($summary.counts.mode_2_primitive_count -eq 1000) "expected 1000 mode-2 skinned primitives"
    Assert-Condition ($summary.counts.rigid_primitives_inside_skinned_meshes -eq 0) "expected 0 rigid primitives inside exported skinned meshes"
    Assert-Condition ($summary.counts.skinned_vertex_rows -eq 107504) "expected 107504 skinned vertex rows"
    Assert-Condition ($summary.counts.skeleton_bone_count -eq 3280) "expected 3280 exported skeleton bones"
    Assert-Condition ($summary.counts.finite_bind_world_matrix_entries -eq 52480) "expected 52480 finite bind matrix entries"
    Assert-Condition ($summary.counts.validation_error_count -eq 0) "expected 0 bind-pose validation errors"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "1") -eq 7421) "expected 7421 width-1 rows"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "2") -eq 47528) "expected 47528 width-2 rows"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "3") -eq 52345) "expected 52345 width-3 rows"
    Assert-Condition ((Get-JsonValue $summary.influence_width_rows "4") -eq 210) "expected 210 width-4 rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "1") -eq 66706) "expected 66706 one-weight rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "2") -eq 37364) "expected 37364 two-weight rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "3") -eq 3433) "expected 3433 three-weight rows"
    Assert-Condition ((Get-JsonValue $summary.nonzero_influence_rows "4") -eq 1) "expected 1 four-weight row"
    Assert-Condition ($summary.record_count -eq 202) "expected 202 manifest records"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 parse errors"
    Assert-Condition ($summary.missing_export_count -eq 0) "expected every manifest export file to exist"

    Assert-Condition ($summary.package_audit_counts.archive_entry_count -eq 204) "expected 202 exports plus 2 manifests"
    Assert-Condition ($summary.package_audit_counts.has_manifest) "expected archive manifest.json"
    Assert-Condition ($summary.package_audit_counts.has_skinned_bind_pose_batch_manifest) "expected archived skinned bind-pose batch manifest"
    Assert-Condition ($summary.package_audit_counts.archived_skinned_bind_pose_batch_manifest_matches) "expected archived skinned bind-pose manifest to match source"
    Assert-Condition ($summary.package_audit_counts.exported_record_count -eq 202) "expected 202 exported records"
    Assert-Condition ($summary.package_audit_counts.expected_export_count -eq 202) "expected 202 expected bind-pose JSON entries"
    Assert-Condition ($summary.package_audit_counts.expected_unique_export_count -eq 202) "expected unique bind-pose archive paths"
    Assert-Condition ($summary.package_audit_counts.export_entry_count -eq 202) "expected every bind-pose export in archive"
    Assert-Condition ($summary.package_audit_counts.missing_source_export_file_count -eq 0) "expected no missing generated bind-pose files"
    Assert-Condition ($summary.package_audit_counts.invalid_source_export_json_count -eq 0) "expected no invalid source bind-pose JSON"
    Assert-Condition ($summary.package_audit_counts.missing_export_entry_count -eq 0) "expected no missing archived bind-pose entries"
    Assert-Condition ($summary.package_audit_counts.extra_archive_entry_count -eq 0) "expected no extra archive entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_archive_entry_count -eq 0) "expected no duplicate archive entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_expected_export_path_count -eq 0) "expected no duplicate expected bind-pose paths"
    Assert-Condition ($summary.package_audit_counts.invalid_archived_export_json_count -eq 0) "expected no invalid archived bind-pose JSON"
    Assert-Condition ((Get-JsonValue $summary.package_audit_counts.archived_export_format_counts "oot3d_skinned_bind_pose_export_v1") -eq 202) "expected 202 archived skinned bind-pose exports"
    Assert-Condition ($summary.package_audit_counts.issue_counts.total -eq 0) "expected zero package audit issues"
}

Write-Host "OOT3D skinned bind-pose package archive: $archiveOutput"
Write-Host "OOT3D skinned bind-pose package audit: $packageAuditOutput"
Write-Host "OOT3D skinned bind-pose package summary: $summaryOutput"
