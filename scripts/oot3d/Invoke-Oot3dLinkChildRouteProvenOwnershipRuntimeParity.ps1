param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$CharacterManifest = "",
    [string]$PackageRoot = "",
    [string]$OutputRoot = "",
    [int]$FrameStep = 1,
    [int]$SampleLimit = 100,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
$characterRoot = Join-Path $WorkRoot "character_conversion"
if ([string]::IsNullOrWhiteSpace($CharacterManifest)) {
    $CharacterManifest = Join-Path $characterRoot "link_child_character_conversion_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $characterRoot "route_proven_ownership_package"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $characterRoot "route_proven_ownership_runtime_parity"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child route-proven ownership runtime parity failed: $Message"
    }
}

function Convert-CsabNameToSlug([string]$Name) {
    $slug = ($Name -replace '[^A-Za-z0-9]+', '_').Trim('_').ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "track"
    }
    return $slug
}

function Resolve-PackageTrackPath([string]$Root, [string]$TrackPath) {
    if ([System.IO.Path]::IsPathRooted($TrackPath)) {
        return $TrackPath
    }
    return Join-Path $Root ($TrackPath -replace '/', '\')
}

function Invoke-Oot3dTool([string[]]$Arguments) {
    Write-Host "python -m oot3d_asset_tool $($Arguments -join ' ')"
    & python -m oot3d_asset_tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $CharacterManifest "Link child character conversion manifest"
Require-Path $PackageRoot "Route-proven ownership package root"

$packageSummaryPath = Join-Path $PackageRoot "link_child_route_proven_ownership_package_summary.json"
$packageAuditPath = Join-Path $PackageRoot "link_child_route_proven_ownership_package_audit.json"
$trackBatchManifestPath = Join-Path $PackageRoot "link_child_route_proven_ownership_track_batch_manifest.json"
$archivePath = Join-Path $PackageRoot "oot3d_link_child_route_proven_ownership_candidates.o2r"
$summaryOutput = Join-Path $OutputRoot "link_child_route_proven_ownership_runtime_parity_summary.json"
$auditRoot = Join-Path $OutputRoot "audits"
$glbRoot = Join-Path $OutputRoot "glb"

Require-Path $packageSummaryPath "Route-proven ownership package summary"
Require-Path $packageAuditPath "Route-proven ownership package audit"
Require-Path $trackBatchManifestPath "Route-proven ownership track batch manifest"
Require-Path $archivePath "Route-proven ownership package archive"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
New-Item -ItemType Directory -Force -Path $glbRoot | Out-Null

$packageSummary = Get-Content -LiteralPath $packageSummaryPath -Raw | ConvertFrom-Json
$packageAudit = Get-Content -LiteralPath $packageAuditPath -Raw | ConvertFrom-Json
$trackBatchManifest = Get-Content -LiteralPath $trackBatchManifestPath -Raw | ConvertFrom-Json

if ($Verify) {
    Assert-Condition ($packageSummary.status -eq "package_ready_pending_runtime_skinned_parity") "expected package to be ready and pending runtime/skinned parity"
    Assert-Condition ([int]$packageSummary.exported -eq 5) "expected 5 packaged route-proven tracks"
    Assert-Condition ([int]$packageSummary.failed -eq 0) "expected no package export failures"
    Assert-Condition ([int]$packageSummary.package_audit_counts.issue_counts.total -eq 0) "expected package audit issue count 0"
    Assert-Condition ($trackBatchManifest.status -eq "route_proven_ownership_track_batch_ready") "expected route-proven track batch ready"
    Assert-Condition ([int]$trackBatchManifest.exported -eq 5) "expected 5 route-proven track batch records"
    Assert-Condition ([int]$packageAudit.issue_counts.total -eq 0) "expected package audit total issues 0"
}

$records = @()
$allValid = $true
$totalPrimitiveCount = 0
$totalSkinnedPrimitiveCount = 0
$totalRigidPrimitiveCount = 0
$totalTriangleCount = 0
$totalVertexCount = 0
$totalMorphTargetCount = 0
$totalRuntimeFrameSlotCount = 0
$totalRuntimeTargetFrameCount = 0
$totalAnimatedPositionRows = 0
$totalFinitePositionRows = 0
$totalFiniteNormalRows = 0
$totalIssueCount = 0

foreach ($record in @($trackBatchManifest.records)) {
    if ($record.status -ne "exported") {
        $allValid = $false
        continue
    }
    $csabName = [string]$record.output_csab_name
    $slug = Convert-CsabNameToSlug $csabName
    $trackPath = Resolve-PackageTrackPath $PackageRoot ([string]$record.track_export)
    $auditOutput = Join-Path $auditRoot "link_child_${slug}_runtime_track_audit.json"
    $runtimeGlbOutput = Join-Path $glbRoot "link_child_${slug}_minimal_runtime_track.glb"

    Require-Path $trackPath "Packaged route-proven ownership track"

    Push-Location $ToolRoot
    try {
        $env:PYTHONPATH = Join-Path $ToolRoot "src"
        Invoke-Oot3dTool -Arguments @(
            "audit-minimal-runtime-track-export",
            $CharacterManifest,
            $trackPath,
            "--output",
            $auditOutput,
            "--runtime-glb-output",
            $runtimeGlbOutput,
            "--csab-name",
            $csabName,
            "--frame-step",
            [string]$FrameStep,
            "--fps",
            "60",
            "--interpolation",
            "STEP",
            "--sample-limit",
            [string]$SampleLimit
        )
    }
    finally {
        Pop-Location
    }

    Require-Path $auditOutput "Route-proven ownership runtime parity audit"
    Require-Path $runtimeGlbOutput "Route-proven ownership runtime GLB"
    $audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
    $runtimeCounts = $audit.runtime_manifest.counts
    $comparison = $audit.comparison
    $inspection = $audit.inspection
    $runtimeFrameSlotCount = @($audit.runtime_manifest.sample_frames).Count
    $runtimeTargetFrameCount = @($audit.runtime_manifest.target_frames).Count

    if ($audit.status -ne "valid") {
        $allValid = $false
    }
    if ($Verify) {
        Assert-Condition ($audit.status -eq "valid") "expected valid runtime track audit for $csabName"
        Assert-Condition ($audit.format -eq "oot3d_minimal_runtime_track_export_audit_v1") "unexpected runtime track audit format for $csabName"
        Assert-Condition ($audit.csab_name -eq $csabName) "runtime track audit CSAB name mismatch for $csabName"
        Assert-Condition ([int]$runtimeCounts.primitive_count -eq 12) "expected 12 selected primitives for $csabName"
        Assert-Condition ([int]$runtimeCounts.skinned_primitive_count -eq 10) "expected 10 selected skinned primitives for $csabName"
        Assert-Condition ([int]$runtimeCounts.rigid_primitive_count -eq 2) "expected 2 selected rigid primitives for $csabName"
        Assert-Condition ([int]$record.frame_slot_count -eq $runtimeFrameSlotCount) "expected runtime frame-slot count to match package manifest for $csabName"
        Assert-Condition ([int]$comparison.issue_count -eq 0) "expected zero runtime track issues for $csabName"
        Assert-Condition ([int]$inspection.finite_position_row_count -eq [int]$inspection.animated_position_row_count) "expected all runtime positions finite for $csabName"
        Assert-Condition ([int]$inspection.finite_normal_row_count -eq [int]$inspection.animated_normal_row_count) "expected all runtime normals finite for $csabName"
        Assert-Condition ($record.source_sample_parity_status -eq "passed") "expected source-sample parity passed for $csabName"
        Assert-Condition ($record.output_frame_dense_validation_status -eq "valid") "expected frame-dense validation valid for $csabName"
    }

    $totalPrimitiveCount += [int]$runtimeCounts.primitive_count
    $totalSkinnedPrimitiveCount += [int]$runtimeCounts.skinned_primitive_count
    $totalRigidPrimitiveCount += [int]$runtimeCounts.rigid_primitive_count
    $totalTriangleCount += [int]$runtimeCounts.triangle_count
    $totalVertexCount += [int]$runtimeCounts.vertex_count
    $totalMorphTargetCount += [int]$runtimeCounts.morph_target_count
    $totalRuntimeFrameSlotCount += $runtimeFrameSlotCount
    $totalRuntimeTargetFrameCount += $runtimeTargetFrameCount
    $totalAnimatedPositionRows += [int]$inspection.animated_position_row_count
    $totalFinitePositionRows += [int]$inspection.finite_position_row_count
    $totalFiniteNormalRows += [int]$inspection.finite_normal_row_count
    $totalIssueCount += [int]$comparison.issue_count

    $records += [ordered]@{
        status = $audit.status
        n64_name = $record.n64_name
        n64_data_name = $record.n64_data_name
        source_csab_name = $record.source_csab_name
        output_csab_name = $record.output_csab_name
        materialization_class = $record.materialization_class
        route_callsite_contexts = $record.route_callsite_contexts
        track_export = $trackPath
        audit = $auditOutput
        runtime_glb = $runtimeGlbOutput
        package_frame_slot_count = [int]$record.frame_slot_count
        runtime_frame_slot_count = $runtimeFrameSlotCount
        runtime_target_frame_count = $runtimeTargetFrameCount
        primitive_count = [int]$runtimeCounts.primitive_count
        skinned_primitive_count = [int]$runtimeCounts.skinned_primitive_count
        rigid_primitive_count = [int]$runtimeCounts.rigid_primitive_count
        animated_position_row_count = [int]$inspection.animated_position_row_count
        finite_position_row_count = [int]$inspection.finite_position_row_count
        finite_normal_row_count = [int]$inspection.finite_normal_row_count
        issue_count = [int]$comparison.issue_count
        runtime_acceptance_status = "offline_runtime_skinned_pose_parity_valid_pending_installed_runtime_draw_capture"
    }
}

$status = if ($allValid -and $totalIssueCount -eq 0) {
    "offline_runtime_skinned_pose_parity_valid"
} else {
    "offline_runtime_skinned_pose_parity_invalid"
}

$summary = [ordered]@{
    format = "oot3d_link_child_route_proven_ownership_runtime_parity_summary_v1"
    status = $status
    runtime_acceptance_status = if ($status -eq "offline_runtime_skinned_pose_parity_valid") {
        "offline_runtime_skinned_pose_parity_valid_pending_installed_runtime_draw_capture"
    } else {
        "offline_runtime_skinned_pose_parity_invalid"
    }
    character_manifest = (Resolve-Path $CharacterManifest).Path
    package_root = (Resolve-Path $PackageRoot).Path
    package_summary = (Resolve-Path $packageSummaryPath).Path
    package_audit = (Resolve-Path $packageAuditPath).Path
    track_batch_manifest = (Resolve-Path $trackBatchManifestPath).Path
    archive = (Resolve-Path $archivePath).Path
    output_root = $OutputRoot
    exported = $records.Count
    failed = @($records | Where-Object { $_.status -ne "valid" }).Count
    counts = [ordered]@{
        runtime_track_count = $records.Count
        valid_runtime_track_count = @($records | Where-Object { $_.status -eq "valid" }).Count
        runtime_primitive_count = $totalPrimitiveCount
        runtime_skinned_primitive_count = $totalSkinnedPrimitiveCount
        runtime_rigid_primitive_count = $totalRigidPrimitiveCount
        runtime_triangle_count = $totalTriangleCount
        runtime_vertex_count = $totalVertexCount
        runtime_morph_target_count = $totalMorphTargetCount
        runtime_frame_slot_count = $totalRuntimeFrameSlotCount
        runtime_target_frame_count = $totalRuntimeTargetFrameCount
        animated_position_row_count = $totalAnimatedPositionRows
        finite_position_row_count = $totalFinitePositionRows
        finite_normal_row_count = $totalFiniteNormalRows
        issue_count = $totalIssueCount
    }
    records = $records
    next_gate = "Mount the candidate package in the installed Shipwright runtime and capture/diff real draw output before promotion."
}

$summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.status -eq "offline_runtime_skinned_pose_parity_valid") "expected valid route-proven ownership runtime parity summary"
    Assert-Condition ([int]$summary.exported -eq 5) "expected 5 route-proven ownership runtime parity records"
    Assert-Condition ([int]$summary.failed -eq 0) "expected no route-proven ownership runtime parity failures"
    Assert-Condition ([int]$summary.counts.runtime_track_count -eq 5) "expected 5 runtime track records"
    Assert-Condition ([int]$summary.counts.valid_runtime_track_count -eq 5) "expected 5 valid runtime track records"
    Assert-Condition ([int]$summary.counts.runtime_primitive_count -eq 60) "expected 60 selected runtime primitives"
    Assert-Condition ([int]$summary.counts.runtime_skinned_primitive_count -eq 50) "expected 50 selected skinned runtime primitives"
    Assert-Condition ([int]$summary.counts.runtime_rigid_primitive_count -eq 10) "expected 10 selected rigid runtime primitives"
    Assert-Condition ([int]$summary.counts.runtime_frame_slot_count -eq 159) "expected 159 runtime frame slots"
    Assert-Condition ([int]$summary.counts.issue_count -eq 0) "expected zero runtime parity issues"
    Assert-Condition ([int]$summary.counts.finite_position_row_count -eq [int]$summary.counts.animated_position_row_count) "expected all runtime positions finite"
}

Write-Host "OOT3D Link child route-proven ownership runtime parity summary: $summaryOutput"
foreach ($record in $records) {
    Write-Host "OOT3D Link child route-proven ownership runtime parity audit: $($record.audit)"
}
