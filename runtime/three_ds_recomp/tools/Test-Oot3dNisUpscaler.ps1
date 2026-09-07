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
    "OOT3D_VULKAN_VALIDATION"
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

try {
    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:OOT3D_GRAPHICS_NIS = "1"
    $env:OOT3D_VULKAN_VALIDATION = "1"

    if (-not $SkipBuild) {
        & $buildScript -Target oot3d_graphics_foundation_tests `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the upscaler contract tests"
        }
        & $foundationTests "--gtest_filter=Oot3dUpscaler.*" | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Upscaler contract tests failed"
        }
        & $buildScript -Target oot3d_native_game `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the native game"
        }
    }

    & $harness -SkipBuild -Frames $Frames -MaxSeconds $MaxSeconds |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "NIS runtime exited with $LASTEXITCODE"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "NIS produced $($diagnosticFrames.Count)/$Frames frames"
    }
    $activeFrames = @(
        $diagnosticFrames |
            Where-Object { $_.nis_upscale_pass_count -gt 0 }
    )
    if ($activeFrames.Count -lt ($Frames - 1)) {
        throw "NIS did not dispatch on every rendered frame"
    }
    $last = $activeFrames[-1]
    if ($last.vulkan_validation_error_count -ne 0U -or
        $last.nri_validation_error_count -ne 0U) {
        throw "NIS produced renderer validation errors"
    }
    if (-not $last.nis_nri_dispatched -or
        -not $last.upscaler_output_nri_owned -or
        -not $last.upscaler_barriers_nri_owned) {
        throw "NIS did not retain NRI dispatch/output/barrier ownership"
    }
    if ($last.native_pica_draw_count -eq 0U -or
        $last.nri_pica_owned_draw_count -eq 0U) {
        throw "NIS did not preserve the playable PICA scene"
    }
    if ($last.nis_input_width -ge $last.nis_output_width -or
        $last.nis_input_height -ge $last.nis_output_height) {
        throw "NIS Quality did not use a lower internal resolution"
    }
    $scaleX =
        [double]$last.nis_output_width / [double]$last.nis_input_width
    $scaleY =
        [double]$last.nis_output_height / [double]$last.nis_input_height
    if ([Math]::Abs($scaleX - 1.5) -gt 0.01 -or
        [Math]::Abs($scaleY - 1.5) -gt 0.01) {
        throw "NIS Quality scale is not 1.5x: $scaleX x $scaleY"
    }

    [pscustomobject]@{
        Result = "PASS"
        Frames = $Frames
        Dispatches = $activeFrames.Count
        Input = "$($last.nis_input_width)x$($last.nis_input_height)"
        Output = "$($last.nis_output_width)x$($last.nis_output_height)"
        NriOutput = $last.upscaler_output_nri_owned
        NriBarriers = $last.upscaler_barriers_nri_owned
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
