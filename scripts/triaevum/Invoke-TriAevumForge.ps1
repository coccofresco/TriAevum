[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ForgeArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$forge = Join-Path $repoRoot "tools\triaevum_release\forge.py"
& python $forge @ForgeArguments
exit $LASTEXITCODE
