param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$InputSubdir = "actor\zelda_box.zar",
    [string]$ResourcePrefix = "objects/oot3d_rigid_multibone_pilot",
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
        throw "OOT3D rigid multi-bone verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

$inputPath = Join-Path $RomFs $InputSubdir
Require-Path $inputPath "OOT3D rigid multi-bone input"

$outputRoot = Join-Path $WorkRoot "rigid_multibone_pilot"
$convertedOutput = Join-Path $outputRoot "converted"
$resourceAuditOutput = Join-Path $outputRoot "rigid_multibone_resource_audit.json"
$summaryOutput = Join-Path $outputRoot "rigid_multibone_summary.json"
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
        $ResourcePrefix
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
Require-Path $manifestPath "Rigid multi-bone manifest"
Require-Path $resourceAuditOutput "Rigid multi-bone resource audit"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$resourceAudit = Get-Content -LiteralPath $resourceAuditOutput -Raw | ConvertFrom-Json
$records = @($manifest.records)
$converted = @($records | Where-Object status -eq "converted")
$skipped = @($records | Where-Object status -eq "skipped")
$failed = @($records | Where-Object status -eq "failed")
$resources = @($converted | ForEach-Object { $_.resources } | Where-Object { $_ -ne $null })
$convertedSummaries = @($converted | ForEach-Object { $_.summary })

$summary = [ordered]@{
    input = (Resolve-Path $inputPath).Path
    output = $convertedOutput
    manifest = $manifestPath
    resource_audit = $resourceAuditOutput
    resource_prefix = $ResourcePrefix
    no_textures = [bool]$NoTextures
    considered = $manifest.considered
    converted = $manifest.converted
    skipped = $manifest.skipped
    failed = $manifest.failed
    resource_count = $resources.Count
    resource_kind_counts = Group-ResourceKinds -Records $converted
    converted_asset_ids = @($converted | ForEach-Object { $_.asset_id })
    converted_bone_counts = @($convertedSummaries | ForEach-Object { $_.bone_count })
    converted_max_depths = @($convertedSummaries | ForEach-Object { $_.skeleton.max_depth })
    converted_nonzero_rotation_counts = @(
        $convertedSummaries | ForEach-Object { $_.skeleton.nonzero_rotation_count }
    )
    converted_nonzero_translation_counts = @(
        $convertedSummaries | ForEach-Object { $_.skeleton.nonzero_translation_count }
    )
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
    }
}

$summary | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $summaryOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.considered -eq 2) "expected 2 CMBs from zelda_box.zar"
    Assert-Condition ($summary.converted -eq 2) "expected 2 rigid multi-bone conversions"
    Assert-Condition ($summary.skipped -eq 0) "expected no skipped rigid multi-bone models"
    Assert-Condition ($summary.failed -eq 0) "expected no rigid multi-bone conversion failures"
    if ($NoTextures) {
        Assert-Condition ($summary.resource_count -eq 69) "expected 69 generated geometry resources"
        Assert-Condition ($summary.resource_kind_counts.DisplayList -eq 42) "expected 42 display lists"
        Assert-Condition ($summary.resource_kind_counts.Vertex -eq 27) "expected 27 vertex resources"
        Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 0) "expected no texture loads in geometry-only pilot"
    } else {
        Assert-Condition ($summary.resource_count -eq 78) "expected 78 generated resources"
        Assert-Condition ($summary.resource_kind_counts.DisplayList -eq 42) "expected 42 display lists"
        Assert-Condition ($summary.resource_kind_counts.Texture -eq 9) "expected 9 texture resources"
        Assert-Condition ($summary.resource_kind_counts.Vertex -eq 27) "expected 27 vertex resources"
        Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 8) "expected 8 texture image loads"
    }
    Assert-Condition ($summary.resource_audit_counts.material_display_list_count -eq 4) "expected 4 material display lists"
    Assert-Condition ($summary.resource_audit_counts.mesh_display_list_count -eq 9) "expected 9 mesh display lists"
    Assert-Condition ($summary.resource_audit_counts.vertex_resource_count -eq 27) "expected 27 vertex resources in audit"
    Assert-Condition ($summary.resource_audit_counts.load_texture_block_count -eq 0) "expected no LoadTextureBlock usage"
    Assert-Condition ($summary.resource_audit_counts.negative_st_count -eq 0) "expected no negative vertex S/T values"
    Assert-Condition ($summary.resource_audit_counts.total_issues -eq 0) "expected no resource audit issues"
}

Write-Host "Rigid multi-bone resources: $convertedOutput"
Write-Host "Rigid multi-bone manifest: $manifestPath"
Write-Host "Rigid multi-bone resource audit: $resourceAuditOutput"
Write-Host "Rigid multi-bone summary: $summaryOutput"
