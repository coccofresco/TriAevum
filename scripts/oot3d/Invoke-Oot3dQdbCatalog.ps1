param(
    [string]$ToolRoot = "",
    [string]$RomfsRoot = "E:\ppssppvr\oot3d_decomp\work\extract\romfs",
    [string]$WorkRoot = "I:\oot3dre_work",
    [string]$OutputRoot = "",
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $WorkRoot "qdb_catalog"
}
$outputJson = Join-Path $OutputRoot "oot3d_qdb_catalog.json"
$outputMarkdown = Join-Path $OutputRoot "oot3d_qdb_catalog.md"
$archive = Join-Path $OutputRoot "oot3d-cutscenes.o2r"
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

Push-Location $ToolRoot
try {
    $env:PYTHONPATH = Join-Path $ToolRoot "src"
    $arguments = @(
        "-m", "oot3d_asset_tool.qdb_catalog",
        "--root", "scene=$(Join-Path $RomfsRoot 'scene')",
        "--root", "actor=$(Join-Path $RomfsRoot 'actor')",
        "--output-json", $outputJson,
        "--output-md", $outputMarkdown,
        "--archive", $archive
    )
    if ($Verify) { $arguments += "--verify" }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "OOT3D QDB catalog failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

Write-Host "OOT3D QDB catalog: $outputJson"
Write-Host "OOT3D QDB archive: $archive"
