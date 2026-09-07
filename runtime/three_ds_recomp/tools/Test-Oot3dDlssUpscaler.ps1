[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [string]$BuildDirectory = "",
    [ValidateRange(60, 600)]
    [int]$Frames = 120,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot "tools\Invoke-Oot3dRendererDev.ps1"
$buildScript = Join-Path $rendererRoot `
    "..\..\tools\oot3d\build_fast_dev.ps1"
$buildDirectory = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $rendererRoot ".renderer-dev\build"
} else {
    [IO.Path]::GetFullPath($BuildDirectory)
}
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
    $env:OOT3D_GRAPHICS_DLSS = "1"
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

    $dlssRuntime = Join-Path $buildDirectory "nvngx_dlss.dll"
    if (-not (Test-Path -LiteralPath $dlssRuntime -PathType Leaf)) {
        throw "Official NVIDIA DLSS runtime was not deployed"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $dlssRuntime
    if ($signature.Status -ne "Valid" -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch
            '(^|, )O=NVIDIA Corporation(,|$)') {
        throw "Deployed DLSS runtime is not validly signed by NVIDIA"
    }
    $runtimeVersion = [Version](
        ((Get-Item -LiteralPath $dlssRuntime).VersionInfo.ProductVersion) `
            -replace ',', '.')
    $cacheVersion = Select-String -LiteralPath (
        Join-Path $buildDirectory "CMakeCache.txt") `
        -Pattern '^THREE_DS_RECOMP_NGX_VERSION:STRING=(.+)$' |
        Select-Object -First 1
    if ($null -eq $cacheVersion) {
        throw "Configured NGX version is absent from CMakeCache.txt"
    }
    $expectedVersion = [Version]$cacheVersion.Matches[0].Groups[1].Value
    if ($runtimeVersion.Major -ne $expectedVersion.Major -or
        $runtimeVersion.Minor -ne $expectedVersion.Minor -or
        $runtimeVersion.Build -ne $expectedVersion.Build) {
        throw "DLSS runtime $runtimeVersion does not match SDK $expectedVersion"
    }

    & $harness -SkipBuild -FromBoot -BuildDirectory $buildDirectory `
        -Frames $Frames `
        -MaxSeconds $MaxSeconds |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "DLSS runtime exited with $LASTEXITCODE"
    }
    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $diagnosticFrames = @($diagnostics.frames)
    if ($diagnosticFrames.Count -ne $Frames) {
        throw "DLSS produced $($diagnosticFrames.Count)/$Frames frames"
    }
    $activeFrames = @(
        $diagnosticFrames |
            Where-Object { $_.dlss_upscale_pass_count -gt 0 }
    )
    $adapterVendor = [uint32]$diagnosticFrames[-1].vulkan_adapter_vendor_id
    if ($activeFrames.Count -eq 0 -and $adapterVendor -ne 0x10DE) {
        [pscustomobject]@{
            Result = "SKIP"
            Reason = "DLSS requires an NVIDIA adapter"
            VendorId = "0x{0:X4}" -f $adapterVendor
        } | Format-List
        return
    }
    $sceneFrames = @(
        $diagnosticFrames |
            Where-Object {
                $_.pica_display_scene_resolved_snapshot_count -gt 0
            }
    )
    if ($sceneFrames.Count -eq 0) {
        throw "DLSS gate did not reach a resolved 3D scene"
    }
    if ($activeFrames.Count -lt [Math]::Max(1, $sceneFrames.Count - 2)) {
        throw "DLSS did not dispatch across resolved scene frames"
    }
    $last = $activeFrames[-1]
    if ($last.vulkan_validation_error_count -ne 0 -or
        $last.nri_validation_error_count -ne 0) {
        throw "DLSS produced renderer validation errors"
    }
    if (-not $last.dlss_nri_dispatched -or
        -not $last.upscaler_output_nri_owned -or
        -not $last.upscaler_barriers_nri_owned) {
        throw "DLSS did not retain NRI dispatch/output/barrier ownership"
    }
    $motionPasses =
        ($diagnosticFrames |
            Measure-Object -Property motion_vector_pass_count -Sum).Sum
    $resetCount =
        ($diagnosticFrames |
            Measure-Object -Property dlss_history_reset_count -Sum).Sum
    if ($motionPasses -lt $activeFrames.Count -or $resetCount -lt 1) {
        throw "DLSS did not consume motion or reset its temporal history"
    }
    if ($last.temporal_prepare_count -eq 0 -or
        $last.temporal_valid_count -eq 0) {
        throw "DLSS did not prepare a valid temporal view"
    }
    if ($last.native_pica_draw_count -eq 0 -or
        $last.nri_pica_owned_draw_count -eq 0) {
        throw "DLSS did not preserve the playable PICA scene"
    }
    if ($last.dlss_input_width -ge $last.dlss_output_width -or
        $last.dlss_input_height -ge $last.dlss_output_height) {
        throw "DLSS Quality did not use a lower internal resolution"
    }

    [pscustomobject]@{
        Result = "PASS"
        Frames = $Frames
        Dispatches = $activeFrames.Count
        MotionPasses = $motionPasses
        HistoryResets = $resetCount
        Input = "$($last.dlss_input_width)x$($last.dlss_input_height)"
        Output = "$($last.dlss_output_width)x$($last.dlss_output_height)"
        NriOutput = $last.upscaler_output_nri_owned
        NriBarriers = $last.upscaler_barriers_nri_owned
        Runtime = [string]$runtimeVersion
        Signer = "NVIDIA Corporation"
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
