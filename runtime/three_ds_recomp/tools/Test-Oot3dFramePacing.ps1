[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(6, 120)]
    [int]$Frames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot "tools\Invoke-Oot3dRendererDev.ps1"
$buildScript = Join-Path $rendererRoot `
    "..\..\tools\oot3d\build_fast_dev.ps1"
$buildDirectory = Join-Path $rendererRoot ".renderer-dev\build"
$foundationTests = Join-Path $buildDirectory `
    "three_ds_recomp_runtime\tests\oot3d_graphics_foundation_tests.exe"
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$summaryPath = Join-Path $artifactRoot "native-game-summary.json"
$diagnosticsPath = Join-Path $artifactRoot "vulkan-diagnostics.json"

$environmentNames = @(
    "OOT3D_GRAPHICS_FRAME_RATE",
    "OOT3D_VULKAN_VALIDATION"
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Invoke-PacingCase {
    param(
        [string]$Mode,
        [Nullable[uint32]]$ExpectedRate,
        [bool]$ExpectedEnabled
    )
    $env:OOT3D_GRAPHICS_FRAME_RATE = $Mode
    $env:OOT3D_VULKAN_VALIDATION = "1"
    & $harness -SkipBuild -Frames $Frames -MaxSeconds $MaxSeconds |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Mode pacing runtime exited with $LASTEXITCODE"
    }

    $summary =
        Get-Content -LiteralPath $summaryPath -Raw |
        ConvertFrom-Json
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ([uint64]$summary.presentation_frames -ne $Frames -or
        $diagnosticFrames.Count -ne $Frames) {
        throw "$Mode did not produce exactly $Frames presentation frames"
    }
    if ([bool]$summary.realtime_pacing.enabled -ne $ExpectedEnabled) {
        throw "$Mode selected the wrong pacing enabled state"
    }
    $actualRate = $summary.realtime_pacing.target_refresh_hz
    if ($null -eq $ExpectedRate) {
        if ($null -ne $actualRate) {
            throw "$Mode unexpectedly selected a finite refresh rate"
        }
    } elseif ([uint32]$actualRate -ne [uint32]$ExpectedRate) {
        throw "$Mode selected refresh rate $actualRate"
    }

    $frameRate = $summary.frame_rate
    if ([uint32]$frameRate.original_simulation_hz -ne 30U -or
        [uint32]$frameRate.simulation_hz -ne 30U -or
        [uint32]$frameRate.native_update_rate -ne 2U -or
        -not [bool]$frameRate.a32_update_rate_exact -or
        [math]::Abs([double]$frameRate.step_seconds -
                    (1.0 / 30.0)) -gt 1.0e-9) {
        throw "$Mode altered the authoritative 30 Hz guest simulation"
    }

    $vulkanErrors = [uint64](
        $diagnosticFrames |
            Measure-Object -Property vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $diagnosticFrames |
            Measure-Object -Property nri_validation_error_count -Maximum
    ).Maximum
    $nativeDraws = [uint64](
        $diagnosticFrames |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $nriDraws = [uint64](
        $diagnosticFrames |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($vulkanErrors -ne 0U -or $nriErrors -ne 0U) {
        throw "$Mode produced validation errors"
    }
    if ($nativeDraws -eq 0U -or $nativeDraws -ne $nriDraws) {
        throw "$Mode broke native PICA/NRI draw parity"
    }

    [pscustomobject]@{
        Mode = $Mode
        TargetRateHz = $actualRate
        PacingEnabled = [bool]$summary.realtime_pacing.enabled
        PresentationFrames = [uint64]$summary.presentation_frames
        GuestSimulationHz = [uint32]$frameRate.simulation_hz
        NativeDraws = $nativeDraws
    }
}

try {
    if (-not $SkipBuild) {
        & $buildScript -Target oot3d_graphics_foundation_tests `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build pacing contract tests"
        }
        & $foundationTests `
            "--gtest_filter=Oot3dPresentationPacing.*:Oot3dVisualClock.*" |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Presentation pacing contract tests failed"
        }
        & $buildScript -Target oot3d_native_game `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the native game"
        }
    }

    $results = @(
        Invoke-PacingCase -Mode "Original30" `
            -ExpectedRate ([Nullable[uint32]]30U) -ExpectedEnabled $true
        Invoke-PacingCase -Mode "Fixed60" `
            -ExpectedRate ([Nullable[uint32]]60U) -ExpectedEnabled $true
        Invoke-PacingCase -Mode "Uncapped" `
            -ExpectedRate $null -ExpectedEnabled $false
    )
    $results | Format-Table -AutoSize
    [pscustomobject]@{
        Result = "PASS"
        Modes = $results.Count
        FramesPerMode = $Frames
        GuestSimulationHz = 30
    } | Format-List
} finally {
    foreach ($name in $environmentNames) {
        $previous = $previousEnvironment[$name]
        if ($null -eq $previous) {
            Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable(
                $name, [string]$previous, "Process")
        }
    }
}
