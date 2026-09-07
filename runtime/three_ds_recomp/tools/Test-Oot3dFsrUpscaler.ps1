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
    $env:OOT3D_GRAPHICS_FSR = "1"
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
        throw "FSR runtime exited with $LASTEXITCODE"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "FSR produced $($diagnosticFrames.Count)/$Frames frames"
    }
    $activeFrames = @(
        $diagnosticFrames |
            Where-Object { $_.fsr_upscale_pass_count -gt 0 }
    )
    if ($activeFrames.Count -lt ($Frames - 2)) {
        throw "FSR did not dispatch after temporal bootstrap"
    }
    $last = $activeFrames[-1]
    if ($last.vulkan_validation_error_count -ne 0U -or
        $last.nri_validation_error_count -ne 0U) {
        throw "FSR produced renderer validation errors"
    }
    if (-not $last.fsr_nri_dispatched -or
        -not $last.upscaler_output_nri_owned -or
        -not $last.upscaler_barriers_nri_owned) {
        throw "FSR did not retain NRI dispatch/output/barrier ownership"
    }
    $motionPasses =
        ($diagnosticFrames |
            Measure-Object -Property motion_vector_pass_count -Sum).Sum
    $resetCount =
        ($diagnosticFrames |
            Measure-Object -Property fsr_history_reset_count -Sum).Sum
    if ($motionPasses -lt $activeFrames.Count -or $resetCount -lt 1U) {
        throw "FSR did not consume motion or reset its temporal history"
    }
    if ($last.temporal_prepare_count -eq 0U -or
        $last.temporal_valid_count -eq 0U) {
        throw "FSR did not prepare a valid temporal view"
    }
    if ($last.native_pica_draw_count -eq 0U -or
        $last.nri_pica_owned_draw_count -eq 0U) {
        throw "FSR did not preserve the playable PICA scene"
    }
    if ($last.fsr_input_width -ge $last.fsr_output_width -or
        $last.fsr_input_height -ge $last.fsr_output_height) {
        throw "FSR Quality did not use a lower internal resolution"
    }
    $scaleX =
        [double]$last.fsr_output_width / [double]$last.fsr_input_width
    $scaleY =
        [double]$last.fsr_output_height / [double]$last.fsr_input_height
    if ([Math]::Abs($scaleX - 1.5) -gt 0.01 -or
        [Math]::Abs($scaleY - 1.5) -gt 0.01) {
        throw "FSR Quality scale is not 1.5x: $scaleX x $scaleY"
    }

    [pscustomobject]@{
        Result = "PASS"
        Frames = $Frames
        Dispatches = $activeFrames.Count
        MotionPasses = $motionPasses
        HistoryResets = $resetCount
        Input = "$($last.fsr_input_width)x$($last.fsr_input_height)"
        Output = "$($last.fsr_output_width)x$($last.fsr_output_height)"
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
