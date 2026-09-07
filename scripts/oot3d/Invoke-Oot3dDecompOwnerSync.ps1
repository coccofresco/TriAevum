[CmdletBinding()]
param(
    [string]$DecompRoot = "I:\oot3decomp",
    [string]$Revision = "HEAD",
    [string]$OutputRoot = "I:\oot3dre_work\source_integration",
    [string]$Config = "",
    [switch]$Replace,
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$tool = Join-Path $repoRoot "tools\oot3d\source_integration\build_owner_closure.py"
if ([string]::IsNullOrWhiteSpace($Config)) {
    $Config = Join-Path $repoRoot "tools\oot3d\source_integration\owner_roots.json"
}

foreach ($required in @($tool, $Config, $DecompRoot)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required OOT3D owner-sync input is missing: $required"
    }
}

if ($RunTests) {
    $tests = Join-Path $repoRoot "tools\oot3d\source_integration\test_build_owner_closure.py"
    & python $tests
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$arguments = @(
    $tool,
    "--decomp-root", ([System.IO.Path]::GetFullPath($DecompRoot)),
    "--runtime-root", $repoRoot,
    "--config", ([System.IO.Path]::GetFullPath($Config)),
    "--revision", $Revision,
    "--output-root", ([System.IO.Path]::GetFullPath($OutputRoot))
)
if ($Replace) {
    $arguments += "--replace"
}

& python @arguments
exit $LASTEXITCODE
