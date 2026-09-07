param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ResourcePrefix = "objects/oot3d_actor_static",
    [string]$ArchiveName = "oot3d_static_actor_candidates.o2r",
    [string]$Name = "OOT3D Static Actor Candidates",
    [string]$Author = "local",
    [string]$Version = "0.1.0",
    [int]$Limit = 0,
    [switch]$NoTextures,
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
        throw "OOT3D static actor package verification failed: $Message"
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

$outputRoot = Join-Path $WorkRoot "static_actor_export"
$convertedOutput = Join-Path $outputRoot "converted"
$manifestPath = Join-Path $convertedOutput "batch_manifest.json"
$resourceAuditOutput = Join-Path $outputRoot "static_actor_export_resource_audit.json"
$archiveOutput = Join-Path $outputRoot $ArchiveName
$packageAuditOutput = Join-Path $outputRoot "static_actor_package_audit.json"
$summaryOutput = Join-Path $outputRoot "static_actor_package_summary.json"
New-Item -ItemType Directory -Force -Path $convertedOutput | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $batchArgs = @(
        "batch-static",
        $ActorRoot,
        "--output",
        $convertedOutput,
        "--resource-prefix",
        $ResourcePrefix
    )
    if ($Limit -gt 0) {
        $batchArgs += @("--limit", "$Limit")
    }
    if ($NoTextures) {
        $batchArgs += "--no-textures"
    }
    Invoke-Oot3dTool -Arguments $batchArgs

    Invoke-Oot3dTool -Arguments @(
        "audit-static-batch",
        $manifestPath,
        "--output",
        $resourceAuditOutput
    )

    Invoke-Oot3dTool -Arguments @(
        "pack-static-batch",
        $manifestPath,
        "--output",
        $archiveOutput,
        "--name",
        $Name,
        "--author",
        $Author,
        "--version",
        $Version
    )

    Invoke-Oot3dTool -Arguments @(
        "audit-static-batch-package",
        $manifestPath,
        $archiveOutput,
        "--output",
        $packageAuditOutput
    )
}
finally {
    Pop-Location
}

Require-Path $manifestPath "Static actor batch manifest"
Require-Path $resourceAuditOutput "Static actor resource audit"
Require-Path $archiveOutput "Static actor package archive"
Require-Path $packageAuditOutput "Static actor package audit"

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$resourceAudit = Get-Content -LiteralPath $resourceAuditOutput -Raw | ConvertFrom-Json
$packageAudit = Get-Content -LiteralPath $packageAuditOutput -Raw | ConvertFrom-Json
$archiveItem = Get-Item -LiteralPath $archiveOutput
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    manifest = $manifestPath
    resource_audit = $resourceAuditOutput
    archive = $archiveOutput
    package_audit = $packageAuditOutput
    archive_byte_length = $archiveItem.Length
    resource_prefix = $ResourcePrefix
    limit = $Limit
    no_textures = [bool]$NoTextures
    filter = $manifest.filter
    scanned = $manifest.scanned
    filtered = $manifest.filtered
    considered = $manifest.considered
    converted = $manifest.converted
    skipped = $manifest.skipped
    failed = $manifest.failed
    parse_failed = $manifest.parse_failed
    resource_kind_counts = $packageAudit.resource_kind_counts_expected
    resource_audit_counts = [ordered]@{
        converted_record_count = $resourceAudit.converted_record_count
        resource_count = $resourceAudit.resource_count
        material_display_list_count = $resourceAudit.material_display_list_count
        mesh_display_list_count = $resourceAudit.mesh_display_list_count
        vertex_resource_count = $resourceAudit.vertex_resource_count
        set_texture_image_count = $resourceAudit.set_texture_image_count
        load_texture_block_count = $resourceAudit.load_texture_block_count
        negative_st_count = $resourceAudit.negative_st_count
        min_st = $resourceAudit.min_st
        max_st = $resourceAudit.max_st
        issue_counts = $resourceAudit.issue_counts
    }
    package_audit_counts = [ordered]@{
        archive_entry_count = $packageAudit.archive_entry_count
        has_manifest = $packageAudit.has_manifest
        has_static_batch_manifest = $packageAudit.has_static_batch_manifest
        archived_static_batch_manifest_matches = $packageAudit.archived_static_batch_manifest_matches
        converted_record_count = $packageAudit.converted_record_count
        expected_resource_count = $packageAudit.expected_resource_count
        expected_unique_resource_count = $packageAudit.expected_unique_resource_count
        resource_entry_count = $packageAudit.resource_entry_count
        missing_resource_entry_count = $packageAudit.missing_resource_entry_count
        extra_resource_entry_count = $packageAudit.extra_resource_entry_count
        duplicate_archive_entry_count = $packageAudit.duplicate_archive_entry_count
        duplicate_expected_resource_path_count = $packageAudit.duplicate_expected_resource_path_count
        invalid_resource_xml_count = $packageAudit.invalid_resource_xml_count
        resource_kind_counts_expected = $packageAudit.resource_kind_counts_expected
        resource_kind_counts_archive = $packageAudit.resource_kind_counts_archive
        issue_counts = $packageAudit.issue_counts
    }
}

