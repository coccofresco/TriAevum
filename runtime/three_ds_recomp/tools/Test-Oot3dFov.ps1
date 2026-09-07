[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(4, 120)]
    [int]$Frames = 6,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45,
    [ValidateRange(0.001, 1.0)]
    [double]$MinimumChangedPixelFraction = 0.01
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
$diagnosticsPath = Join-Path $artifactRoot "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"
Import-Module (Join-Path $rendererRoot `
    "tools\Oot3dFramebufferArtifact.psm1") -Force

$environmentNames = @(
    "OOT3D_VULKAN_VALIDATION",
    "OOT3D_GRAPHICS_FOV_MULTIPLIER"
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Invoke-FovCase {
    param([double]$Multiplier)
    $env:OOT3D_VULKAN_VALIDATION = "1"
    $env:OOT3D_GRAPHICS_FOV_MULTIPLIER =
        $Multiplier.ToString(
            [Globalization.CultureInfo]::InvariantCulture)
    & $harness -SkipBuild -Frames $Frames -MaxSeconds $MaxSeconds |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "FOV $Multiplier runtime exited with $LASTEXITCODE"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "FOV $Multiplier produced " +
            "$($diagnosticFrames.Count)/$Frames frames"
    }
    $vulkanErrors = [uint64](
        $diagnosticFrames | Measure-Object `
            -Property vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $diagnosticFrames | Measure-Object `
            -Property nri_validation_error_count -Maximum
    ).Maximum
    $draws = [uint64](
        $diagnosticFrames |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $nriDraws = [uint64](
        $diagnosticFrames |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($vulkanErrors -ne 0 -or $nriErrors -ne 0) {
        throw "FOV $Multiplier produced validation errors"
    }
    if ($draws -eq 0 -or $draws -ne $nriDraws) {
        throw "FOV $Multiplier broke native PICA/NRI draw parity"
    }
    $framebuffer = Read-Oot3dFramebufferBmp -Path $screenshotPath
    $metrics = Get-Oot3dFramebufferMetrics -Framebuffer $framebuffer
    if ($metrics.UniqueColors -lt 32 -or
        $metrics.ChannelRange -lt 24) {
        throw "FOV $Multiplier produced a collapsed framebuffer"
    }
    return [pscustomobject]@{
        Multiplier = $Multiplier
        Draws = $draws
        Framebuffer = $framebuffer
    }
}

try {
    if (-not $SkipBuild) {
        & $buildScript -Target oot3d_graphics_foundation_tests `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build FOV contract tests"
        }
        & $foundationTests `
            "--gtest_filter=Oot3dPerspectiveFov.*:Oot3dSceneViewBridge.*" |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "FOV contract tests failed"
        }
        & $buildScript -Target oot3d_native_game `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the native game"
        }
    }
    $baseline = Invoke-FovCase -Multiplier 1.0
    $expanded = Invoke-FovCase -Multiplier 1.5
    if ($baseline.Draws -ne $expanded.Draws) {
        throw "FOV changed native draw coverage"
    }
    $difference = Compare-Oot3dFramebufferBmp `
        -Reference $baseline.Framebuffer `
        -Candidate $expanded.Framebuffer `
        -ChangedPixelThreshold 2
    if ($difference.ChangedPixelFraction -lt
        $MinimumChangedPixelFraction) {
        throw "FOV 1.5 did not visibly change the perspective: " +
            "$($difference.ChangedPixelFraction)"
    }
    [pscustomobject]@{
        Result = "PASS"
        FramesPerCase = $Frames
        DrawsPerCase = $baseline.Draws
        ChangedPixelFraction = $difference.ChangedPixelFraction
        MeanAbsoluteChannelDelta =
            $difference.MeanAbsoluteChannelDelta
        MaximumChannelDelta = $difference.MaximumChannelDelta
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
