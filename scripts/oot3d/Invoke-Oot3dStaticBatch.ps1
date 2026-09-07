param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$InputSubdir = "actor",
    [string]$ResourcePrefix = "objects/oot3d_static_actor_pilot",
    [int]$Limit = 25,
    [switch]$NoTextures,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
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

function Group-ResourceKinds($Records) {
    $resources = @($Records | ForEach-Object { $_.resources } | Where-Object { $_ -ne $null })
    $result = [ordered]@{}
    $resources | Group-Object kind | Sort-Object Name | ForEach-Object {
        $result[$_.Name] = $_.Count
    }
    return $result
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D static batch verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$inputPath = Join-Path $RomFs $InputSubdir
Require-Path $inputPath "OOT3D batch input"

$outputRoot = Join-Path $WorkRoot "static_actor_pilot"
$convertedOutput = Join-Path $outputRoot "converted"
$resourceAuditOutput = Join-Path $outputRoot "static_actor_pilot_resource_audit.json"
$auditOutput = Join-Path $outputRoot "static_actor_pilot_summary.json"
New-Item -ItemType Directory -Force -Path $convertedOutput | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $batchArgs = @(
        "batch-static",
        $inputPath,
        "--output",
        $convertedOutput,
        "--resource-prefix",
        $ResourcePrefix,
        "--limit",
        "$Limit"
    )
    if ($NoTextures) {
        $batchArgs += "--no-textures"
    }
    Invoke-Oot3dTool -Arguments $batchArgs

    $manifestPathForAudit = Join-Path $convertedOutput "batch_manifest.json"
    Invoke-Oot3dTool -Arguments @(
        "audit-static-batch",
        $manifestPathForAudit,
        "--output",
        $resourceAuditOutput
    )
}
finally {
    Pop-Location
}

$manifestPath = Join-Path $convertedOutput "batch_manifest.json"
Require-Path $manifestPath "Static batch manifest"
Require-Path $resourceAuditOutput "Static batch resource audit"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$resourceAudit = Get-Content -LiteralPath $resourceAuditOutput -Raw | ConvertFrom-Json
$records = @($manifest.records)
$converted = @($records | Where-Object status -eq "converted")
$skipped = @($records | Where-Object status -eq "skipped")
$failed = @($records | Where-Object status -eq "failed")
$resources = @($converted | ForEach-Object { $_.resources } | Where-Object { $_ -ne $null })

$summary = [ordered]@{
    input = (Resolve-Path $inputPath).Path
    output = $convertedOutput
    manifest = $manifestPath
    resource_audit = $resourceAuditOutput
    resource_prefix = $ResourcePrefix
    limit = $Limit
    no_textures = [bool]$NoTextures
    considered = $manifest.considered
    converted = $manifest.converted
    skipped = $manifest.skipped
    failed = $manifest.failed
    converted_record_count = $converted.Count
    skipped_record_count = $skipped.Count
    failed_record_count = $failed.Count
    resource_count = $resources.Count
    resource_kind_counts = Group-ResourceKinds -Records $converted
    resource_audit_counts = [ordered]@{
        material_display_list_count = $resourceAudit.material_display_list_count
        mesh_display_list_count = $resourceAudit.mesh_display_list_count
        vertex_resource_count = $resourceAudit.vertex_resource_count
        set_texture_image_count = $resourceAudit.set_texture_image_count
        load_texture_block_count = $resourceAudit.load_texture_block_count
        negative_st_count = $resourceAudit.negative_st_count
        min_st = $resourceAudit.min_st
        max_st = $resourceAudit.max_st
        total_issues = $resourceAudit.issue_counts.total
        missing_resource_file = $resourceAudit.issue_counts.missing_resource_file
        missing_texture_reference = $resourceAudit.issue_counts.missing_texture_reference
        missing_mesh_material_reference = $resourceAudit.issue_counts.missing_mesh_material_reference
    }
    converted_asset_ids = @($converted | ForEach-Object { $_.asset_id })
    skipped_asset_ids = @($skipped | ForEach-Object { $_.asset_id })
}

$summary | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $auditOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.considered -gt 0) "batch considered no models"
    Assert-Condition ($summary.converted -gt 0) "batch converted no static models"
    Assert-Condition ($summary.failed -eq 0) "batch contains failed conversions"
    Assert-Condition ($summary.resource_count -gt 0) "batch generated no resources"
    Assert-Condition ($summary.resource_audit_counts.total_issues -eq 0) "batch resource audit found resource binding issues"
    Assert-Condition ($summary.resource_audit_counts.load_texture_block_count -eq 0) "batch resource audit found LoadTextureBlock usage"
    Assert-Condition ($summary.resource_audit_counts.negative_st_count -eq 0) "batch resource audit found negative vertex S/T values"
    if ($InputSubdir -eq "actor" -and $Limit -eq 25 -and -not $NoTextures) {
        Assert-Condition ($summary.considered -eq 25) "actor pilot expected 25 considered models"
        Assert-Condition ($summary.converted -eq 17) "actor pilot expected 17 converted static models"
        Assert-Condition ($summary.skipped -eq 8) "actor pilot expected 8 skipped non-static models"
        Assert-Condition ($summary.failed -eq 0) "actor pilot expected 0 failed conversions"
        Assert-Condition ($summary.resource_count -eq 168) "actor pilot expected 168 generated resources"
        Assert-Condition ($summary.resource_kind_counts.DisplayList -eq 104) "actor pilot expected 104 display lists"
        Assert-Condition ($summary.resource_kind_counts.Texture -eq 25) "actor pilot expected 25 textures"
        Assert-Condition ($summary.resource_kind_counts.Vertex -eq 39) "actor pilot expected 39 vertex resources"
        Assert-Condition ($summary.resource_audit_counts.material_display_list_count -eq 24) "actor pilot expected 24 material display lists"
        Assert-Condition ($summary.resource_audit_counts.mesh_display_list_count -eq 24) "actor pilot expected 24 mesh display lists"
        Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 29) "actor pilot expected 29 texture image loads"
    }
}

Write-Host "Static batch resources: $convertedOutput"
Write-Host "Static batch manifest: $manifestPath"
Write-Host "Static batch resource audit: $resourceAuditOutput"
Write-Host "Static batch summary: $auditOutput"
