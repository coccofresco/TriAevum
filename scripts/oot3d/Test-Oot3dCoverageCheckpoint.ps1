Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$matrixScript = Join-Path $PSScriptRoot "Test-Oot3dAzaharCoverageMatrix.ps1"
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($matrixScript, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
# Exercise the actual publisher without executing the matrix's emulator loop.
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq "Write-MatrixState"
}, $true)
. ([scriptblock]::Create($function.Extent.Text))
$CatalogPath = Join-Path $PSScriptRoot "../../tools/oot3d/native_a32_runtime/oot3d_azahar_coverage_scenarios.json"
$catalog = Get-Content -Raw -LiteralPath $CatalogPath | ConvertFrom-Json
$Mode = "smoke"
$Backend = "vulkan"
$ScenarioListPath = ""
$CaptureFramesOverride = 12
$CaptureWindowOffsetsSeconds = @(0, 4, 12)
$MaxVerticesPerDraw = 1
[switch]$SkipFramebuffer = $true
[switch]$CompactEvidence = $true
[switch]$ShaderSeed = $true
$stub = New-TemporaryFile
$path = $stub.FullName
try {
    $results = [System.Collections.ArrayList]::new()
    for ($i = 0; $i -lt 3; ++$i) {
        $null = Write-MatrixState $path $catalog @($catalog.scenarios[0]) $results
        $saved = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
        if ($saved.format -ne "oot3d_azahar_coverage_matrix_v1" -or
            $saved.capture_policy.capture_window_offsets_seconds.Count -ne 3) {
            throw "Published checkpoint is incomplete."
        }
    }
    $reader = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    $blocked = $false
    try { $null = Write-MatrixState $path $catalog @($catalog.scenarios[0]) $results }
    catch [System.IO.IOException] { $blocked = $true }
    finally { $reader.Dispose() }
    if (-not $blocked -or -not (Test-Path -LiteralPath ($path + ".tmp"))) {
        throw "Blocked publication did not preserve its recoverable pending checkpoint."
    }
    $null = Write-MatrixState $path $catalog @($catalog.scenarios[0]) $results
    Write-Host "Coverage checkpoint: replacement, bounded lock failure and recovery passed."
} finally {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath ($path + ".tmp") -Force -ErrorAction SilentlyContinue
}
