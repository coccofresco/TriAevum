[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SemanticManifest,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$SourceCommit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$manifestPath = [System.IO.Path]::GetFullPath($SemanticManifest)
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Semantic manifest does not exist: $manifestPath"
}
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$boundaries = @(
    $manifest.functions | ForEach-Object {
        [pscustomobject]@{
            Entry = [Convert]::ToUInt32($_.entry.Substring(2), 16)
            End = [Convert]::ToUInt32($_.end_exclusive.Substring(2), 16)
        }
    } | Sort-Object Entry, End -Unique
)
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("#include <array>")
$lines.Add("#include <cstdint>")
$lines.Add("")
$lines.Add("namespace oot3d::recomp::mass_metadata {")
$lines.Add("")
$lines.Add("struct FunctionBoundary {")
$lines.Add("    std::uint32_t entry;")
$lines.Add("    std::uint32_t end;")
$lines.Add("};")
$lines.Add("")
$lines.Add("inline constexpr char kSourceCommit[] = `"$SourceCommit`";")
$lines.Add("inline constexpr char kSemanticManifestSha256[] = `"$manifestHash`";")
$lines.Add("inline constexpr std::array<FunctionBoundary, $($boundaries.Count)> kFunctionBoundaries{{")
foreach ($boundary in $boundaries) {
    $lines.Add(("    {{0x{0:X8}U, 0x{1:X8}U}}," -f $boundary.Entry, $boundary.End))
}
$lines.Add("}};")
$lines.Add("")
$lines.Add("} // namespace oot3d::recomp::mass_metadata")

$destination = Join-Path $outputPath "oot3d_mass_function_boundaries.h"
[System.IO.File]::WriteAllLines(
    $destination,
    $lines,
    [System.Text.UTF8Encoding]::new($false))
Write-Output "Wrote $($boundaries.Count) function boundaries to $destination"
