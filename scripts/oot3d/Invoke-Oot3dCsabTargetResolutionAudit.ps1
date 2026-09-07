param(
    [string]$ToolRoot = "",
    [string]$RomFs = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$ActorRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
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
        throw "OOT3D CSAB target resolution audit verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "csab_target_resolution_audit"
$auditOutput = Join-Path $outputRoot "oot3d_csab_target_resolution_audit.json"
$summaryOutput = Join-Path $outputRoot "oot3d_csab_target_resolution_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "audit-csab-target-resolution",
        $ActorRoot,
        "--output",
        $auditOutput
    )
}
finally {
    Pop-Location
}

$audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    audit = $auditOutput
    considered_csab = $audit.considered_csab
    resolved_count = $audit.resolved_count
    unresolved_or_missing = $audit.unresolved_or_missing
    archive_parse_error_count = $audit.archive_parse_error_count
    target_resolution_counts = $audit.target_resolution_counts
    target_support_counts = $audit.target_support_counts
    unresolved_archive_counts = $audit.unresolved_archive_counts
    unresolved_same_bone_candidate_count_counts = $audit.unresolved_same_bone_candidate_count_counts
    unresolved_namespace_candidate_count_counts = $audit.unresolved_namespace_candidate_count_counts
    unresolved_same_bone_support_counts = $audit.unresolved_same_bone_support_counts
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.considered_csab -eq 2465) "expected 2465 considered CSAB payloads"
    Assert-Condition ($summary.resolved_count -eq 2315) "expected 2315 conservatively resolved CSAB targets"
    Assert-Condition ($summary.unresolved_or_missing -eq 150) "expected 150 unresolved or missing CSAB targets"
    Assert-Condition ($summary.archive_parse_error_count -eq 0) "expected 0 archive parse errors"

    Assert-Condition ($summary.target_resolution_counts.single_bone_count_match -eq 2275) "expected 2275 single bone-count matches"
    Assert-Condition ($summary.target_resolution_counts.multiple_bone_count_exact_name_match -eq 10) "expected 10 exact-name multiple matches"
    Assert-Condition ($summary.target_resolution_counts.multiple_bone_count_contained_name_match -eq 30) "expected 30 contained-name multiple matches"
    Assert-Condition ($summary.target_resolution_counts.multiple_bone_count_unresolved -eq 139) "expected 139 unresolved multiple matches"
    Assert-Condition ($summary.target_resolution_counts.no_matching_cmb_bone_count -eq 11) "expected 11 no-matching-CMB records"

    Assert-Condition ($summary.target_support_counts.needs_skinning_mode_1_support -eq 46) "expected 46 mode-1 skinned targets"
    Assert-Condition ($summary.target_support_counts.needs_skinning_mode_2_support -eq 1247) "expected 1247 mode-2 skinned targets"
    Assert-Condition ($summary.target_support_counts.needs_skinning_mode_1_and_2_support -eq 978) "expected 978 mixed skinned targets"
    Assert-Condition ($summary.target_support_counts.rigid_multibone_export_supported -eq 37) "expected 37 rigid multi-bone targets"
    Assert-Condition ($summary.target_support_counts.static_cmb_export_supported -eq 7) "expected 7 static CMB targets"

    Assert-Condition ($summary.unresolved_archive_counts.'zelda_keep_opening.zar' -eq 11) "expected 11 keep-opening no-match records"
    Assert-Condition ($summary.unresolved_archive_counts.'zelda_mb.zar' -eq 28) "expected 28 zelda_mb unresolved records"
    Assert-Condition ($summary.unresolved_archive_counts.'zelda_ec.zar' -eq 19) "expected 19 zelda_ec unresolved records"
    Assert-Condition ($summary.unresolved_archive_counts.'zelda_sst.zar' -eq 16) "expected 16 zelda_sst unresolved records"
    Assert-Condition ($summary.unresolved_archive_counts.'zelda_tw.zar' -eq 16) "expected 16 zelda_tw unresolved records"

    Assert-Condition ($summary.unresolved_same_bone_candidate_count_counts.'0' -eq 11) "expected 11 records with no same-bone candidate"
    Assert-Condition ($summary.unresolved_same_bone_candidate_count_counts.'2' -eq 114) "expected 114 records with two same-bone candidates"
    Assert-Condition ($summary.unresolved_same_bone_candidate_count_counts.'3' -eq 13) "expected 13 records with three same-bone candidates"
    Assert-Condition ($summary.unresolved_same_bone_candidate_count_counts.'4' -eq 4) "expected 4 records with four same-bone candidates"
    Assert-Condition ($summary.unresolved_same_bone_candidate_count_counts.'5' -eq 8) "expected 8 records with five same-bone candidates"

    Assert-Condition ($summary.unresolved_same_bone_support_counts.needs_skinning_mode_2_support -eq 277) "expected 277 unresolved same-bone mode-2 candidates"
    Assert-Condition ($summary.unresolved_same_bone_support_counts.needs_skinning_mode_1_and_2_support -eq 26) "expected 26 unresolved same-bone mixed candidates"
    Assert-Condition ($summary.unresolved_same_bone_support_counts.needs_skinning_mode_1_support -eq 15) "expected 15 unresolved same-bone mode-1 candidates"
    Assert-Condition ($summary.unresolved_same_bone_support_counts.rigid_multibone_export_supported -eq 5) "expected 5 unresolved same-bone rigid candidates"
}

Write-Host "OOT3D CSAB target resolution audit: $auditOutput"
Write-Host "OOT3D CSAB target resolution summary: $summaryOutput"
