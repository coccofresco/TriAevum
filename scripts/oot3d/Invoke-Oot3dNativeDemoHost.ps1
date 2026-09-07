param(
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json",
    [string]$OutputRoot = "",
    [string]$ReferenceTrace = "",
    [switch]$Verify,
    [switch]$RequireReference,
    [switch]$LaunchViewer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$hostScript = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_native_host.py"
if (-not (Test-Path -LiteralPath $hostScript)) {
    throw "OOT3D native demo host script not found: $hostScript"
}
if (-not (Test-Path -LiteralPath $Manifest)) {
    throw "OOT3D standalone manifest not found: $Manifest"
}

$args = @($hostScript, "--manifest", $Manifest)
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
    $args += @("--output-root", $OutputRoot)
}
if ($LaunchViewer) {
    $args += @("--launch-viewer")
}

& python @args
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native demo host probe failed with exit code $LASTEXITCODE"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Split-Path -Parent $Manifest) "native_host"
}

$compareScript = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_trace_compare.py"
$candidateTrace = Join-Path $OutputRoot "native_host_trace.json"
$compareOutput = Join-Path $OutputRoot "native_host_trace_compare.json"
$compareArgs = @(
    $compareScript,
    "--candidate", $candidateTrace,
    "--output", $compareOutput
)
if (-not [string]::IsNullOrWhiteSpace($ReferenceTrace)) {
    $compareArgs += @("--reference", $ReferenceTrace)
}
if ($RequireReference) {
    $compareArgs += "--require-reference"
}

& python @compareArgs
if ($LASTEXITCODE -ne 0) {
    throw "OOT3D native demo trace comparison failed with exit code $LASTEXITCODE"
}

if ($Verify) {
    $probe = Join-Path $OutputRoot "native_host_probe.json"
    if (-not (Test-Path -LiteralPath $probe)) {
        throw "Native host probe summary was not written: $probe"
    }
    $json = Get-Content -LiteralPath $probe -Raw | ConvertFrom-Json
    if ($json.status -ne "valid" -or $json.issue_count -ne 0) {
        throw "Native host probe summary is not valid: $probe"
    }
    if (-not (Test-Path -LiteralPath $compareOutput)) {
        throw "Native host trace comparison was not written: $compareOutput"
    }
    $compareJson = Get-Content -LiteralPath $compareOutput -Raw | ConvertFrom-Json
    if ($RequireReference) {
        if ($compareJson.status -ne "passed" -or $compareJson.parity_passed -ne $true) {
            throw "Native host trace comparison did not pass against required reference: $compareOutput"
        }
    } elseif ($compareJson.gate_available -ne $true -or ($compareJson.status -ne "passed" -and $compareJson.status -ne "missing_reference")) {
        throw "Native host trace comparison gate is not available: $compareOutput"
    }
}
