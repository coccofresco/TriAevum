param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$KankyoRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ResourcePrefix = "environments/oot3d/kankyo",
    [string]$ArchiveName = "oot3d_kankyo_environment_candidates.o2r",
    [string]$Name = "OOT3D Kankyo Environment Candidates",
    [string]$Author = "local",
    [string]$Version = "0.1.0",
    [switch]$NoTextures,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($KankyoRoot)) {
    $KankyoRoot = Join-Path $RomFs "kankyo"
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
        throw "OOT3D kankyo environment package verification failed: $Message"
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
Require-Path $KankyoRoot "OOT3D kankyo directory"

$outputRoot = Join-Path $WorkRoot "kankyo_environment_export"
$manifestOutput = Join-Path $outputRoot "kankyo_environment_export_manifest.json"
$resourceAuditOutput = Join-Path $outputRoot "kankyo_environment_export_resource_audit.json"
$archiveOutput = Join-Path $outputRoot $ArchiveName
$packageAuditOutput = Join-Path $outputRoot "kankyo_environment_package_audit.json"
$summaryOutput = Join-Path $outputRoot "kankyo_environment_package_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $exportArgs = @(
        "export-kankyo-environment-assets",
        $KankyoRoot,
        "--output",
        $outputRoot,
        "--resource-prefix",
        $ResourcePrefix
    )
    if ($NoTextures) {
        $exportArgs += "--no-textures"
    }
    Invoke-Oot3dTool -Arguments $exportArgs

    Invoke-Oot3dTool -Arguments @(
        "pack-kankyo-environment-export",
        $manifestOutput,
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
        "audit-kankyo-environment-export-package",
        $manifestOutput,
        $archiveOutput,
        "--output",
        $packageAuditOutput
    )
}
finally {
    Pop-Location
}

Require-Path $manifestOutput "Kankyo environment export manifest"
Require-Path $resourceAuditOutput "Kankyo environment resource audit"
Require-Path $archiveOutput "Kankyo environment package archive"
Require-Path $packageAuditOutput "Kankyo environment package audit"

