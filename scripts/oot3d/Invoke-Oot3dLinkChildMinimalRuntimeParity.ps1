param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$CharacterManifest = "",
    [string[]]$CsabName = @(
        "boy/anim/nml_wait_free.csab",
        "child/anim/nml_walk_free.csab",
        "child/anim/nml_run_free.csab",
        "boy/anim/nml_jump.csab",
        "child/anim/cl_nml_climb_startA.csab",
        "child/anim/cl_nml_climb_upL.csab",
        "child/anim/cl_nml_climb_upR.csab",
        "child/anim/cl_nml_climb_endAL.csab"
    ),
    [string]$OutputRoot = "",
    [int]$FrameStep = 1,
    [double]$PositionTolerance = 0.00001,
    [double]$NormalTolerance = 0.00005,
    [int]$SampleLimit = 20,
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
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "character_conversion\minimal_runtime_player_parity"
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
        throw "OOT3D Link child minimal runtime parity failed: $Message"
    }
}

function Convert-CsabNameToSlug([string]$Name) {
    $slug = ($Name -replace '[^A-Za-z0-9]+', '_').Trim('_').ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($slug)) {
        return "animation"
    }
    return $slug
}

Require-Path $ToolRoot "Tool root"
Require-Path $CharacterManifest "Link child character conversion manifest"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$summaryOutput = Join-Path $OutputRoot "link_child_minimal_runtime_parity_summary.json"
$records = @()
$allValid = $true

foreach ($name in $CsabName) {
    if ([string]::IsNullOrWhiteSpace($name)) {
        continue
    }
    $slug = Convert-CsabNameToSlug $name
    $auditOutput = Join-Path $OutputRoot "link_child_${slug}_audit.json"
    $runtimeGlbOutput = Join-Path $OutputRoot "link_child_${slug}_minimal_runtime.glb"
    $validatedGlbOutput = Join-Path $OutputRoot "link_child_${slug}_validated.glb"
    $validatedManifestOutput = Join-Path $OutputRoot "link_child_${slug}_validated.manifest.json"

    Push-Location $ToolRoot
    try {
        $env:PYTHONPATH = Join-Path $ToolRoot "src"
        Invoke-Oot3dTool -Arguments @(
            "audit-minimal-runtime-player-export",
            $CharacterManifest,
            "--output",
            $auditOutput,
            "--runtime-glb-output",
            $runtimeGlbOutput,
            "--validated-glb-output",
            $validatedGlbOutput,
            "--validated-manifest-output",
            $validatedManifestOutput,
            "--csab-name",
            $name,
            "--frame-step",
            [string]$FrameStep,
            "--fps",
            "60",
            "--interpolation",
            "STEP",
            "--position-tolerance",
            [string]$PositionTolerance,
            "--normal-tolerance",
            [string]$NormalTolerance,
            "--sample-limit",
            [string]$SampleLimit
        )
    }
    finally {
        Pop-Location
    }

    Require-Path $auditOutput "Minimal runtime parity audit"
    Require-Path $runtimeGlbOutput "Minimal runtime GLB"
    Require-Path $validatedGlbOutput "Validated oracle GLB"

    $audit = Get-Content -LiteralPath $auditOutput -Raw | ConvertFrom-Json
    if ($audit.status -ne "valid") {
        $allValid = $false
    }
    if ($Verify) {
        Assert-Condition ($audit.status -eq "valid") "expected valid parity audit for $name"
        Assert-Condition ($audit.runtime_manifest.counts.primitive_count -eq 12) "expected 12 selected Link child primitives for $name"
        Assert-Condition ($audit.runtime_manifest.counts.skinned_primitive_count -eq 10) "expected 10 selected skinned primitives for $name"
        Assert-Condition ($audit.runtime_manifest.counts.rigid_primitive_count -eq 2) "expected 2 selected rigid face primitives for $name"
        Assert-Condition ($audit.comparison.runtime_primitive_count -eq 12) "expected 12 runtime GLB primitives for $name"
        Assert-Condition ($audit.comparison.validated_primitive_count -eq 12) "expected 12 validated GLB primitives for $name"
        Assert-Condition ($audit.comparison.matched_primitive_count -eq 12) "expected all primitives to match for $name"
        Assert-Condition ($audit.comparison.position_mismatch_count -eq 0) "expected zero position mismatches for $name"
        Assert-Condition ($audit.comparison.normal_mismatch_count -eq 0) "expected zero normal mismatches for $name"
        Assert-Condition ($audit.comparison.issue_count -eq 0) "expected zero parity issues for $name"
    }

    $records += [ordered]@{
        csab_name = $name
        status = $audit.status
        audit = $auditOutput
        runtime_glb = $runtimeGlbOutput
        validated_glb = $validatedGlbOutput
        runtime_primitive_count = $audit.comparison.runtime_primitive_count
        validated_primitive_count = $audit.comparison.validated_primitive_count
        matched_frame_count = $audit.comparison.matched_frame_count
        matched_vertex_rows = $audit.comparison.matched_vertex_rows
        position_mismatch_count = $audit.comparison.position_mismatch_count
        normal_mismatch_count = $audit.comparison.normal_mismatch_count
        issue_count = $audit.comparison.issue_count
        max_position_delta = $audit.comparison.max_position_delta
        max_normal_delta = $audit.comparison.max_normal_delta
    }
}

$summary = [ordered]@{
    status = if ($allValid) { "valid" } else { "invalid" }
    character_manifest = (Resolve-Path $CharacterManifest).Path
    output_root = $OutputRoot
    animation_count = $records.Count
    csab_names = @($records | ForEach-Object { $_.csab_name })
    records = $records
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryOutput -Encoding UTF8
if ($Verify) {
    Assert-Condition ($summary.status -eq "valid") "expected all minimal runtime parity audits valid"
    Assert-Condition ($summary.animation_count -eq $CsabName.Count) "expected every requested animation to be audited"
}

Write-Host "OOT3D Link child minimal runtime parity summary: $summaryOutput"
foreach ($record in $records) {
    Write-Host "OOT3D Link child minimal runtime parity audit: $($record.audit)"
}
