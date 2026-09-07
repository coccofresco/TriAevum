[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(8, 60)]
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
$diagnosticsPath = Join-Path $rendererRoot `
    ".renderer-dev\artifacts\vulkan-diagnostics.json"

$environmentNames = @(
    "OOT3D_GRAPHICS_FXAA",
    "OOT3D_GRAPHICS_SMAA",
    "OOT3D_GRAPHICS_MSAA",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS",
    "OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING",
    "OOT3D_VULKAN_VALIDATION"
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Invoke-AntiAliasingCase {
    param(
        [ValidateSet("FXAA", "SMAA", "MSAA2", "MSAA4", "MSAA8",
                     "MSAA4VulkanFallback", "TAA")]
        [string]$Mode
    )
    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:OOT3D_VULKAN_VALIDATION = "1"
    switch ($Mode) {
        "FXAA" { $env:OOT3D_GRAPHICS_FXAA = "1" }
        "SMAA" { $env:OOT3D_GRAPHICS_SMAA = "1" }
        "MSAA2" { $env:OOT3D_GRAPHICS_MSAA = "2" }
        "MSAA4" { $env:OOT3D_GRAPHICS_MSAA = "4" }
        "MSAA8" { $env:OOT3D_GRAPHICS_MSAA = "8" }
        "MSAA4VulkanFallback" {
            $env:OOT3D_GRAPHICS_MSAA = "4"
            $env:OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING = "0"
        }
        "TAA" { $env:OOT3D_GRAPHICS_TAA = "1" }
    }
    & $harness -SkipBuild -Frames $Frames -MaxSeconds $MaxSeconds |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Mode runtime exited with $LASTEXITCODE"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "$Mode produced $($diagnosticFrames.Count)/$Frames frames"
    }
    $maximum = {
        param([string]$Property)
        [uint64](
            $diagnosticFrames |
                Measure-Object -Property $Property -Maximum
        ).Maximum
    }
    $sum = {
        param([string]$Property)
        [uint64](
            $diagnosticFrames |
                Measure-Object -Property $Property -Sum
        ).Sum
    }
    $nativeDraws = & $sum "native_pica_draw_count"
    $nriDraws = & $sum "nri_pica_owned_draw_count"
    $nriScopeFrames = @(
        $diagnosticFrames |
            Where-Object { $_.nri_pica_rendering_scope_owned }
    ).Count
    if ((& $maximum "vulkan_validation_error_count") -ne 0U -or
        (& $maximum "nri_validation_error_count") -ne 0U) {
        throw "$Mode produced validation errors"
    }
    if ($nativeDraws -eq 0U) {
        throw "$Mode produced no native PICA draws"
    }
    if ($Mode -eq "MSAA4VulkanFallback") {
        if ($nriDraws -ne 0U -or $nriScopeFrames -ne 0U) {
            throw "$Mode did not isolate the Vulkan raster fallback"
        }
    } else {
        $ownershipMismatches = @(
            $diagnosticFrames |
                Where-Object {
                    [uint64]$_.native_pica_draw_count -ne 0U -and
                    [uint64]$_.nri_pica_owned_draw_count -ne
                        [uint64]$_.native_pica_draw_count
                }
        )
        if ($ownershipMismatches.Count -ne 0 -or
            $nriDraws -ne $nativeDraws -or
            $nriScopeFrames -eq 0U) {
            throw "$Mode did not keep exact per-frame NRI draw ownership"
        }
    }

    $spatial = & $maximum "spatial_aa_mode"
    $samples = & $maximum "msaa_samples"
    $smaa = & $sum "smaa_1x_pass_count"
    $taa = & $sum "temporal_aa_pass_count"
    $jitter = & $sum "temporal_jitter_draw_count"
    $nis = & $sum "nis_upscale_pass_count"
    $alphaTestDraws = & $sum "native_pica_alpha_test_draw_count"
    $alphaCoverageDraws =
        & $sum "native_pica_alpha_to_coverage_draw_count"
    if ($alphaCoverageDraws -gt $alphaTestDraws) {
        throw "$Mode reported alpha-to-coverage on non-alpha-tested draws"
    }
    switch ($Mode) {
        "FXAA" {
            if ($spatial -ne 1U -or $samples -ne 1U -or
                $smaa -ne 0U -or $taa -ne 0U -or $nis -ne 0U) {
                throw "FXAA fell back or activated another AA path"
            }
        }
        "SMAA" {
            if ($spatial -ne 2U -or $smaa -eq 0U -or
                $samples -ne 1U -or $taa -ne 0U -or $nis -ne 0U) {
                throw "SMAA fell back or activated another AA path"
            }
        }
        { $_ -in "MSAA2", "MSAA4", "MSAA8",
                   "MSAA4VulkanFallback" } {
            $expectedSamples = switch ($Mode) {
                "MSAA2" { 2U }
                "MSAA8" { 8U }
                default { 4U }
            }
            if ($samples -ne $expectedSamples -or $spatial -ne 0U -or
                $smaa -ne 0U -or $taa -ne 0U -or $nis -ne 0U) {
                throw "$Mode fell back or activated another AA path"
            }
            if ($alphaTestDraws -eq 0U -or $alphaCoverageDraws -eq 0U) {
                throw "$Mode did not cover alpha-tested PICA geometry"
            }
        }
        "TAA" {
            if ($taa -eq 0U -or $jitter -eq 0U -or
                $spatial -ne 0U -or $samples -ne 1U -or
                $smaa -ne 0U -or $nis -ne 0U) {
                throw "TAA fell back or activated another AA path"
            }
        }
    }
    if ($Mode -notin "MSAA2", "MSAA4", "MSAA8",
                     "MSAA4VulkanFallback" -and
        $alphaCoverageDraws -ne 0U) {
        throw "$Mode enabled alpha-to-coverage without multisampling"
    }
    [pscustomobject]@{
        Mode = $Mode
        Draws = $nativeDraws
        NriDraws = $nriDraws
        NriScopeFrames = $nriScopeFrames
        SpatialMode = $spatial
        Samples = $samples
        SmaaPasses = $smaa
        TaaPasses = $taa
        NisPasses = $nis
        AlphaTestDraws = $alphaTestDraws
        AlphaCoverageDraws = $alphaCoverageDraws
    }
}

try {
    if (-not $SkipBuild) {
        & $buildScript -Target oot3d_graphics_foundation_tests `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build AA policy tests"
        }
        & $foundationTests `
            "--gtest_filter=Oot3dAntiAliasingFramePolicy.*:Oot3dPicaAlphaCoveragePolicy.*:Oot3dDisplayEffectPlan.*" |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "AA policy tests failed"
        }
        & $buildScript -Target oot3d_native_game `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the native game"
        }
    }

    $results = @(
        Invoke-AntiAliasingCase -Mode "FXAA"
        Invoke-AntiAliasingCase -Mode "SMAA"
        Invoke-AntiAliasingCase -Mode "MSAA2"
        Invoke-AntiAliasingCase -Mode "MSAA4"
        Invoke-AntiAliasingCase -Mode "MSAA8"
        Invoke-AntiAliasingCase -Mode "MSAA4VulkanFallback"
        Invoke-AntiAliasingCase -Mode "TAA"
    )
    $results | Format-Table -AutoSize
    [pscustomobject]@{
        Result = "PASS"
        Modes = $results.Count
        FramesPerMode = $Frames
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