$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$resourceAudit = Get-Content -LiteralPath $resourceAuditOutput -Raw | ConvertFrom-Json
$packageAudit = Get-Content -LiteralPath $packageAuditOutput -Raw | ConvertFrom-Json
$archiveItem = Get-Item -LiteralPath $archiveOutput
$summary = [ordered]@{
    kankyo_root = (Resolve-Path $KankyoRoot).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    resource_audit = $resourceAuditOutput
    archive = $archiveOutput
    package_audit = $packageAuditOutput
    archive_byte_length = $archiveItem.Length
    resource_prefix = $ResourcePrefix
    include_textures = $manifest.include_textures
    source_audit_summary = $manifest.source_audit_summary
    archive_count = $manifest.archive_count
    considered = $manifest.considered
    converted = $manifest.converted
    skipped = $manifest.skipped
    failed = $manifest.failed
    parse_failed = $manifest.parse_failed
    status_counts = $manifest.status_counts
    environment_model_group_counts = $manifest.environment_model_group_counts
    resource_kind_counts = $manifest.resource_kind_counts
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
        has_kankyo_environment_export_manifest = $packageAudit.has_kankyo_environment_export_manifest
        archived_export_manifest_matches = $packageAudit.archived_export_manifest_matches
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
    Assert-Condition ($summary.include_textures -eq (-not $NoTextures)) "manifest texture flag does not match command"
    Assert-Condition ($summary.archive_count -eq 6) "expected 6 kankyo ZAR archives"
    Assert-Condition ($summary.source_audit_summary.archive_file_count_total -eq 107) "expected 107 embedded kankyo files"
    Assert-Condition ($summary.source_audit_summary.embedded_type_counts.cmb -eq 76) "expected 76 embedded CMB files"
    Assert-Condition ($summary.source_audit_summary.embedded_type_counts.cmab -eq 11) "expected 11 embedded CMAB files"
    Assert-Condition ($summary.source_audit_summary.embedded_type_counts.ctxb -eq 18) "expected 18 embedded CTXB files"
    Assert-Condition ($summary.source_audit_summary.embedded_type_counts.tbd -eq 2) "expected 2 embedded TBD files"
    Assert-Condition ($summary.considered -eq 76) "expected 76 considered kankyo CMB models"
    Assert-Condition ($summary.converted -eq 76) "expected every kankyo CMB model to convert"
    Assert-Condition ($summary.skipped -eq 0) "expected 0 skipped kankyo CMB models"
    Assert-Condition ($summary.failed -eq 0) "expected 0 failed kankyo CMB exports"
    Assert-Condition ($summary.parse_failed -eq 0) "expected 0 kankyo parse failures"
    Assert-Condition ($summary.status_counts.converted -eq 76) "expected every kankyo record to have converted status"
    Assert-Condition ($summary.environment_model_group_counts.kumo_cloud -eq 36) "expected 36 cloud exports"
    Assert-Condition ($summary.environment_model_group_counts.tenkyu_sky_dome -eq 32) "expected 32 sky-dome exports"
    Assert-Condition ($summary.environment_model_group_counts.sun -eq 6) "expected 6 sun exports"
    Assert-Condition ($summary.environment_model_group_counts.star -eq 2) "expected 2 star exports"
    Assert-Condition ((Get-JsonValue $summary.resource_kind_counts "DisplayList") -eq 616) "expected 616 generated DisplayList resources"
    Assert-Condition ((Get-JsonValue $summary.resource_kind_counts "Vertex") -eq 388) "expected 388 generated Vertex resources"
    Assert-Condition ((Get-JsonValue $summary.resource_kind_counts "Texture") -eq 63) "expected 63 generated Texture resources"
    Assert-Condition ($summary.resource_audit_counts.converted_record_count -eq 76) "expected 76 audited converted records"
    Assert-Condition ($summary.resource_audit_counts.resource_count -eq 1067) "expected 1067 generated resources"
    Assert-Condition ($summary.resource_audit_counts.material_display_list_count -eq 76) "expected 76 material display lists"
    Assert-Condition ($summary.resource_audit_counts.mesh_display_list_count -eq 76) "expected 76 mesh display lists"
    Assert-Condition ($summary.resource_audit_counts.vertex_resource_count -eq 388) "expected 388 vertex resources"
    Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 64) "expected 64 texture image references"
    Assert-Condition ($summary.resource_audit_counts.load_texture_block_count -eq 0) "expected 0 LoadTextureBlock calls"
    Assert-Condition ($summary.resource_audit_counts.negative_st_count -eq 996) "expected 996 negative generated S/T values in clamped star assets"
    Assert-Condition ($summary.resource_audit_counts.min_st -eq -624) "expected min generated S/T -624"
    Assert-Condition ($summary.resource_audit_counts.max_st -eq 32767) "expected max generated S/T 32767"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_resource_file -eq 0) "expected 0 missing generated resources"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.invalid_resource_xml -eq 0) "expected 0 invalid generated XML resources"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.load_texture_block_used -eq 0) "expected 0 LoadTextureBlock audit issues"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_texture_reference -eq 0) "expected 0 missing texture references"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_mesh_material_call -eq 0) "expected 0 missing mesh material calls"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.missing_mesh_material_reference -eq 0) "expected 0 missing mesh material references"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.negative_vertex_st -eq 44) "expected 44 clamped-star vertex resources with negative S/T values"
    Assert-Condition ($summary.resource_audit_counts.issue_counts.total -eq 44) "expected only the 44 known clamped-star negative-S/T audit warnings"
    Assert-Condition ($summary.archive_byte_length -eq 1048654) "expected packaged archive byte length 1048654"
    Assert-Condition ($summary.package_audit_counts.archive_entry_count -eq 1069) "expected 1067 resources plus 2 manifests"
    Assert-Condition ($summary.package_audit_counts.has_manifest) "expected archive manifest.json"
    Assert-Condition ($summary.package_audit_counts.has_kankyo_environment_export_manifest) "expected archived kankyo export manifest"
    Assert-Condition ($summary.package_audit_counts.archived_export_manifest_matches) "expected archived export manifest to match source"
    Assert-Condition ($summary.package_audit_counts.converted_record_count -eq 76) "expected 76 packaged converted records"
    Assert-Condition ($summary.package_audit_counts.expected_resource_count -eq 1067) "expected 1067 packaged resources"
    Assert-Condition ($summary.package_audit_counts.expected_unique_resource_count -eq 1067) "expected unique resource paths"
    Assert-Condition ($summary.package_audit_counts.resource_entry_count -eq 1067) "expected every generated resource in archive"
    Assert-Condition ($summary.package_audit_counts.missing_resource_entry_count -eq 0) "expected no missing archive resource entries"
    Assert-Condition ($summary.package_audit_counts.extra_resource_entry_count -eq 0) "expected no extra archive resource entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_archive_entry_count -eq 0) "expected no duplicate archive entries"
    Assert-Condition ($summary.package_audit_counts.duplicate_expected_resource_path_count -eq 0) "expected no duplicate expected resource paths"
    Assert-Condition ($summary.package_audit_counts.invalid_resource_xml_count -eq 0) "expected no invalid archived resource XML"
    Assert-Condition ((Get-JsonValue $summary.package_audit_counts.resource_kind_counts_archive "DisplayList") -eq 616) "expected 616 archived DisplayList resources"
    Assert-Condition ((Get-JsonValue $summary.package_audit_counts.resource_kind_counts_archive "Vertex") -eq 388) "expected 388 archived Vertex resources"
    Assert-Condition ((Get-JsonValue $summary.package_audit_counts.resource_kind_counts_archive "Texture") -eq 63) "expected 63 archived Texture resources"
    Assert-Condition ($summary.package_audit_counts.issue_counts.total -eq 0) "expected zero package audit issues"
}

Write-Host "OOT3D kankyo environment package archive: $archiveOutput"
Write-Host "OOT3D kankyo environment package audit: $packageAuditOutput"
Write-Host "OOT3D kankyo environment package summary: $summaryOutput"
