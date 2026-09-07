param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$KankyoRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ResourcePrefix = "environments/oot3d/kankyo",
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
        throw "OOT3D kankyo environment export verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $KankyoRoot "OOT3D kankyo directory"

$outputRoot = Join-Path $WorkRoot "kankyo_environment_export"
$manifestOutput = Join-Path $outputRoot "kankyo_environment_export_manifest.json"
$summaryOutput = Join-Path $outputRoot "kankyo_environment_export_summary.json"
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
}
finally {
    Pop-Location
}

$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    kankyo_root = (Resolve-Path $KankyoRoot).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    resource_prefix = $manifest.resource_prefix
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
    resource_audit_summary = $manifest.resource_audit_summary
    record_count = $manifest.records.Count
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
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
    Assert-Condition ($summary.resource_kind_counts.DisplayList -eq 616) "expected 616 generated DisplayList resources"
    Assert-Condition ($summary.resource_kind_counts.Vertex -eq 388) "expected 388 generated Vertex resources"
    Assert-Condition ($summary.resource_kind_counts.Texture -eq 63) "expected 63 generated Texture resources"
    Assert-Condition ($summary.resource_audit_summary.converted_record_count -eq 76) "expected 76 audited converted records"
    Assert-Condition ($summary.resource_audit_summary.resource_count -eq 1067) "expected 1067 generated resources"
    Assert-Condition ($summary.resource_audit_summary.material_display_list_count -eq 76) "expected 76 material display lists"
    Assert-Condition ($summary.resource_audit_summary.mesh_display_list_count -eq 76) "expected 76 mesh display lists"
    Assert-Condition ($summary.resource_audit_summary.vertex_resource_count -eq 388) "expected 388 vertex resources"
    Assert-Condition ($summary.resource_audit_summary.set_texture_image_count -eq 64) "expected 64 texture image references"
    Assert-Condition ($summary.resource_audit_summary.load_texture_block_count -eq 0) "expected 0 LoadTextureBlock calls"
    Assert-Condition ($summary.resource_audit_summary.negative_st_count -eq 996) "expected 996 negative generated S/T values in clamped star assets"
    Assert-Condition ($summary.resource_audit_summary.min_st -eq -624) "expected min generated S/T -624"
    Assert-Condition ($summary.resource_audit_summary.max_st -eq 32767) "expected max generated S/T 32767"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.missing_resource_file -eq 0) "expected 0 missing generated resources"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.invalid_resource_xml -eq 0) "expected 0 invalid generated XML resources"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.load_texture_block_used -eq 0) "expected 0 LoadTextureBlock audit issues"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.missing_texture_reference -eq 0) "expected 0 missing texture references"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.missing_mesh_material_call -eq 0) "expected 0 missing mesh material calls"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.missing_mesh_material_reference -eq 0) "expected 0 missing mesh material references"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.negative_vertex_st -eq 44) "expected 44 clamped-star vertex resources with negative S/T values"
    Assert-Condition ($summary.resource_audit_summary.issue_counts.total -eq 44) "expected only the 44 known clamped-star negative-S/T audit warnings"
    Assert-Condition ($summary.record_count -eq 76) "expected 76 manifest records"
}

Write-Host "OOT3D kankyo environment export manifest: $manifestOutput"
Write-Host "OOT3D kankyo environment export summary: $summaryOutput"
