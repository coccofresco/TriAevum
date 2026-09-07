[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Dump,
    [string]$PackageDirectory = "",
    [string]$Output = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
}
$root = [System.IO.Path]::GetFullPath($PackageDirectory)
$resolvedDump = [System.IO.Path]::GetFullPath($Dump)
$resolver = Join-Path $root "symbols\resolve_whole_aot_minidump.py"
$guestMap = Join-Path $root "symbols\whole_aot_guest_map.json"
$linkMap = Join-Path $root "symbols\oot3d_native_game.map.zip"
foreach($required in @($resolvedDump, $resolver, $guestMap, $linkMap)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Whole-AOT crash-resolution input is missing: $required"
    }
}
if([string]::IsNullOrWhiteSpace($Output)) {
    $Output = "$resolvedDump.resolution.json"
}
$resolvedOutput = [System.IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path -Parent $resolvedOutput
if(-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

& python $resolver --dump $resolvedDump --guest-map $guestMap `
    --link-map $linkMap --output $resolvedOutput
if($LASTEXITCODE -ne 0) {
    throw "Whole-AOT crash resolution failed with code $LASTEXITCODE"
}
