param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$RuntimeManifest = "",
    [string]$AudioAssetAudit = "",
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
if ([string]::IsNullOrWhiteSpace($AudioAssetAudit)) {
    $AudioAssetAudit = Join-Path $WorkRoot "audio_asset_audit\oot3d_audio_asset_audit.json"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "kokiri_audio_binding"
}
$expectedRuntimeEnablementStatus = if ($RuntimeEnabled) { "oot3d_runtime_enabled" } else { "n64_fallback_only" }

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
        throw "OOT3D Kokiri audio binding plan verification failed: $Message"
    }
}

function Get-JsonValue($Object, [string]$Name) {
    if ($null -eq $Object) {
        return $null
    }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.ContainsKey($Name)) {
            return $Object[$Name]
        }
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
Require-Path $AudioAssetAudit "OOT3D audio asset audit"

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$planOutput = Join-Path $OutputRoot "oot3d_kokiri_audio_binding_plan.json"
$summaryOutput = Join-Path $OutputRoot "oot3d_kokiri_audio_binding_summary.json"

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "export-kokiri-audio-binding-plan",
        $RuntimeManifest,
        $AudioAssetAudit,
        "--output",
        $planOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

Require-Path $planOutput "Kokiri audio binding plan"
$plan = Get-Content -LiteralPath $planOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    plan = $planOutput
    runtime_manifest = (Resolve-Path -LiteralPath $RuntimeManifest).Path
    audio_asset_audit = (Resolve-Path -LiteralPath $AudioAssetAudit).Path
    route_id = $plan.route_id
    runtime_enablement_status = $plan.runtime_enablement_status
    audio_summary_status = $plan.audio_summary_status
    fallback = $plan.fallback
    playback_status = $plan.playback_status
    route_audio_binding_status = $plan.route_audio_binding_status
    sound_setting_count = $plan.sound_setting_count
    mapped_sound_setting_count = $plan.mapped_sound_setting_count
    unresolved_sound_setting_count = $plan.unresolved_sound_setting_count
    route_audio_profile_count = $plan.route_audio_profile_count
    mapped_route_audio_profile_count = $plan.mapped_route_audio_profile_count
    unresolved_route_audio_profile_count = $plan.unresolved_route_audio_profile_count
    audio_support_file_count = $plan.audio_support_file_count
    audio_support_byte_total = $plan.audio_support_byte_total
    audio_support_extension_counts = $plan.audio_support_extension_counts
    audio_support_category_counts = $plan.audio_support_category_counts
    audio_support_issue_total = $plan.audio_support_issue_total
    ready_audio_support_record_count = $plan.ready_audio_support_record_count
    ready_audio_support_byte_total = $plan.ready_audio_support_byte_total
    ready_runtime_record_count = $plan.ready_runtime_record_count
    profile_binding_status_counts = $plan.profile_binding_status_counts
    sound_setting_binding_status_counts = $plan.sound_setting_binding_status_counts
    audio_support_status_counts = $plan.audio_support_status_counts
    binding_issue_counts = $plan.binding_issue_counts
    ready_binding_issue_total = $plan.ready_binding_issue_total
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($plan.format -eq "oot3d_kokiri_audio_binding_plan_v1") "unexpected plan format"
    Assert-Condition ($summary.route_id -eq "link_house_to_kokiri_forest") "unexpected route id"
    Assert-Condition ($summary.runtime_enablement_status -eq $expectedRuntimeEnablementStatus) "unexpected runtime enablement status"
    Assert-Condition ($summary.audio_summary_status -eq "sound_settings_mapped_n64_audio_fallback") "unexpected audio summary status"
    Assert-Condition ($summary.fallback -eq "shipwright_n64_audio") "unexpected audio fallback"
    Assert-Condition ($summary.playback_status -eq "n64_audio_fallback_until_bcsar_bcstm_decoder") "unexpected playback status"
    Assert-Condition ($summary.route_audio_binding_status -eq "mapped_to_shipwright_n64_audio_fallback") "unexpected route audio binding status"
    Assert-Condition ($summary.sound_setting_count -eq 17) "expected 17 route sound-setting records"
    Assert-Condition ($summary.mapped_sound_setting_count -eq 17) "expected 17 mapped route sound-setting records"
    Assert-Condition ($summary.unresolved_sound_setting_count -eq 0) "expected no unresolved sound-setting records"
    Assert-Condition ($summary.route_audio_profile_count -eq 7) "expected 7 route audio profiles"
    Assert-Condition ($summary.mapped_route_audio_profile_count -eq 7) "expected 7 mapped route audio profiles"
    Assert-Condition ($summary.unresolved_route_audio_profile_count -eq 0) "expected no unresolved route audio profiles"
    Assert-Condition ($summary.audio_support_file_count -eq 3) "expected 3 audio support files"
    Assert-Condition ($summary.audio_support_byte_total -eq 12734648) "expected total audio support byte size 12734648"
    Assert-Condition ((Get-JsonValue $summary.audio_support_extension_counts ".bcsar") -eq 2) "expected 2 BCSAR support files"
    Assert-Condition ((Get-JsonValue $summary.audio_support_extension_counts ".bcstm") -eq 1) "expected 1 BCSTM support file"
    Assert-Condition ($summary.audio_support_category_counts.audio_archive -eq 2) "expected 2 audio archive support files"
    Assert-Condition ($summary.audio_support_category_counts.audio_stream -eq 1) "expected 1 audio stream support file"
    Assert-Condition ($summary.audio_support_issue_total -eq 0) "expected no audio support audit issues"
    Assert-Condition ($summary.ready_audio_support_record_count -eq 3) "expected 3 ready audio support records"
    Assert-Condition ($summary.ready_audio_support_byte_total -eq 12734648) "expected every support byte to be ready"
    Assert-Condition ($summary.ready_runtime_record_count -eq 7) "expected 7 ready audio fallback runtime records"
    Assert-Condition ((Get-JsonValue $summary.profile_binding_status_counts "mapped_to_shipwright_n64_audio_fallback") -eq 7) "expected every audio profile to map to fallback"
    Assert-Condition ((Get-JsonValue $summary.sound_setting_binding_status_counts "mapped_to_shipwright_n64_audio_fallback") -eq 17) "expected every sound setting to map to fallback"
    Assert-Condition ((Get-JsonValue $summary.audio_support_status_counts "present_valid_audio_container_header") -eq 3) "expected every support file to have a valid header"
    Assert-Condition ($summary.ready_binding_issue_total -eq 0) "expected no ready audio binding issues"
}

Write-Host "OOT3D Kokiri audio binding plan: $planOutput"
Write-Host "OOT3D Kokiri audio binding summary: $summaryOutput"
