param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$InputSubdir = ".",
    [string]$ResourcePrefix = "objects/oot3d_rigid_multibone_batch",
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

function Add-Count($Counts, [string]$Key) {
    if ($Counts.Contains($Key)) {
        $Counts[$Key] += 1
    } else {
        $Counts[$Key] = 1
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

function Get-FailureClass([string]$Reason) {
    if ([string]::IsNullOrWhiteSpace($Reason)) {
        return "unknown"
    }
    if ($Reason -match "unsupported CMB texture .*format 0x([0-9a-fA-F]+), type 0x([0-9a-fA-F]+)") {
        return "unsupported_texture_format_0x$($Matches[1].ToLower())_type_0x$($Matches[2].ToLower())"
    }
    if ($Reason -match "not a rigid export candidate") {
        return "not_rigid_export_candidate"
    }
    if ($Reason -match "skinning mode") {
        return "unsupported_skinning_mode"
    }
    if ($Reason -match "missing bone") {
        return "missing_bone"
    }
    return "other"
}

function Group-FailureClasses($Records) {
    $result = [ordered]@{}
    $Records | ForEach-Object {
        Add-Count $result (Get-FailureClass $_.reason)
    }
    return $result
}

function Group-TextureFormats($Records) {
    $result = [ordered]@{}
    $Records | ForEach-Object {
        $record = $_
        $summaryProperty = $record.PSObject.Properties["summary"]
        if ($null -ne $summaryProperty -and $null -ne $summaryProperty.Value) {
            $texturesProperty = $summaryProperty.Value.PSObject.Properties["textures"]
            if ($null -ne $texturesProperty -and $null -ne $texturesProperty.Value) {
                @($texturesProperty.Value) | ForEach-Object {
                    Add-Count $result "format=$($_.format);type=$($_.data_type)"
                }
            }
        }
    }
    return $result
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D rigid multi-bone batch verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $RomFs "OOT3D RomFS"

if ([string]::IsNullOrWhiteSpace($InputSubdir) -or $InputSubdir -eq ".") {
    $inputPath = $RomFs
} else {
    $inputPath = Join-Path $RomFs $InputSubdir
}
Require-Path $inputPath "OOT3D rigid multi-bone batch input"

$outputRoot = Join-Path $WorkRoot "rigid_multibone_batch"
$convertedOutput = Join-Path $outputRoot "converted"
$resourceAuditOutput = Join-Path $outputRoot "rigid_multibone_batch_resource_audit.json"
$summaryOutput = Join-Path $outputRoot "rigid_multibone_batch_summary.json"
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
        "--only-rigid-multibone"
    )
    if ($Limit -gt 0) {
        $batchArgs += @("--limit", "$Limit")
    }
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
Require-Path $manifestPath "Rigid multi-bone batch manifest"
Require-Path $resourceAuditOutput "Rigid multi-bone batch resource audit"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$resourceAudit = Get-Content -LiteralPath $resourceAuditOutput -Raw | ConvertFrom-Json
$records = @($manifest.records)
$converted = @($records | Where-Object status -eq "converted")
$skipped = @($records | Where-Object status -eq "skipped")
$failed = @($records | Where-Object status -eq "failed")
$parseFailed = @($records | Where-Object status -eq "parse_failed")
$resources = @($converted | ForEach-Object { $_.resources } | Where-Object { $_ -ne $null })

$summary = [ordered]@{
    input = (Resolve-Path $inputPath).Path
    output = $convertedOutput
    manifest = $manifestPath
    resource_audit = $resourceAuditOutput
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
    converted_record_count = $converted.Count
    skipped_record_count = $skipped.Count
    failed_record_count = $failed.Count
    parse_failed_record_count = $parseFailed.Count
    resource_count = $resources.Count
    resource_kind_counts = Group-ResourceKinds -Records $converted
    considered_texture_format_counts = Group-TextureFormats -Records $records
    failed_texture_format_counts = Group-TextureFormats -Records $failed
    failure_class_counts = Group-FailureClasses -Records $failed
    failed_samples = @(
        $failed | Select-Object -First 20 | ForEach-Object {
            [ordered]@{
                asset_id = $_.asset_id
                source = $_.source
                failure_class = Get-FailureClass $_.reason
                reason = $_.reason
            }
        }
    )
    parse_failed_samples = @(
        $parseFailed | Select-Object -First 20 | ForEach-Object {
            [ordered]@{
                asset_id = $_.asset_id
                source = $_.source
                reason = $_.reason
            }
        }
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
        missing_resource_file = $resourceAudit.issue_counts.missing_resource_file
        invalid_resource_xml = $resourceAudit.issue_counts.invalid_resource_xml
        missing_texture_reference = $resourceAudit.issue_counts.missing_texture_reference
        missing_mesh_material_call = $resourceAudit.issue_counts.missing_mesh_material_call
        missing_mesh_material_reference = $resourceAudit.issue_counts.missing_mesh_material_reference
    }
}

$summary | ConvertTo-Json -Depth 10 | Out-File -LiteralPath $summaryOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.filter -eq "rigid_multibone") "batch did not use the rigid multi-bone filter"
    Assert-Condition ($summary.considered -gt 0) "batch considered no rigid multi-bone models"
    Assert-Condition (($summary.converted + $summary.skipped + $summary.failed) -eq $summary.considered) "record counts do not add up"
    Assert-Condition ($summary.parse_failed -eq $summary.parse_failed_record_count) "parse-failed count mismatch"
    Assert-Condition ($summary.skipped -eq 0) "rigid multi-bone filter should not leave skipped non-candidate records"
    Assert-Condition ($summary.converted -gt 0) "batch converted no rigid multi-bone models"
    Assert-Condition ($summary.resource_count -gt 0) "batch generated no resources"
    Assert-Condition ($summary.resource_audit_counts.missing_resource_file -eq 0) "batch resource audit found missing resource files"
    Assert-Condition ($summary.resource_audit_counts.invalid_resource_xml -eq 0) "batch resource audit found invalid XML"
    Assert-Condition ($summary.resource_audit_counts.missing_texture_reference -eq 0) "batch resource audit found missing texture references"
    Assert-Condition ($summary.resource_audit_counts.missing_mesh_material_call -eq 0) "batch resource audit found mesh display lists without material calls"
    Assert-Condition ($summary.resource_audit_counts.missing_mesh_material_reference -eq 0) "batch resource audit found missing mesh material references"
    Assert-Condition ($summary.resource_audit_counts.load_texture_block_count -eq 0) "batch resource audit found LoadTextureBlock usage"
    if ($InputSubdir -eq "." -and $Limit -eq 0) {
        Assert-Condition ($summary.considered -eq 288) "full RomFS expected 288 rigid multi-bone candidates"
        Assert-Condition ($summary.parse_failed -eq 0) "full RomFS expected 0 known parse-failed CMBs"
        if (-not $NoTextures) {
            Assert-Condition ($summary.filtered -eq 1710) "full RomFS expected 1710 non-rigid or one-bone parsed models filtered out"
            Assert-Condition ($summary.converted -eq 288) "full RomFS expected 288 converted rigid multi-bone models"
            Assert-Condition ($summary.failed -eq 0) "full RomFS expected 0 texture-format conversion failures"
            Assert-Condition ($summary.resource_count -eq 67064) "full RomFS expected 67064 generated resources"
            Assert-Condition ($summary.resource_kind_counts.DisplayList -eq 35839) "full RomFS expected 35839 display lists"
            Assert-Condition ($summary.resource_kind_counts.Texture -eq 3649) "full RomFS expected 3649 texture resources"
            Assert-Condition ($summary.resource_kind_counts.Vertex -eq 27576) "full RomFS expected 27576 vertex resources"
            Assert-Condition ($summary.resource_audit_counts.material_display_list_count -eq 3761) "full RomFS expected 3761 material display lists"
            Assert-Condition ($summary.resource_audit_counts.mesh_display_list_count -eq 4214) "full RomFS expected 4214 mesh display lists"
            Assert-Condition ($summary.resource_audit_counts.vertex_resource_count -eq 27576) "full RomFS expected 27576 vertex resources in audit"
            Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 4379) "full RomFS expected 4379 texture image loads"
            Assert-Condition ($summary.resource_audit_counts.total_issues -eq 37) "full RomFS expected 37 tracked UV audit issues"
            Assert-Condition ($summary.resource_audit_counts.negative_st_count -eq 177) "full RomFS expected 177 negative vertex S/T values"
            Assert-Condition ($summary.resource_audit_counts.min_st -eq -992) "full RomFS expected minimum S/T of -992"
            Assert-Condition ($summary.resource_audit_counts.max_st -eq 32767) "full RomFS expected maximum S/T of 32767"
        }
    }
}

Write-Host "Rigid multi-bone batch resources: $convertedOutput"
Write-Host "Rigid multi-bone batch manifest: $manifestPath"
Write-Host "Rigid multi-bone batch resource audit: $resourceAuditOutput"
Write-Host "Rigid multi-bone batch summary: $summaryOutput"
