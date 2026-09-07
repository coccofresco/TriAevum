[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
& python -m unittest discover `
    (Join-Path $repoRoot "tools\triaevum_release") -p "test_*.py" -v
exit $LASTEXITCODE
