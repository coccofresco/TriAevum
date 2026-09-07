[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipBorderless,
    [ValidateRange(2, 240)]
    [int]$Frames = 7,
    [ValidateRange(5, 240)]
    [int]$BorderlessFrames = 12,
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

function Invoke-RendererCheckpoint {
    param(
        [bool]$UseNri,
        [int]$FrameCount,
        [bool]$Build,
        [bool]$Borderless
    )

    if ($UseNri) {
        Remove-Item Env:OOT3D_GRAPHICS_NRI_SWAPCHAIN `
            -ErrorAction SilentlyContinue
    } else {
        $env:OOT3D_GRAPHICS_NRI_SWAPCHAIN = "0"
    }

    $arguments = @{
        Frames = $FrameCount
        MaxSeconds = $MaxSeconds
    }
    if (-not $Build) {
        $arguments.SkipBuild = $true
    }
    if ($Borderless) {
        $arguments.BorderlessTransitionSmoke = $true
    }

    & $harness @arguments | Out-Host
    $rendererExitCode = $LASTEXITCODE
    if ($rendererExitCode -ne 0) {
        throw "Renderer checkpoint exited with code $rendererExitCode"
    }
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf)) {
        throw "Renderer checkpoint did not produce Vulkan diagnostics"
    }
    if (-not (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
        throw "Renderer checkpoint did not produce a screenshot"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw | ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $FrameCount) {
        throw "Expected $FrameCount diagnostic frames, got " +
            "$($frameDiagnostics.Count)"
    }
    $fallbackReasons = @(
        $frameDiagnostics |
            Select-Object -ExpandProperty `
                nri_swapchain_fallback_reason -Unique
    )
    $fallbackDetails = @(
        $frameDiagnostics |
            Select-Object -ExpandProperty `
                nri_swapchain_fallback_detail -Unique
    )
    if ($fallbackReasons.Count -ne 1 -or
        $fallbackDetails.Count -ne 1) {
        throw "NRI/Vulkan presentation cause changed between frames"
    }

    [pscustomobject]@{
        FrameCount = $frameDiagnostics.Count
        AcquireCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_swapchain_acquire_count -Sum
        ).Sum
        PresentCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_swapchain_present_count -Sum
        ).Sum
        DrawCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property native_pica_draw_count -Sum
        ).Sum
        NriDrawCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_pica_owned_draw_count -Sum
        ).Sum
        DisplayCopyCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_pica_display_copy_count -Sum
        ).Sum
        ScanoutCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_scanout_count -Sum
        ).Sum
        AllNriOwned = -not @(
            $frameDiagnostics |
                Where-Object { -not $_.nri_swapchain_owned }
        ).Count
        AllSynchronizationOwned = -not @(
            $frameDiagnostics |
                Where-Object {
                    -not $_.nri_swapchain_synchronization_owned
                }
        ).Count
        AllWorkersBypassed = -not @(
            $frameDiagnostics |
                Where-Object { -not $_.nri_present_worker_bypassed }
        ).Count
        AllImageCountsValid = -not @(
            $frameDiagnostics |
                Where-Object { $_.nri_swapchain_image_count -lt 2 }
        ).Count
        AnyNriOwned = @(
            $frameDiagnostics |
                Where-Object { $_.nri_swapchain_owned }
        ).Count -gt 0
        FallbackReason = [uint32]$fallbackReasons[0]
        FallbackDetail = [string]$fallbackDetails[0]
        ScreenshotSha256 = (
            Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256
        ).Hash
    }
}

function Assert-CommonRendering {
    param(
        [pscustomobject]$Metrics,
        [string]$Label
    )

    if ($Metrics.DrawCount -eq 0 -or
        $Metrics.DrawCount -ne $Metrics.NriDrawCount) {
        throw "$Label did not preserve NRI-owned PICA draw parity"
    }
    if ($Metrics.DisplayCopyCount -eq 0 -or
        $Metrics.ScanoutCount -eq 0) {
        throw "$Label did not exercise display copy and scanout"
    }
}

$previousSwapchain =
    [Environment]::GetEnvironmentVariable(
        "OOT3D_GRAPHICS_NRI_SWAPCHAIN", "Process")
try {
    $nri = Invoke-RendererCheckpoint `
        -UseNri $true `
        -FrameCount $Frames `
        -Build (-not $SkipBuild) `
        -Borderless $false
    Assert-CommonRendering -Metrics $nri -Label "NRI swapchain"
    if ($nri.AcquireCount -ne $Frames -or
        $nri.PresentCount -ne $Frames -or
        -not $nri.AllNriOwned -or
        -not $nri.AllSynchronizationOwned -or
        -not $nri.AllWorkersBypassed -or
        -not $nri.AllImageCountsValid -or
        $nri.FallbackReason -ne 0 -or
        -not [string]::IsNullOrEmpty($nri.FallbackDetail)) {
        throw "NRI swapchain ownership contract is incomplete"
    }

    $fallback = Invoke-RendererCheckpoint `
        -UseNri $false `
        -FrameCount $Frames `
        -Build $false `
        -Borderless $false
    Assert-CommonRendering -Metrics $fallback -Label "Vulkan fallback"
    if ($fallback.AcquireCount -ne 0 -or
        $fallback.PresentCount -ne 0 -or
        $fallback.AnyNriOwned -or
        $fallback.FallbackReason -eq 0 -or
        [string]::IsNullOrWhiteSpace($fallback.FallbackDetail)) {
        throw "Forced Vulkan fallback still reported NRI swapchain ownership"
    }
    if ($fallback.DrawCount -ne $nri.DrawCount -or
        $fallback.DisplayCopyCount -ne $nri.DisplayCopyCount -or
        $fallback.ScanoutCount -ne $nri.ScanoutCount -or
        $fallback.ScreenshotSha256 -ne $nri.ScreenshotSha256) {
        throw "NRI and Vulkan swapchains are not A/B equivalent"
    }

    $borderless = $null
    if (-not $SkipBorderless) {
        $borderless = Invoke-RendererCheckpoint `
            -UseNri $true `
            -FrameCount $BorderlessFrames `
            -Build $false `
            -Borderless $true
        Assert-CommonRendering `
            -Metrics $borderless `
            -Label "NRI borderless transition"
        if ($borderless.AcquireCount -ne $BorderlessFrames -or
            $borderless.PresentCount -ne $BorderlessFrames -or
            -not $borderless.AllNriOwned -or
            -not $borderless.AllSynchronizationOwned -or
            -not $borderless.AllWorkersBypassed -or
            -not $borderless.AllImageCountsValid -or
            $borderless.FallbackReason -ne 0 -or
            -not [string]::IsNullOrEmpty(
                $borderless.FallbackDetail)) {
            throw "NRI borderless transition broke swapchain ownership"
        }
    }

    [pscustomobject]@{
        Result = "PASS"
        NriFrames = $nri.FrameCount
        VulkanFallbackFrames = $fallback.FrameCount
        BorderlessFrames = if ($null -eq $borderless) {
            0
        } else {
            $borderless.FrameCount
        }
        DrawCount = $nri.DrawCount
        DisplayCopyCount = $nri.DisplayCopyCount
        ScanoutCount = $nri.ScanoutCount
        ScreenshotSha256 = $nri.ScreenshotSha256
    } | Format-List
} finally {
    if ($null -eq $previousSwapchain) {
        Remove-Item Env:OOT3D_GRAPHICS_NRI_SWAPCHAIN `
            -ErrorAction SilentlyContinue
    } else {
        $env:OOT3D_GRAPHICS_NRI_SWAPCHAIN = $previousSwapchain
    }
}
