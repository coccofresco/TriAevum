Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dAzaharCoverageScenario.ps1"
$stub = New-TemporaryFile
try {
    # Validation must reject these before reading the catalog or starting a process.
    '{"format":"invalid-test-catalog"}' | Set-Content -LiteralPath $stub.FullName -Encoding ascii
    $cases = @(
        @{ offsets = @(); valid = $false },
        @{ offsets = @(1); valid = $false },
        @{ offsets = @(0, 0); valid = $false },
        @{ offsets = @(0, -1); valid = $false },
        @{ offsets = @(0, 4, 2); valid = $false },
        @{ offsets = @(0, [double]::NaN); valid = $false },
        @{ offsets = @(0, [double]::PositiveInfinity); valid = $false },
        @{ offsets = @(0); valid = $true },
        @{ offsets = @(0, 4, 12); valid = $true }
    )
    foreach ($case in $cases) {
        $message = ""
        try {
            & $launcher -ScenarioId synthetic -CatalogPath $stub.FullName `
                -AzaharExe $stub.FullName -RomPath $stub.FullName `
                -CaptureWindowOffsetsSeconds $case.offsets | Out-Null
        } catch { $message = $_.Exception.Message }
        $expected = if ($case.valid) { "Unsupported Azahar coverage catalog" } else { "Capture window offsets|CaptureWindowOffsetsSeconds" }
        if ($message -notmatch $expected) {
            throw "Unexpected validation result for offsets '$($case.offsets)': $message"
        }
    }
    Write-Host "Azahar capture schedule: $($cases.Count) validation cases passed; no emulator launched."
} finally {
    Remove-Item -LiteralPath $stub.FullName -Force
}
