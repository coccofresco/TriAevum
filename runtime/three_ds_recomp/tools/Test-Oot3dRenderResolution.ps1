[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(24, 120)]
    [int]$Frames = 32,
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
    "OOT3D_GRAPHICS_RENDER_SCALE",
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_CACAO_QUALITY",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_VULKAN_VALIDATION"
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

try {
    if (-not $SkipBuild) {
        & $buildScript -Target oot3d_graphics_foundation_tests `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build render-resolution contract tests"
        }
        & $foundationTests `
            "--gtest_filter=Oot3dRenderResolution.*:Oot3dTemporalHistory.*" |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Render-resolution contract tests failed"
        }
        & $buildScript -Target oot3d_native_game `
            -BuildDirectory $buildDirectory -Parallel 2 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the native game"
        }
    }

    $env:OOT3D_GRAPHICS_RENDER_SCALE = "1.0"
    $env:OOT3D_GRAPHICS_CACAO = "1"
    $env:OOT3D_GRAPHICS_CACAO_QUALITY = "1"
    $env:OOT3D_GRAPHICS_TAA = "1"
    $env:OOT3D_VULKAN_VALIDATION = "1"
    & $harness -SkipBuild -RenderScaleTransitionSmoke `
        -Frames $Frames -MaxSeconds $MaxSeconds | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Render-resolution transition exited with $LASTEXITCODE"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "Resolution transition produced " +
            "$($diagnosticFrames.Count)/$Frames frames"
    }
    $activeFrames = @(
        $diagnosticFrames |
            Where-Object {
                [uint64]$_.native_pica_draw_count -gt 0U
            }
    )
    if ($activeFrames.Count -lt ($Frames - 2)) {
        throw "Resolution transition lost too many playable frames"
    }
    $invalid = @(
        $activeFrames |
            Where-Object {
                [uint64]$_.native_pica_draw_count -ne
                    [uint64]$_.nri_pica_owned_draw_count -or
                [uint64]$_.vulkan_validation_error_count -ne 0U -or
                [uint64]$_.nri_validation_error_count -ne 0U
            }
    )
    if ($invalid.Count -ne 0) {
        throw "Resolution transition broke draw parity or validation"
    }

    $baseline = @(
        $activeFrames |
            Where-Object {
                [double]$_.internal_resolution_scale -lt 1.01
            } |
            Select-Object -Last 1
    )
    $expanded = @(
        $activeFrames |
            Where-Object {
                [double]$_.internal_resolution_scale -gt 1.49
            } |
            Select-Object -First 1
    )
    if ($baseline.Count -ne 1 -or $expanded.Count -ne 1) {
        throw "Resolution transition did not expose both 1.0 and 1.5 states"
    }
    if ([uint32]$baseline[0].output_width -ne
            [uint32]$expanded[0].output_width -or
        [uint32]$baseline[0].output_height -ne
            [uint32]$expanded[0].output_height) {
        throw "Internal scale unexpectedly changed output/UI resolution"
    }
    $widthRatio =
        [double]$expanded[0].internal_target_width /
        [double]$baseline[0].internal_target_width
    $heightRatio =
        [double]$expanded[0].internal_target_height /
        [double]$baseline[0].internal_target_height
    if ([math]::Abs($widthRatio - 1.5) -gt 0.001 -or
        [math]::Abs($heightRatio - 1.5) -gt 0.001) {
        throw "Internal target did not scale exactly by 1.5"
    }
    $advancedBefore = @(
        $activeFrames |
            Where-Object {
                [double]$_.internal_resolution_scale -lt 1.01 -and
                [uint64]$_.cacao_pass_count -gt 0U -and
                [uint64]$_.temporal_aa_pass_count -gt 0U
            }
    )
    $advancedAfter = @(
        $activeFrames |
            Where-Object {
                [double]$_.internal_resolution_scale -gt 1.49 -and
                [uint64]$_.cacao_pass_count -gt 0U -and
                [uint64]$_.temporal_aa_pass_count -gt 0U
            }
    )
    if ($advancedBefore.Count -eq 0 -or $advancedAfter.Count -eq 0) {
        throw "CACAO/TAA did not resume across the resolution transition"
    }
    $resolutionResets = @(
        $advancedAfter |
            Where-Object {
                [uint32]$_.temporal_reset_reason -eq 1U -and
                [uint32]$_.temporal_aa_history_valid_count -eq 0U
            }
    )
    if ($resolutionResets.Count -eq 0) {
        throw "TAA history was not reset for the resolution transition"
    }
    $recoveredHistory = @(
        $advancedAfter |
            Where-Object {
                [uint32]$_.temporal_aa_history_valid_count -gt 0U
            }
    )
    if ($recoveredHistory.Count -eq 0) {
        throw "TAA history did not recover after the resolution transition"
    }

    [pscustomobject]@{
        Result = "PASS"
        Frames = $Frames
        OutputExtent =
            "$($baseline[0].output_width)x$($baseline[0].output_height)"
        BaselineInternalExtent =
            "$($baseline[0].internal_target_width)x" +
            "$($baseline[0].internal_target_height)"
        ExpandedInternalExtent =
            "$($expanded[0].internal_target_width)x" +
            "$($expanded[0].internal_target_height)"
        ScaleRatio = $widthRatio
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
