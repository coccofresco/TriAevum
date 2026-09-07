[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(28, 60)]
    [int]$Frames = 28,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 60
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

function Invoke-UpscalerTransition {
    param(
        [ValidateSet("NIS", "FSR", "DLSS")]
        [string]$Provider
    )

    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    Set-Item "Env:OOT3D_GRAPHICS_$Provider" "1"
    $env:OOT3D_VULKAN_VALIDATION = "1"

    & $harness -SkipBuild -Frames $Frames -MaxSeconds $MaxSeconds `
        -UpscalerQualityTransitionSmoke | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Provider quality transition exited with $LASTEXITCODE"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "$Provider produced $($diagnosticFrames.Count)/$Frames frames"
    }

    $prefix = $Provider.ToLowerInvariant()
    $passProperty = "${prefix}_upscale_pass_count"
    $inputWidthProperty = "${prefix}_input_width"
    $inputHeightProperty = "${prefix}_input_height"
    $outputWidthProperty = "${prefix}_output_width"
    $outputHeightProperty = "${prefix}_output_height"
    $resetProperty = "${prefix}_history_reset_count"
    $activeFrames = @(
        $diagnosticFrames |
            Where-Object { $_.$passProperty -gt 0 }
    )
    $vendor = [uint32]$diagnosticFrames[-1].vulkan_adapter_vendor_id
    if ($Provider -eq "DLSS" -and $activeFrames.Count -eq 0 -and
        $vendor -ne 0x10DEU) {
        return [pscustomobject]@{
            Provider = $Provider
            Result = "SKIP"
            QualityInput = "-"
            PerformanceInput = "-"
            Output = "-"
            Resets = 0
        }
    }
    if ($activeFrames.Count -lt ($Frames - 3)) {
        throw "$Provider did not survive the quality transition"
    }

    $qualityFrames = @(
        $activeFrames |
            Where-Object {
                $_.frame_index -ge 10 -and $_.frame_index -le 20
            }
    )
    $performanceFrames = @(
        $activeFrames |
            Where-Object { $_.frame_index -ge 21 }
    )
    if ($qualityFrames.Count -eq 0 -or $performanceFrames.Count -eq 0) {
        throw "$Provider did not expose both quality phases"
    }
    $quality = $qualityFrames[-1]
    $performance = $performanceFrames[0]
    if ([Math]::Abs(
            [double]$quality.internal_resolution_scale - (2.0 / 3.0)) -gt
        0.01 -or
        [Math]::Abs(
            [double]$performance.internal_resolution_scale - 0.5) -gt
        0.01) {
        throw "$Provider did not apply Quality -> Performance scales"
    }
    if ($performance.$inputWidthProperty -ge
            $quality.$inputWidthProperty -or
        $performance.$inputHeightProperty -ge
            $quality.$inputHeightProperty) {
        throw "$Provider did not recreate its lower-resolution inputs"
    }
    if ($performance.$outputWidthProperty -ne
            $quality.$outputWidthProperty -or
        $performance.$outputHeightProperty -ne
            $quality.$outputHeightProperty) {
        throw "$Provider changed the requested output extent"
    }
    $last = $activeFrames[-1]
    if ($last.vulkan_validation_error_count -ne 0U -or
        $last.nri_validation_error_count -ne 0U) {
        throw "$Provider transition produced validation errors"
    }
    foreach ($frame in $performanceFrames) {
        if (-not $frame.upscaler_output_nri_owned -or
            -not $frame.upscaler_barriers_nri_owned -or
            $frame.native_pica_draw_count -eq 0U) {
            throw "$Provider lost ownership or the playable scene"
        }
    }
    $resets = if ($Provider -eq "NIS") {
        0
    } else {
        ($performanceFrames |
            Measure-Object -Property $resetProperty -Sum).Sum
    }
    if ($Provider -ne "NIS" -and $resets -lt 1U) {
        throw "$Provider did not reset temporal history on quality change"
    }

    return [pscustomobject]@{
        Provider = $Provider
        Result = "PASS"
        QualityInput =
            "$($quality.$inputWidthProperty)x$($quality.$inputHeightProperty)"
        PerformanceInput =
            "$($performance.$inputWidthProperty)x$($performance.$inputHeightProperty)"
        Output =
            "$($performance.$outputWidthProperty)x$($performance.$outputHeightProperty)"
        Resets = $resets
    }
}

try {
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

    $results = @(
        Invoke-UpscalerTransition -Provider "NIS"
        Invoke-UpscalerTransition -Provider "FSR"
        Invoke-UpscalerTransition -Provider "DLSS"
    )
    $results | Format-Table -AutoSize
    [pscustomobject]@{
        Result = "PASS"
        Providers = $results.Count
        FramesPerProvider = $Frames
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
