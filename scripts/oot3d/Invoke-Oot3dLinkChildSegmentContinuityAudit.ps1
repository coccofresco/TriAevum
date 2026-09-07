param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$CharacterManifest = "",
    [string[]]$CsabName = @(
        "child/anim/cl_nml_climb_startA.csab",
        "child/anim/cl_nml_climb_upL.csab",
        "child/anim/cl_nml_climb_upR.csab",
        "child/anim/cl_nml_climb_endAL.csab"
    ),
    [string]$Output = "",
    [double]$Tolerance = 0.001,
    [int]$SampleLimit = 100,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($CharacterManifest)) {
    $CharacterManifest = Join-Path $WorkRoot "character_conversion\link_child_character_conversion_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $WorkRoot "character_conversion\link_child_segment_continuity_audit.json"
}

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "OOT3D Link child segment continuity audit failed: $Message"
    }
}

Require-Path $ToolRoot "Tool root"
Require-Path $CharacterManifest "Link child character conversion manifest"
New-Item -ItemType Directory -Force -Path (Split-Path $Output) | Out-Null

$arguments = @(
    "audit-character-segment-continuity",
    $CharacterManifest,
    "--output",
    $Output,
    "--tolerance",
    [string]$Tolerance,
    "--sample-limit",
    [string]$SampleLimit
)
foreach ($name in $CsabName) {
    $arguments += @("--csab-name", $name)
}

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    Write-Host "python -m oot3d_asset_tool $($arguments -join ' ')"
    & python -m oot3d_asset_tool @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_asset_tool failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Require-Path $Output "Segment continuity audit"
$audit = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json

if ($Verify) {
    Assert-Condition ($audit.status -eq "valid") "expected normalized segment continuity to be valid"
    Assert-Condition ($audit.segment_count -eq $CsabName.Count) "expected every requested CSAB segment to be audited"
    Assert-Condition ($audit.transition_count -eq [Math]::Max(0, $CsabName.Count - 1)) "expected transition count to match segment chain"
    Assert-Condition ($audit.normalized_discontinuity_count -eq 0) "expected zero normalized discontinuities"
    Assert-Condition ($audit.max_normalized_transition_delta -le $Tolerance) "expected normalized transition delta within tolerance"
}

Write-Host "OOT3D Link child segment continuity audit: $Output"
