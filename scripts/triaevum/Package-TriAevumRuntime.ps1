[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Output,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$Layout = "",
    [string]$SourceCommit = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if([string]::IsNullOrWhiteSpace($Layout)) {
    $Layout = Join-Path $repoRoot `
        "tools\triaevum_release\runtime_release_layout.example.json"
}
if([string]::IsNullOrWhiteSpace($SourceCommit)) {
    $SourceCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0) {
        throw "Unable to resolve the source commit"
    }
}

$packager = Join-Path $repoRoot "tools\triaevum_release\package_release.py"
& python $packager --layout $Layout --source-root $repoRoot --output $Output `
    --version $Version --source-commit $SourceCommit
exit $LASTEXITCODE
