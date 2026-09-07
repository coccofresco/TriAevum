param(
    [string]$ToolRoot = "",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [int]$MaxAgeDays = 45,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "playable_coverage"
}

if (-not (Test-Path -LiteralPath $ToolRoot)) {
    throw "OOT3D asset tool root not found: $ToolRoot"
}
if (-not (Test-Path -LiteralPath $WorkRoot)) {
    throw "OOT3D work root not found: $WorkRoot"
}

$outputJson = Join-Path $OutputRoot "oot3d_asset_coverage_baseline.json"
$outputMarkdown = Join-Path $OutputRoot "oot3d_asset_coverage_baseline.md"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "-m", "oot3d_asset_tool.playable_coverage",
        "--work-root", $WorkRoot,
        "--output-json", $outputJson,
        "--output-md", $outputMarkdown,
        "--max-age-days", [string]$MaxAgeDays
    )
    if ($Verify) {
        $arguments += "--require-complete"
    }
    & python @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D playable coverage baseline failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

if ($Verify) {
    $report = Get-Content -LiteralPath $outputJson -Raw | ConvertFrom-Json
    if ($report.format -ne "oot3d_playable_asset_coverage_v1") {
        throw "Unexpected playable coverage format: $($report.format)"
    }
    if ($report.status -ne "complete") {
        throw "Playable coverage baseline is incomplete: $($report.status)"
    }
    if ($report.family_count -lt 10) {
        throw "Playable coverage baseline has too few asset families: $($report.family_count)"
    }
    if (@($report.families | Where-Object { $_.family_id -eq "scene_rooms" }).Count -ne 1) {
        throw "Playable coverage baseline is missing the scene_rooms family"
    }
    if (@($report.opportunities).Count -eq 0) {
        throw "Playable coverage baseline produced no implementation opportunities"
    }
}

Write-Host "OOT3D playable coverage JSON: $outputJson"
Write-Host "OOT3D playable coverage Markdown: $outputMarkdown"