$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.filter -eq "rigid_export_candidates") "batch did not use rigid export candidate filtering"
    Assert-Condition (($summary.converted + $summary.skipped + $summary.failed) -eq $summary.considered) "record counts do not add up"
    Assert-Condition ($summary.resource_audit_counts.converted_record_count -eq $summary.converted) "resource audit converted count mismatch"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_resource_file -eq 0) "resource audit found missing generated resources"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.invalid_resource_xml -eq 0) "resource audit found invalid generated XML"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.load_texture_block_used -eq 0) "resource audit found LoadTextureBlock usage"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_texture_reference -eq 0) "resource audit found missing texture references"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_mesh_material_call -eq 0) "resource audit found mesh display lists without material calls"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_mesh_material_reference -eq 0) "resource audit found missing mesh material references"
    Assert-Condition ($summary.package_audit_counts.has_manifest) "expected archive manifest.json"
    Assert-Condition ($summary.package_audit_counts.has_static_batch_manifest) "expected archived static batch manifest"
    Assert-Condition ($summary.package_audit_counts.archived_static_batch_manifest_matches) "expected archived static batch manifest to match source"
    Assert-Condition ($summary.package_audit_counts.expected_resource_count -eq $summary.package_audit_counts.resource_entry_count) "expected every generated resource in archive"
    Assert-Condition ($summary.package_audit_counts.expected_unique_resource_count -eq $summary.package_audit_counts.expected_resource_count) "expected unique resource paths"
    Assert-Condition ($summary.package_audit_counts.missing_resource_entry_count -eq 0) "expected no missing archive resource entries"
    Assert-Condition ($summary.package_audit_counts.extra_resource_entry_count -eq 0) "expected no extra archive resource entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_archive_entry_count -eq 0) "expected no duplicate archive entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_expected_resource_path_count -eq 0) "expected no duplicate expected resource paths"
    Assert-Condition ($summary.package_audit_counts.invalid_resource_xml_count -eq 0) "expected no invalid archived resource XML"
    Assert-Condition ($summary.package_audit_counts.issue_counts.total -eq 0) "expected zero package audit issues"

    if ($Limit -eq 0 -and -not $NoTextures) {
        Assert-Condition ($summary.scanned -eq 1310) "expected 1310 parsed actor CMB models"
        Assert-Condition ($summary.filtered -eq 0) "expected no pre-filtered actor CMB records"
        Assert-Condition ($summary.considered -eq 1310) "expected 1310 considered actor CMB models"
        Assert-Condition ($summary.converted -eq 1108) "expected 1108 static/rigid actor conversions"
        Assert-Condition ($summary.skipped -eq 202) "expected 202 skinned/non-rigid actor CMB skips"
        Assert-Condition ($summary.failed -eq 0) "expected 0 actor static conversion failures"
        Assert-Condition ($summary.parse_failed -eq 0) "expected 0 actor static parse failures"
        Assert-Condition ($summary.resource_audit_counts.resource_count -eq 23725) "expected 23725 generated resources"
        Assert-Condition ((Get-JsonValue $summary.resource_kind_counts "DisplayList") -eq 13952) "expected 13952 display-list resources"
        Assert-Condition ((Get-JsonValue $summary.resource_kind_counts "Texture") -eq 2422) "expected 2422 texture resources"
        Assert-Condition ((Get-JsonValue $summary.resource_kind_counts "Vertex") -eq 7351) "expected 7351 vertex resources"
        Assert-Condition ($summary.resource_audit_counts.material_display_list_count -eq 2676) "expected 2676 material display lists"
        Assert-Condition ($summary.resource_audit_counts.mesh_display_list_count -eq 2817) "expected 2817 mesh display lists"
        Assert-Condition ($summary.resource_audit_counts.vertex_resource_count -eq 7351) "expected 7351 vertex resources in audit"
        Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 3153) "expected 3153 texture image references"
        Assert-Condition ($summary.resource_audit_counts.load_texture_block_count -eq 0) "expected 0 LoadTextureBlock calls"
        Assert-Condition ($summary.resource_audit_counts.negative_st_count -eq 600) "expected 600 negative generated S/T values"
        Assert-Condition ($summary.resource_audit_counts.min_st -eq -4096) "expected min generated S/T -4096"
        Assert-Condition ($summary.resource_audit_counts.max_st -eq 32767) "expected max generated S/T 32767"
        Assert-Condition ($summary.resource_audit_counts.issue_counts.negative_vertex_st -eq 110) "expected 110 tracked negative-S/T warnings"
        Assert-Condition ($summary.resource_audit_counts.issue_counts.total -eq 110) "expected only the 110 known negative-S/T warnings"
        Assert-Condition ($summary.package_audit_counts.archive_entry_count -eq 23727) "expected 23725 resources plus 2 manifests"
        Assert-Condition ($summary.package_audit_counts.converted_record_count -eq 1108) "expected 1108 packaged converted records"
        Assert-Condition ($summary.package_audit_counts.expected_resource_count -eq 23725) "expected 23725 packaged resources"
        Assert-Condition ((Get-JsonValue $summary.package_audit_counts.resource_kind_counts_archive "DisplayList") -eq 13952) "expected 13952 archived display-list resources"
        Assert-Condition ((Get-JsonValue $summary.package_audit_counts.resource_kind_counts_archive "Texture") -eq 2422) "expected 2422 archived texture resources"
        Assert-Condition ((Get-JsonValue $summary.package_audit_counts.resource_kind_counts_archive "Vertex") -eq 7351) "expected 7351 archived vertex resources"
    }
}

Write-Host "OOT3D static actor package archive: $archiveOutput"
Write-Host "OOT3D static actor package audit: $packageAuditOutput"
Write-Host "OOT3D static actor package summary: $summaryOutput"
