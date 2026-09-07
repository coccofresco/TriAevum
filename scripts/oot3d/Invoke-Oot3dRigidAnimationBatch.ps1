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
        throw "OOT3D rigid animation batch verification failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $ActorRoot "OOT3D actor directory"

$outputRoot = Join-Path $WorkRoot "rigid_animation_batch"
$manifestOutput = Join-Path $outputRoot "csab_rigid_track_batch_manifest.json"
$summaryOutput = Join-Path $outputRoot "rigid_animation_batch_summary.json"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Invoke-Oot3dTool -Arguments @(
        "batch-csab-rigid-tracks",
        $ActorRoot,
        "--output",
        $outputRoot
    )
}
finally {
    Pop-Location
}

$manifest = Get-Content -LiteralPath $manifestOutput -Raw | ConvertFrom-Json
$summary = [ordered]@{
    actor_root = (Resolve-Path $ActorRoot).Path
    output_root = $outputRoot
    manifest = $manifestOutput
    considered_csab = $manifest.considered_csab
    exported = $manifest.exported
    failed = $manifest.failed
    target_unresolved_or_missing = $manifest.target_unresolved_or_missing
    target_needs_skinning_support = $manifest.target_needs_skinning_support
    target_needs_unknown_support = $manifest.target_needs_unknown_support
    track_file_count = (Get-ChildItem -LiteralPath (Join-Path $outputRoot "tracks") -Filter *.json).Count
    counts = $manifest.counts
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8

if ($Verify) {
    Assert-Condition ($summary.considered_csab -eq 2465) "expected 2465 considered CSAB payloads"
    Assert-Condition ($summary.exported -eq 44) "expected 44 exported rigid CSAB track manifests"
    Assert-Condition ($summary.failed -eq 0) "expected 0 failed rigid CSAB track exports"
    Assert-Condition ($summary.target_unresolved_or_missing -eq 150) "expected 150 unresolved or missing CSAB targets"
    Assert-Condition ($summary.target_needs_skinning_support -eq 2271) "expected 2271 CSAB targets blocked by skinning"
    Assert-Condition ($summary.target_needs_unknown_support -eq 0) "expected 0 CSAB targets blocked by unknown support"
    Assert-Condition ($summary.track_file_count -eq 44) "expected 44 generated track JSON files"
    Assert-Condition ($summary.counts.track_count -eq 237) "expected 237 exported bone tracks"
    Assert-Condition ($summary.counts.channel_count -eq 1311) "expected 1311 exported f32 channels"
    Assert-Condition ($summary.counts.const_channel_count -eq 1126) "expected 1126 constant f32 channels"
    Assert-Condition ($summary.counts.keyed_channel_count -eq 185) "expected 185 keyed f32 channels"
    Assert-Condition ($summary.counts.keyframe_count -eq 2922) "expected 2922 f32 keyframes"
    Assert-Condition ($summary.counts.frame_slot_count -eq 3714) "expected 3714 frame slots"
    Assert-Condition ($summary.counts.sampled_channel_values -eq 158688) "expected 158688 sampled channel values"
    Assert-Condition ($summary.counts.finite_world_matrix_entries -eq 474336) "expected 474336 finite world matrix entries"
    Assert-Condition ($summary.counts.non_f32_channel_blocks -eq 0) "expected 0 non-f32 channel blocks"
}

Write-Host "OOT3D rigid animation batch manifest: $manifestOutput"
Write-Host "OOT3D rigid animation batch summary: $summaryOutput"
