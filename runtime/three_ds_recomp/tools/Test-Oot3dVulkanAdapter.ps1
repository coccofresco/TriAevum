[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(2, 240)]
    [int]$Frames = 7,
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

function Get-StableFrameValue {
    param(
        [object[]]$FrameDiagnostics,
        [string]$Property,
        [string]$Label
    )

    $values = @(
        $FrameDiagnostics |
            ForEach-Object { $_.$Property } |
            Select-Object -Unique
    )
    if ($values.Count -ne 1) {
        throw "$Label changed $Property between frames"
    }
    return $values[0]
}

function Invoke-AdapterCheckpoint {
    param(
        [string]$Label,
        [bool]$Build,
        [AllowEmptyString()]
        [string]$AdapterIndex
    )

    if ([string]::IsNullOrEmpty($AdapterIndex)) {
        Remove-Item Env:OOT3D_GRAPHICS_VULKAN_ADAPTER `
            -ErrorAction SilentlyContinue
    } else {
        $env:OOT3D_GRAPHICS_VULKAN_ADAPTER = $AdapterIndex
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
        throw "$Label exited with code $rendererExitCode"
    }
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
        throw "$Label did not produce complete renderer artifacts"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw | ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $Frames) {
        throw "$Label produced $($frameDiagnostics.Count) of $Frames frames"
    }

    $requiredProperties = @(
        "vulkan_adapter_count",
        "vulkan_adapter_index",
        "vulkan_adapter_vendor_id",
        "vulkan_adapter_device_id",
        "vulkan_graphics_queue_family",
        "vulkan_present_queue_family",
        "vulkan_graphics_queue_count",
        "vulkan_requested_graphics_queue_count",
        "vulkan_present_queue_index",
        "vulkan_async_present_supported",
        "nri_swapchain_queue_eligible",
        "nri_swapchain_fallback_reason",
        "nri_swapchain_fallback_detail"
    )
    foreach ($property in $requiredProperties) {
        if ($frameDiagnostics[0].PSObject.Properties.Name -notcontains
            $property) {
            throw "$Label diagnostics are missing $property"
        }
    }

    $adapterCount = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_adapter_count" -Label $Label)
    $selectedIndex = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_adapter_index" -Label $Label)
    $vendorId = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_adapter_vendor_id" -Label $Label)
    $deviceId = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_adapter_device_id" -Label $Label)
    $graphicsFamily = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_graphics_queue_family" -Label $Label)
    $presentFamily = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_present_queue_family" -Label $Label)
    $graphicsQueueCount = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_graphics_queue_count" -Label $Label)
    $requestedGraphicsQueueCount = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_requested_graphics_queue_count" -Label $Label)
    $presentQueueIndex = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_present_queue_index" -Label $Label)
    $asyncPresent = [bool](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "vulkan_async_present_supported" -Label $Label)
    $queueEligible = [bool](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "nri_swapchain_queue_eligible" -Label $Label)
    $fallbackReason = [uint32](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "nri_swapchain_fallback_reason" -Label $Label)
    $fallbackDetail = [string](Get-StableFrameValue `
        -FrameDiagnostics $frameDiagnostics `
        -Property "nri_swapchain_fallback_detail" -Label $Label)

    $metrics = [pscustomobject]@{
        Label = $Label
        FrameCount = $frameDiagnostics.Count
        AdapterCount = $adapterCount
        AdapterIndex = $selectedIndex
        VendorId = $vendorId
        DeviceId = $deviceId
        GraphicsQueueFamily = $graphicsFamily
        PresentQueueFamily = $presentFamily
        GraphicsQueueCount = $graphicsQueueCount
        RequestedGraphicsQueueCount = $requestedGraphicsQueueCount
        PresentQueueIndex = $presentQueueIndex
        AsyncPresent = $asyncPresent
        QueueEligible = $queueEligible
        FallbackReason = $fallbackReason
        FallbackDetail = $fallbackDetail
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
        AnyNriSwapchain = @(
            $frameDiagnostics |
                Where-Object { $_.nri_swapchain_owned }
        ).Count -gt 0
        AllNriSwapchains = -not @(
            $frameDiagnostics |
                Where-Object { -not $_.nri_swapchain_owned }
        ).Count
        ScreenshotSha256 = (
            Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256
        ).Hash
    }

    if ($metrics.AdapterCount -eq 0 -or
        $metrics.AdapterIndex -ge $metrics.AdapterCount -or
        $metrics.GraphicsQueueCount -eq 0) {
        throw "$Label reported an invalid Vulkan adapter topology"
    }
    if ($metrics.QueueEligible -ne
        ($metrics.GraphicsQueueFamily -eq $metrics.PresentQueueFamily)) {
        throw "$Label reported an inconsistent NRI queue eligibility"
    }
    $dualSharedQueue =
        $metrics.GraphicsQueueFamily -eq $metrics.PresentQueueFamily -and
        $metrics.GraphicsQueueCount -ge 2
    if ($dualSharedQueue) {
        if ($metrics.RequestedGraphicsQueueCount -ne 2 -or
            $metrics.PresentQueueIndex -ne 1 -or
            -not $metrics.AsyncPresent) {
            throw "$Label did not apply its dual-queue shared-family plan"
        }
    } elseif ($metrics.RequestedGraphicsQueueCount -ne 1 -or
              $metrics.PresentQueueIndex -ne 0 -or
              ($metrics.AsyncPresent -ne
               ($metrics.GraphicsQueueFamily -ne
                $metrics.PresentQueueFamily))) {
        throw "$Label did not apply its single/separate-queue plan"
    }
    if ($metrics.DrawCount -eq 0 -or
        $metrics.DrawCount -ne $metrics.NriDrawCount -or
        $metrics.DisplayCopyCount -eq 0 -or
        $metrics.ScanoutCount -eq 0) {
        throw "$Label broke native PICA rendering parity"
    }
    if ($metrics.AnyNriSwapchain) {
        if (-not $metrics.QueueEligible -or
            -not $metrics.AllNriSwapchains -or
            $metrics.AcquireCount -ne $Frames -or
            $metrics.PresentCount -ne $Frames -or
            $metrics.FallbackReason -ne 0 -or
            -not [string]::IsNullOrEmpty($metrics.FallbackDetail)) {
            throw "$Label reported partial NRI swapchain ownership"
        }
    } else {
        if ($metrics.AcquireCount -ne 0 -or
            $metrics.PresentCount -ne 0) {
            throw "$Label mixed the Vulkan fallback with NRI presentation"
        }
        if ($metrics.FallbackReason -eq 0 -or
            [string]::IsNullOrWhiteSpace($metrics.FallbackDetail)) {
            throw "$Label did not classify its Vulkan fallback"
        }
    }
    return $metrics
}

$previousAdapter =
    [Environment]::GetEnvironmentVariable(
        "OOT3D_GRAPHICS_VULKAN_ADAPTER", "Process")
$previousNriSwapchain =
    [Environment]::GetEnvironmentVariable(
        "OOT3D_GRAPHICS_NRI_SWAPCHAIN", "Process")
try {
    Remove-Item Env:OOT3D_GRAPHICS_NRI_SWAPCHAIN `
        -ErrorAction SilentlyContinue

    $automatic = Invoke-AdapterCheckpoint `
        -Label "Automatic adapter selection" `
        -Build (-not $SkipBuild) `
        -AdapterIndex ""
    $explicit = Invoke-AdapterCheckpoint `
        -Label "Explicit adapter selection" `
        -Build $false `
        -AdapterIndex ([string]$automatic.AdapterIndex)

    foreach ($property in @(
        "AdapterCount",
        "AdapterIndex",
        "VendorId",
        "DeviceId",
        "GraphicsQueueFamily",
        "PresentQueueFamily",
        "GraphicsQueueCount",
        "RequestedGraphicsQueueCount",
        "PresentQueueIndex",
        "AsyncPresent",
        "QueueEligible",
        "FallbackReason",
        "FallbackDetail",
        "DrawCount",
        "NriDrawCount",
        "DisplayCopyCount",
        "ScanoutCount",
        "AnyNriSwapchain",
        "ScreenshotSha256"
    )) {
        if ($explicit.$property -ne $automatic.$property) {
            throw "Explicit adapter selection changed $property"
        }
    }

    [pscustomobject]@{
        Result = "PASS"
        FramesPerCase = $Frames
        AdapterCount = $automatic.AdapterCount
        SelectedAdapter = $automatic.AdapterIndex
        VendorId = ('0x{0:X4}' -f $automatic.VendorId)
        DeviceId = ('0x{0:X4}' -f $automatic.DeviceId)
        QueueTopology = '{0}:{1} ({2} graphics queues)' -f `
            $automatic.GraphicsQueueFamily, `
            $automatic.PresentQueueFamily, `
            $automatic.GraphicsQueueCount
        RequestedQueues = $automatic.RequestedGraphicsQueueCount
        PresentQueueIndex = $automatic.PresentQueueIndex
        AsyncPresent = $automatic.AsyncPresent
        NriSwapchainActive = $automatic.AnyNriSwapchain
        DrawCount = $automatic.DrawCount
        DisplayCopyCount = $automatic.DisplayCopyCount
        ScanoutCount = $automatic.ScanoutCount
        ScreenshotSha256 = $automatic.ScreenshotSha256
    } | Format-List
} finally {
    if ($null -eq $previousAdapter) {
        Remove-Item Env:OOT3D_GRAPHICS_VULKAN_ADAPTER `
            -ErrorAction SilentlyContinue
    } else {
        $env:OOT3D_GRAPHICS_VULKAN_ADAPTER = $previousAdapter
    }
    if ($null -eq $previousNriSwapchain) {
        Remove-Item Env:OOT3D_GRAPHICS_NRI_SWAPCHAIN `
            -ErrorAction SilentlyContinue
    } else {
        $env:OOT3D_GRAPHICS_NRI_SWAPCHAIN = $previousNriSwapchain
    }
}
