[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(6, 240)]
    [int]$BaselineFrames = 8,
    [ValidateRange(8, 240)]
    [int]$AdvancedFrames = 12,
    [ValidateRange(6, 240)]
    [int]$UpscalerFrames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot "tools\Invoke-Oot3dRendererDev.ps1"
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$diagnosticsPath = Join-Path $artifactRoot "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"
$profileEnvironment = @(
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_HIZ",
    "OOT3D_GRAPHICS_SSSR",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_GRASS_AUTO",
    "OOT3D_GRAPHICS_TOON_MATERIAL",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS"
)

function Get-TimingAverage {
    param(
        [object[]]$Frames,
        [uint64]$MaximumSettledFrame,
        [string]$CounterProperty,
        [string]$TimingProperty,
        [string]$Label
    )

    $eligible = @(
        $Frames |
            Where-Object {
                [uint64]$_.frame_index -le $MaximumSettledFrame -and
                [uint64]$_.$CounterProperty -gt 0
            }
    )
    if ($eligible.Count -eq 0) {
        throw "$Label did not execute in a settled diagnostic frame"
    }
    $missing = @(
        $eligible |
            Where-Object {
                $null -eq $_.gpu.$TimingProperty -or
                [double]$_.gpu.$TimingProperty -lt 0.0
            }
    )
    if ($missing.Count -ne 0) {
        throw "$Label is missing $TimingProperty for " +
            "$($missing.Count) settled frames"
    }
    return [double](
        $eligible |
            ForEach-Object { [double]$_.gpu.$TimingProperty } |
            Measure-Object -Average
    ).Average
}

function Get-FrameTimingAverage {
    param(
        [object[]]$Frames,
        [uint64]$MaximumSettledFrame,
        [string]$Label
    )

    $settled = @(
        $Frames |
            Where-Object {
                [uint64]$_.frame_index -le $MaximumSettledFrame
            }
    )
    if ($settled.Count -eq 0 -or
        @(
            $settled |
                Where-Object {
                    $null -eq $_.gpu.frame_ms -or
                    [double]$_.gpu.frame_ms -lt 0.0
                }
        ).Count -ne 0) {
        throw "$Label is missing settled frame GPU timings"
    }
    return [double](
        $settled |
            ForEach-Object { [double]$_.gpu.frame_ms } |
            Measure-Object -Average
    ).Average
}

