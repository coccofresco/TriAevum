param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [int]$SampleLimit = 100,
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

function Get-JsonValue($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return 0
    }
    return $property.Value
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D skinning queue audit verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "skinning_queue_audit"
$auditOutput = Join-Path $outputRoot "oot3d_actor_skinning_queue_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_actor_skinning_queue_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-actor-skinning-queue",
        $ActorRoot,
        "--output",
        $auditOutput,
        "--sample-limit",
        [string]$SampleLimit
    )
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    audit = $auditOutput
    file_count = $audit.file_count
    archive_count = $audit.archive_count
    loose_cmb_count = $audit.loose_cmb_count
    model_counts = $audit.model_counts
    support_status_counts = $audit.support_status_counts
    skinned_support_status_counts = $audit.skinned_support_status_counts
    skinned_primitive_mode_counts = $audit.skinned_primitive_mode_counts
    primitive_bone_count_counts = $audit.primitive_bone_count_counts
    skinning_mode_bone_count_counts = $audit.skinning_mode_bone_count_counts
    skinned_primitive_profile = $audit.skinned_primitive_profile
    csab_counts = $audit.csab_counts
    csab_target_support_counts = $audit.csab_target_support_counts
    pilot_candidate_count = $audit.pilot_candidates.Count
    top_csab_target_count = $audit.top_csab_targets.Count
    csab_blocked_sample_count = $audit.csab_blocked_samples.Count
    skinned_model_record_count = $audit.skinned_model_records.Count
    parse_error_count = $audit.parse_error_count
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.file_count -eq 349) "expected 349 actor files"
    Assert-Condition ($summary.archive_count -eq 348) "expected 348 actor ZAR archives"
    Assert-Condition ($summary.loose_cmb_count -eq 1) "expected 1 loose actor CMB"
    Assert-Condition ($summary.model_counts.discovered -eq 1310) "expected 1310 discovered actor CMB models"
    Assert-Condition ($summary.model_counts.parsed -eq 1310) "expected 1310 parsed actor CMB models"
    Assert-Condition ($summary.model_counts.parse_errors -eq 0) "expected 0 actor CMB parse errors"
    Assert-Condition ($summary.model_counts.skinned_model_count -eq 202) "expected 202 actor CMBs blocked by skinning"
    Assert-Condition ($summary.model_counts.skinned_archive_count -eq 146) "expected 146 actor archives with skinned CMBs"
    Assert-Condition ($summary.model_counts.skinned_archives_with_csab -eq 142) "expected 142 skinned actor archives with CSAB payloads"
    Assert-Condition ($summary.model_counts.skinned_archives_with_cmab -eq 90) "expected 90 skinned actor archives with CMAB payloads"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "static_cmb_export_supported") -eq 974) "expected 974 static CMBs"
    Assert-Condition ((Get-JsonValue $summary.support_status_counts "rigid_multibone_export_supported") -eq 134) "expected 134 rigid multi-bone CMBs"
    Assert-Condition ((Get-JsonValue $summary.skinned_support_status_counts "needs_skinning_mode_1_support") -eq 13) "expected 13 CMBs needing skinning mode 1"
    Assert-Condition ((Get-JsonValue $summary.skinned_support_status_counts "needs_skinning_mode_2_support") -eq 140) "expected 140 CMBs needing skinning mode 2"
    Assert-Condition ((Get-JsonValue $summary.skinned_support_status_counts "needs_skinning_mode_1_and_2_support") -eq 49) "expected 49 CMBs needing skinning modes 1 and 2"
    Assert-Condition ((Get-JsonValue $summary.skinned_primitive_mode_counts "1") -eq 97) "expected 97 skinned primitives using mode 1"
    Assert-Condition ((Get-JsonValue $summary.skinned_primitive_mode_counts "2") -eq 1000) "expected 1000 skinned primitives using mode 2"
    Assert-Condition ((Get-JsonValue $summary.primitive_bone_count_counts "1") -eq 3535) "expected 3535 one-bone primitives"
    Assert-Condition ((Get-JsonValue $summary.primitive_bone_count_counts "2") -eq 294) "expected 294 two-bone primitives"
    Assert-Condition ((Get-JsonValue $summary.primitive_bone_count_counts "10") -eq 133) "expected 133 ten-bone primitives"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=1;bones=2") -eq 56) "expected 56 mode-1 two-bone primitives"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=1;bones=10") -eq 2) "expected 2 mode-1 ten-bone primitives"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=2;bones=2") -eq 238) "expected 238 mode-2 two-bone primitives"
    Assert-Condition ((Get-JsonValue $summary.skinning_mode_bone_count_counts "mode=2;bones=10") -eq 131) "expected 131 mode-2 ten-bone primitives"
    Assert-Condition ($summary.skinned_primitive_profile.primitive_count -eq 1815) "expected 1815 primitives in skinned models"
    Assert-Condition ($summary.skinned_primitive_profile.skinned_primitive_count -eq 1097) "expected 1097 skinned primitives"
    Assert-Condition ($summary.skinned_primitive_profile.rigid_primitive_count -eq 718) "expected 718 rigid primitives inside skinned models"
    Assert-Condition ($summary.skinned_primitive_profile.skinned_triangle_count -eq 126001) "expected 126001 skinned triangles"
    Assert-Condition ($summary.skinned_primitive_profile.skinned_index_count -eq 378003) "expected 378003 skinned vertex index references"
    Assert-Condition ($summary.skinned_primitive_profile.max_bone_palette_size -eq 10) "expected max bone palette size 10"
    Assert-Condition ($summary.skinned_primitive_profile.max_bone_index -eq 45) "expected max referenced bone index 45"
    Assert-Condition ($summary.skinned_primitive_profile.invalid_bone_index_primitives -eq 0) "expected 0 out-of-range bone palette primitives"
    Assert-Condition ($summary.skinned_primitive_profile.duplicate_bone_palette_primitives -eq 0) "expected 0 duplicate bone palette primitives"
    Assert-Condition ($summary.skinned_primitive_profile.empty_bone_palette_primitives -eq 0) "expected 0 empty bone palette primitives"
    Assert-Condition ($summary.csab_counts.considered -eq 2465) "expected 2465 considered CSAB payloads"
    Assert-Condition ($summary.csab_counts.failed -eq 0) "expected 0 CSAB target audit failures"
    Assert-Condition ($summary.csab_counts.target_export_supported_by_current_rigid_path -eq 44) "expected 44 CSAB targets supported by the current rigid path"
    Assert-Condition ($summary.csab_counts.target_needs_skinning_support -eq 2271) "expected 2271 CSAB targets blocked by skinning"
    Assert-Condition ($summary.csab_counts.target_unresolved_or_missing -eq 150) "expected 150 unresolved or missing CSAB targets"
    Assert-Condition ($summary.csab_counts.target_needs_unknown_support -eq 0) "expected 0 CSAB targets blocked by unknown support"
    Assert-Condition ((Get-JsonValue $summary.csab_target_support_counts "needs_skinning_mode_1_support") -eq 46) "expected 46 CSAB targets needing skinning mode 1"
    Assert-Condition ((Get-JsonValue $summary.csab_target_support_counts "needs_skinning_mode_2_support") -eq 1247) "expected 1247 CSAB targets needing skinning mode 2"
    Assert-Condition ((Get-JsonValue $summary.csab_target_support_counts "needs_skinning_mode_1_and_2_support") -eq 978) "expected 978 CSAB targets needing skinning modes 1 and 2"
    Assert-Condition ($summary.skinned_model_record_count -eq 202) "expected 202 skinned model records"
    Assert-Condition ($summary.parse_error_count -eq 0) "expected 0 parse errors"
}

Write-Host "OOT3D actor skinning queue audit: $auditOutput"
Write-Host "OOT3D actor skinning queue summary: $summaryOutput"
