param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$ResourcePrefix = "objects/oot3d_actor_static",
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

function Group-Reasons($Records) {
    $result = [ordered]@{}
    $Records | ForEach-Object {
        Add-Count $result $_.reason
    }
    return $result
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D static actor export verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "static_actor_export"
$convertedOutput = Join-Path $outputRoot "converted"
$manifestPath = Join-Path $convertedOutput "batch_manifest.json"
$resourceAuditOutput = Join-Path $outputRoot "static_actor_export_resource_audit.json"
$summaryOutput = Join-Path $outputRoot "static_actor_export_summary.json"
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
}
finally {
    Pop-Location
}

Require-Path $manifestPath "Static actor batch manifest"
Require-Path $resourceAuditOutput "Static actor resource audit"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$resourceAudit = Get-Content -LiteralPath $resourceAuditOutput -Raw | ConvertFrom-Json
$records = @($manifest.records)
$converted = @($records | Where-Object status -eq "converted")
$skipped = @($records | Where-Object status -eq "skipped")
$failed = @($records | Where-Object status -eq "failed")
$parseFailed = @($records | Where-Object status -eq "parse_failed")
$resources = @($converted | ForEach-Object { $_.resources } | Where-Object { $_ -ne $null })

$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
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
    skip_reason_counts = Group-Reasons -Records $skipped
    failed_reason_counts = Group-Reasons -Records $failed
    failed_samples = @(
        $failed | Select-Object -First 20 | ForEach-Object {
            [ordered]@{
                asset_id = $_.asset_id
                source = $_.source
                reason = $_.reason
            }
        }
    )
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
        missing_resource_file = $resourceAudit.issue_counts.missing_resource_file
        invalid_resource_xml = $resourceAudit.issue_counts.invalid_resource_xml
        load_texture_block_used = $resourceAudit.issue_counts.load_texture_block_used
        missing_texture_reference = $resourceAudit.issue_counts.missing_texture_reference
        missing_mesh_material_call = $resourceAudit.issue_counts.missing_mesh_material_call
        missing_mesh_material_reference = $resourceAudit.issue_counts.missing_mesh_material_reference
        negative_vertex_st = $resourceAudit.issue_counts.negative_vertex_st
        total_issues = $resourceAudit.issue_counts.total
    }
}

$summary | ConvertTo-Json -Depth 10 | Out-File -LiteralPath $summaryOutput -Encoding utf8

if ($Verify) {
    Assert-Condition ($summary.filter -eq "rigid_export_candidates") "batch did not use rigid export candidate filtering"
    Assert-Condition (($summary.converted + $summary.skipped + $summary.failed) -eq $summary.considered) "record counts do not add up"
    Assert-Condition ($summary.converted -eq $summary.converted_record_count) "converted record count mismatch"
    Assert-Condition ($summary.skipped -eq $summary.skipped_record_count) "skipped record count mismatch"
    Assert-Condition ($summary.failed -eq $summary.failed_record_count) "failed record count mismatch"
    Assert-Condition ($summary.parse_failed -eq $summary.parse_failed_record_count) "parse-failed record count mismatch"
    Assert-Condition ($summary.resource_audit_counts.converted_record_count -eq $summary.converted) "resource audit converted count mismatch"
    Assert-Condition ($summary.resource_audit_counts.resource_count -eq $summary.resource_count) "resource audit resource count mismatch"
    Assert-Condition ($summary.resource_audit_counts.missing_resource_file -eq 0) "resource audit found missing generated resources"
    Assert-Condition ($summary.resource_audit_counts.invalid_resource_xml -eq 0) "resource audit found invalid generated XML"
    Assert-Condition ($summary.resource_audit_counts.load_texture_block_used -eq 0) "resource audit found LoadTextureBlock usage"
    Assert-Condition ($summary.resource_audit_counts.missing_texture_reference -eq 0) "resource audit found missing texture references"
    Assert-Condition ($summary.resource_audit_counts.missing_mesh_material_call -eq 0) "resource audit found mesh display lists without material calls"
    Assert-Condition ($summary.resource_audit_counts.missing_mesh_material_reference -eq 0) "resource audit found missing mesh material references"

    if ($Limit -eq 0 -and -not $NoTextures) {
        Assert-Condition ($summary.scanned -eq 1310) "expected 1310 parsed actor CMB models"
        Assert-Condition ($summary.filtered -eq 0) "expected no pre-filtered actor CMB records"
        Assert-Condition ($summary.considered -eq 1310) "expected 1310 considered actor CMB models"
        Assert-Condition ($summary.converted -eq 1108) "expected 1108 static/rigid actor conversions"
        Assert-Condition ($summary.skipped -eq 202) "expected 202 skinned/non-rigid actor CMB skips"
        Assert-Condition ($summary.skip_reason_counts.not_rigid_export_candidate -eq 202) "expected all skips to be non-rigid export candidates"
        Assert-Condition ($summary.failed -eq 0) "expected 0 actor static conversion failures"
        Assert-Condition ($summary.parse_failed -eq 0) "expected 0 actor static parse failures"
        Assert-Condition ($summary.resource_count -eq 23725) "expected 23725 generated static actor resources"
        Assert-Condition ($summary.resource_kind_counts.DisplayList -eq 13952) "expected 13952 display-list resources"
        Assert-Condition ($summary.resource_kind_counts.Texture -eq 2422) "expected 2422 texture resources"
        Assert-Condition ($summary.resource_kind_counts.Vertex -eq 7351) "expected 7351 vertex resources"
        Assert-Condition ($summary.resource_audit_counts.material_display_list_count -eq 2676) "expected 2676 material display lists"
        Assert-Condition ($summary.resource_audit_counts.mesh_display_list_count -eq 2817) "expected 2817 mesh display lists"
        Assert-Condition ($summary.resource_audit_counts.vertex_resource_count -eq 7351) "expected 7351 vertex resources in audit"
        Assert-Condition ($summary.resource_audit_counts.set_texture_image_count -eq 3153) "expected 3153 texture image references"
        Assert-Condition ($summary.resource_audit_counts.negative_st_count -eq 600) "expected 600 negative generated S/T values"
        Assert-Condition ($summary.resource_audit_counts.min_st -eq -4096) "expected min generated S/T -4096"
        Assert-Condition ($summary.resource_audit_counts.max_st -eq 32767) "expected max generated S/T 32767"
        Assert-Condition ($summary.resource_audit_counts.negative_vertex_st -eq 110) "expected 110 tracked negative-S/T vertex-resource warnings"
        Assert-Condition ($summary.resource_audit_counts.total_issues -eq 110) "expected only the 110 known negative-S/T warnings"
    }
}

Write-Host "OOT3D static actor resources: $convertedOutput"
Write-Host "OOT3D static actor manifest: $manifestPath"
Write-Host "OOT3D static actor resource audit: $resourceAuditOutput"
Write-Host "OOT3D static actor summary: $summaryOutput"