function Invoke-ProfileCase {
    param(
        [ValidateSet("Baseline", "Advanced", "Upscaler")]
        [string]$Mode,
        [int]$Frames,
        [bool]$Build
    )

    foreach ($name in $profileEnvironment) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    if ($Mode -eq "Advanced") {
        $env:OOT3D_GRAPHICS_CACAO = "1"
        $env:OOT3D_GRAPHICS_HIZ = "1"
        $env:OOT3D_GRAPHICS_TAA = "1"
        $env:OOT3D_GRAPHICS_GRASS_AUTO = "1"
        $env:OOT3D_GRAPHICS_TOON_MATERIAL = "1"
    } elseif ($Mode -eq "Upscaler") {
        $env:OOT3D_GRAPHICS_NIS = "1"
    }

    $arguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
    }
    if (-not $Build) {
        $arguments.SkipBuild = $true
    }
    & $harness @arguments | Out-Host
    $rendererExitCode = $LASTEXITCODE
    if ($rendererExitCode -ne 0) {
        throw "$Mode profiling checkpoint exited with code " +
            "$rendererExitCode"
    }
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
        throw "$Mode profiling checkpoint did not produce artifacts"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw | ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $Frames) {
        throw "$Mode profiling checkpoint produced " +
            "$($frameDiagnostics.Count) of $Frames frames"
    }
    $lastFrame = [uint64]$frameDiagnostics[-1].frame_index
    if ($lastFrame -lt 3U) {
        throw "$Mode profiling checkpoint has no settled frames"
    }
    $maximumSettledFrame = $lastFrame - 2U

    $drawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $nriDrawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($drawCount -eq 0 -or $drawCount -ne $nriDrawCount) {
        throw "$Mode profiling checkpoint broke PICA/NRI draw parity"
    }

    $metrics = [ordered]@{
        Mode = $Mode
        Frames = $Frames
        DrawCount = $drawCount
        FrameMs = Get-FrameTimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -Label $Mode
        NativePicaMs = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "native_pica_draw_count" `
            -TimingProperty "native_pica_ms" `
            -Label "$Mode native PICA"
        DisplayTransferMs = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "display_transfer_count" `
            -TimingProperty "display_transfer_ms" `
            -Label "$Mode display transfer"
        ScanoutMs = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "scanout_count" `
            -TimingProperty "scanout_ms" `
            -Label "$Mode scanout"
        OverlayMs = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "overlay_count" `
            -TimingProperty "overlay_ms" `
            -Label "$Mode UI overlay"
        ScreenshotSha256 = (
            Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256
        ).Hash
    }

    if ($Mode -eq "Advanced") {
        $metrics["GrassMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "grass_motion_blade_count" `
            -TimingProperty "grass_ms" `
            -Label "interactive grass"
        $metrics["ToonRasterMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "toon_draw_count" `
            -TimingProperty "toon_raster_ms" `
            -Label "PICA toon raster"
        $metrics["ToonDrawCount"] = [uint64](
            $frameDiagnostics |
                Measure-Object -Property toon_draw_count -Sum
        ).Sum
        $metrics["PicaMaterialToonDrawCount"] = [uint64](
            $frameDiagnostics |
                Measure-Object `
                    -Property pica_material_toon_draw_count -Sum
        ).Sum
        $metrics["CacaoMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "cacao_pass_count" `
            -TimingProperty "cacao_ms" `
            -Label "CACAO"
        $metrics["DepthPreparationMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "hiz_pass_count" `
            -TimingProperty "depth_preparation_ms" `
            -Label "Hi-Z depth preparation"
        $metrics["ReflectionMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "hiz_reflection_count" `
            -TimingProperty "reflection_ms" `
            -Label "Hi-Z reflections"
        $metrics["MotionVectorsMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "motion_vector_pass_count" `
            -TimingProperty "motion_vectors_ms" `
            -Label "motion vectors"
        $metrics["SceneCompositeMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "scene_composite_pass_count" `
            -TimingProperty "scene_composite_ms" `
            -Label "scene composite"
        $metrics["AntiAliasingMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "temporal_aa_pass_count" `
            -TimingProperty "anti_aliasing_ms" `
            -Label "temporal anti-aliasing"
    } elseif ($Mode -eq "Upscaler") {
        $metrics["UpscalerMs"] = Get-TimingAverage `
            -Frames $frameDiagnostics `
            -MaximumSettledFrame $maximumSettledFrame `
            -CounterProperty "nis_upscale_pass_count" `
            -TimingProperty "upscaler_ms" `
            -Label "NRI NIS upscaler"
    }

    return [pscustomobject]$metrics
}

$previousEnvironment = @{}
foreach ($name in $profileEnvironment) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}
try {
    $baseline = Invoke-ProfileCase `
        -Mode "Baseline" -Frames $BaselineFrames `
        -Build (-not $SkipBuild)
    $advanced = Invoke-ProfileCase `
        -Mode "Advanced" -Frames $AdvancedFrames -Build $false
    $upscaler = Invoke-ProfileCase `
        -Mode "Upscaler" -Frames $UpscalerFrames -Build $false

    [pscustomobject]@{
        Result = "PASS"
        BaselineFrames = $baseline.Frames
        AdvancedFrames = $advanced.Frames
        UpscalerFrames = $upscaler.Frames
        BaselineFrameMs = $baseline.FrameMs
        AdvancedFrameMs = $advanced.FrameMs
        NativePicaMs = $advanced.NativePicaMs
        GrassMs = $advanced.GrassMs
        ToonRasterMs = $advanced.ToonRasterMs
        ToonDrawCount = $advanced.ToonDrawCount
        PicaMaterialToonDrawCount =
            $advanced.PicaMaterialToonDrawCount
        CacaoMs = $advanced.CacaoMs
        DepthPreparationMs = $advanced.DepthPreparationMs
        ReflectionMs = $advanced.ReflectionMs
        MotionVectorsMs = $advanced.MotionVectorsMs
        SceneCompositeMs = $advanced.SceneCompositeMs
        AntiAliasingMs = $advanced.AntiAliasingMs
        UpscalerMs = $upscaler.UpscalerMs
        DisplayTransferMs = $advanced.DisplayTransferMs
        ScanoutMs = $advanced.ScanoutMs
        OverlayMs = $advanced.OverlayMs
    } | Format-List
} finally {
    foreach ($name in $profileEnvironment) {
        $previous = $previousEnvironment[$name]
        if ($null -eq $previous) {
            Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable(
                $name, [string]$previous, "Process")
        }
    }
}
