param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$RuntimeManifest = "",
    [string]$KankyoExportManifest = "",
    [string]$OutputRoot = "",
    [int]$SampleLimit = 100,
    [switch]$RuntimeEnabled,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($RuntimeManifest)) {
    $runtimeManifestName = if ($RuntimeEnabled) {
        "oot3d_kokiri_runtime_manifest_enabled.json"
    } else {
        "oot3d_kokiri_runtime_manifest.json"
    }
    $RuntimeManifest = Join-Path $WorkRoot "kokiri_runtime_route\$runtimeManifestName"
}
if ([string]::IsNullOrWhiteSpace($KankyoExportManifest)) {
    $KankyoExportManifest = Join-Path $WorkRoot "kankyo_environment_export\kankyo_environment_export_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "kokiri_kankyo_binding"
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
        throw "OOT3D Kokiri kankyo binding plan verification failed: $Message"
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
Require-Path $RuntimeManifest "Kokiri runtime manifest"
Require-Path $KankyoExportManifest "kankyo environment export manifest"

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$planOutput = Join-Path $OutputRoot "oot3d_kokiri_kankyo_binding_plan.json"
$summaryOutput = Join-Path $OutputRoot "oot3d_kokiri_kankyo_binding_summary.json"

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-kokiri-kankyo-binding-plan",
        $RuntimeManifest,
        $KankyoExportManifest,
        "--output",
        $planOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $planOutput "Kokiri kankyo binding plan"
$plan = Get-Content -LiteralPath $planOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    plan = $planOutput
    runtime_manifest = (Resolve-Path -LiteralPath $RuntimeManifest).Path
    kankyo_export_manifest = (Resolve-Path -LiteralPath $KankyoExportManifest).Path
    route_id = $plan.route_id
    runtime_enablement_status = $plan.runtime_enablement_status
    skybox_profile_count = $plan.skybox_profile_count
    selected_kankyo_profile_count = $plan.selected_kankyo_profile_count
    n64_filter_only_profile_count = $plan.n64_filter_only_profile_count
    unresolved_selection_count = $plan.unresolved_selection_count
    selected_archives = $plan.selected_archives
    selected_environment_groups = $plan.selected_environment_groups
    ready_environment_record_count = $plan.ready_environment_record_count
    ready_environment_resource_count = $plan.ready_environment_resource_count
    ready_top_display_list_count = $plan.ready_top_display_list_count
    ready_environment_archive_counts = $plan.ready_environment_archive_counts
    ready_environment_group_counts = $plan.ready_environment_group_counts
    profile_binding_status_counts = $plan.profile_binding_status_counts
    binding_issue_counts = $plan.binding_issue_counts
    ready_binding_issue_total = $plan.ready_binding_issue_total
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($plan.format -eq "oot3d_kokiri_kankyo_binding_plan_v1") "unexpected plan format"
    Assert-Condition ($summary.route_id -eq "link_house_to_kokiri_forest") "unexpected route id"
    Assert-Condition ($summary.skybox_profile_count -eq 4) "expected 4 Kokiri skybox profiles"
    Assert-Condition ($summary.selected_kankyo_profile_count -eq 2) "expected 2 selected OOT3D kankyo profiles"
    Assert-Condition ($summary.n64_filter_only_profile_count -eq 2) "expected 2 N64 filter-only profiles"
    Assert-Condition ($summary.unresolved_selection_count -eq 0) "expected no unresolved kankyo profile selections"
    Assert-Condition (@($summary.selected_archives).Count -eq 1) "expected one selected kankyo archive"
    Assert-Condition (@($summary.selected_archives)[0] -eq "BlueSky.zar") "expected BlueSky.zar as selected archive"
    Assert-Condition ($summary.ready_environment_record_count -eq 38) "expected 38 selected BlueSky records"
    Assert-Condition ($summary.ready_environment_resource_count -eq 545) "expected 545 selected BlueSky resources"
    Assert-Condition ($summary.ready_top_display_list_count -eq 38) "expected 38 selected BlueSky top display lists"
    Assert-Condition ((Get-JsonValue $summary.ready_environment_archive_counts "BlueSky.zar") -eq 38) "expected every selected record to come from BlueSky.zar"
    Assert-Condition ((Get-JsonValue $summary.ready_environment_group_counts "tenkyu_sky_dome") -eq 16) "expected 16 selected sky-dome records"
    Assert-Condition ((Get-JsonValue $summary.ready_environment_group_counts "kumo_cloud") -eq 19) "expected 19 selected cloud records"
    Assert-Condition ((Get-JsonValue $summary.ready_environment_group_counts "sun") -eq 2) "expected 2 selected sun records"
    Assert-Condition ((Get-JsonValue $summary.ready_environment_group_counts "star") -eq 1) "expected 1 selected star record"
    Assert-Condition ((Get-JsonValue $summary.profile_binding_status_counts "ready_kankyo_environment_binding") -eq 2) "expected two ready kankyo profile bindings"
    Assert-Condition ((Get-JsonValue $summary.profile_binding_status_counts "n64_filter_only_fallback") -eq 2) "expected two N64 filter-only bindings"
    Assert-Condition ($summary.ready_binding_issue_total -eq 0) "expected no ready binding issues"
}

Write-Host "OOT3D Kokiri kankyo binding plan: $planOutput"
Write-Host "OOT3D Kokiri kankyo binding summary: $summaryOutput"
