param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,
    [string]$OutputPath,
    [string]$Manifest,
    [string]$ManifestOut,
    [ValidateSet("auto", "copy-all", "json", "jsonl")]
    [string]$InputFormat = "auto",
    [switch]$RequireShadowRegisters
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$importer = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_pica_trace_import.py"

if (-not (Test-Path $importer)) {
    throw "PICA trace importer was not found: $importer"
}

if (-not $OutputPath) {
    $inputItem = Get-Item $InputPath
    $OutputPath = Join-Path $inputItem.DirectoryName (($inputItem.BaseName) + ".native_pica_register_trace.json")
}

$argsList = @(
    $importer,
    "--input", $InputPath,
    "--output", $OutputPath,
    "--input-format", $InputFormat
)

if ($Manifest -or $ManifestOut) {
    if (-not ($Manifest -and $ManifestOut)) {
        throw "-Manifest and -ManifestOut must be provided together"
    }
    $argsList += @("--manifest", $Manifest, "--manifest-out", $ManifestOut)
}

if ($RequireShadowRegisters) {
    $argsList += "--require-shadow-registers"
}

& python @argsList
if ($LASTEXITCODE -ne 0) {
    throw "PICA trace import failed with exit code $LASTEXITCODE"
}
