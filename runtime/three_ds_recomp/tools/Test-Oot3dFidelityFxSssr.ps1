[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$WithTaa,
    [switch]$ScreenshotSequence,
    [ValidateRange(2, 60)]
    [int]$ScreenshotSequenceLength = 4,
    [ValidateRange(4, 120)]
    [int]$Frames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45,
    [string]$Checkpoint = "",
    [ValidatePattern("^[0-9a-fA-F]{16}$")]
    [string]$TextureHash = "be15aff93dfdcd88",
    [ValidateSet("water", "metal", "polished", "custom")]
    [string]$TextureProfile = "polished"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot `
    "tools\Invoke-Oot3dRendererDev.ps1"
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$diagnosticsPath = Join-Path $artifactRoot `
    "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"
$screenshotValidator = Join-Path $rendererRoot `
    "tools\Test-Oot3dFramebufferArtifact.ps1"
$environmentNames = @(
    "OOT3D_VULKAN_VALIDATION",
    "OOT3D_GRAPHICS_SSSR",
    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH",
    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE",
    "OOT3D_GRAPHICS_HIZ",
    "OOT3D_GRAPHICS_MOTION",
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS"
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
    $env:OOT3D_VULKAN_VALIDATION = "1"
    $env:OOT3D_GRAPHICS_SSSR = "1"
    # Inject one exact catalog hash through the settings service so the gate
    # exercises the same resolver used by the UI. Unassigned textures remain
    # non-reflecting.
    $normalizedTextureHash = $TextureHash.ToLowerInvariant()
    $env:OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH =
        $normalizedTextureHash
    $env:OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE =
        $TextureProfile
    if ($WithTaa) {
        $env:OOT3D_GRAPHICS_TAA = "1"
    }

    $arguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
    }
    if ($SkipBuild) {
        $arguments.SkipBuild = $true
    }
    if ($ScreenshotSequence) {
        $arguments.ScreenshotSequence = $true
        $arguments.ScreenshotSequenceLength =
            $ScreenshotSequenceLength
    }
    if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
        $arguments.Checkpoint = [IO.Path]::GetFullPath($Checkpoint)
    }
    & $harness @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "FidelityFX SSSR scene gate exited with code $LASTEXITCODE"
    }
    $capturedScreenshots = @(
        if ($ScreenshotSequence) {
            Get-ChildItem -LiteralPath $artifactRoot -File `
                -Filter "native-game_*.bmp" |
                Sort-Object -Property Name
        } else {
            Get-Item -LiteralPath $screenshotPath `
                -ErrorAction SilentlyContinue
        }
    )
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf) -or
        $capturedScreenshots.Count -eq 0) {
        throw "FidelityFX SSSR scene gate did not produce artifacts"
    }
    $effectiveScreenshotPath =
        $capturedScreenshots[-1].FullName
    $screenshotMetrics =
        & $screenshotValidator -Path $effectiveScreenshotPath

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $Frames) {
        throw "FidelityFX SSSR scene gate produced " +
            "$($frameDiagnostics.Count) of $Frames frames"
    }
    if (@(
            $frameDiagnostics |
                Where-Object {
                    -not $_.vulkan_validation_enabled -or
                    -not $_.nri_validation_enabled
                }
        ).Count -ne 0) {
        throw "FidelityFX SSSR scene gate did not keep validation enabled"
    }
    $vulkanErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property nri_validation_error_count -Maximum
    ).Maximum
    if ($vulkanErrors -ne 0 -or $nriErrors -ne 0) {
        throw "FidelityFX SSSR reported validation errors: " +
            "Vulkan=$vulkanErrors, NRI=$nriErrors"
    }

    $drawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $nriDrawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($drawCount -eq 0 -or $drawCount -ne $nriDrawCount) {
        throw "FidelityFX SSSR broke native PICA/NRI draw parity"
    }
    $sssrCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property fidelityfx_sssr_count -Sum
    ).Sum
    $motionCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property motion_vector_pass_count -Sum
    ).Sum
    if ($sssrCount -eq 0 -or $motionCount -eq 0) {
        throw "FidelityFX SSSR or its motion-vector input did not execute"
    }
    $linearWorkingColorCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property linear_working_color_count -Sum
    ).Sum
    if ($linearWorkingColorCount -lt $sssrCount) {
        throw "FidelityFX SSSR escaped the linear working-color contract"
    }
    $linearScanoutCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property linear_scanout_count -Sum
    ).Sum
    if ($linearScanoutCount -lt $sssrCount) {
        throw "FidelityFX SSSR linear color did not reach scanout"
    }
    $reflectionIblCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property reflection_ibl_count -Sum
    ).Sum
    $reflectionIblProfileUpdateCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property reflection_ibl_profile_update_count -Sum
    ).Sum
    $reflectionMaterialResolveCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property reflection_material_resolve_count -Sum
    ).Sum
    $reflectionMaterialCandidateDrawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property reflection_material_candidate_draw_count -Sum
    ).Sum
    $reflectionMaterialCalibratedDrawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property reflection_material_calibrated_draw_count -Sum
    ).Sum
    $reflectionMaterialProfiledDrawCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property reflection_material_profiled_draw_count -Sum
    ).Sum
    if ($reflectionIblCount -lt $sssrCount -or
        $reflectionMaterialResolveCount -lt $sssrCount -or
        $reflectionIblProfileUpdateCount -eq 0 -or
        $reflectionMaterialCandidateDrawCount -eq 0 -or
        $reflectionMaterialCalibratedDrawCount -eq 0 -or
        $reflectionMaterialProfiledDrawCount -eq 0) {
        throw "FidelityFX SSSR did not execute its IBL/material resolve chain"
    }
    $profileContract = @{
        water = @{
            Id = 0
            Reflectivity = 0.78
            Roughness = 0.10
        }
        metal = @{
            Id = 1
            Reflectivity = 0.88
            Roughness = 0.22
        }
        polished = @{
            Id = 2
            Reflectivity = 0.58
            Roughness = 0.32
        }
        custom = @{
            Id = 3
            Reflectivity = 0.65
            Roughness = 0.35
        }
    }[$TextureProfile]
    $profiledTextureUsages = @()
    $discoveredTextureIdentities =
        [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::Ordinal)
    foreach ($frame in $frameDiagnostics) {
        if ([uint64]$frame.reflection_texture_usage_overflow_count -ne 0) {
            throw "FidelityFX SSSR reflection texture inventory overflowed"
        }
        foreach ($usage in @($frame.reflection_texture_usages)) {
            [void]$discoveredTextureIdentities.Add(
                "$($usage.texture_hash):$($usage.width)x" +
                "$($usage.height):$($usage.mapper_slot)")
            if ([string]$usage.texture_hash -eq
                    $normalizedTextureHash -and
                [bool]$usage.profiled) {
                $profiledTextureUsages += $usage
            }
        }
    }
    $profiledTextureDrawCount = [uint64](
        $profiledTextureUsages |
            Measure-Object -Property profiled_draw_count -Sum
    ).Sum
    if ($profiledTextureUsages.Count -eq 0 -or
        $profiledTextureDrawCount -ne
            $reflectionMaterialProfiledDrawCount) {
        throw "FidelityFX SSSR did not attribute profiled draws to exact " +
            "texture $normalizedTextureHash"
    }
    if (@(
            $profiledTextureUsages |
                Where-Object {
                    [uint64]$_.rule_id -eq 0 -or
                    [uint64]$_.profile -ne
                        [uint64]$profileContract.Id -or
                    [Math]::Abs(
                        [double]$_.reflectivity -
                        [double]$profileContract.Reflectivity) -gt 0.0001 -or
                    [Math]::Abs(
                        [double]$_.roughness -
                        [double]$profileContract.Roughness) -gt 0.0001
                }
        ).Count -ne 0) {
        throw "FidelityFX SSSR texture profile telemetry does not match " +
            "$TextureProfile defaults"
    }
    if (@(
            $frameDiagnostics |
                Where-Object {
                    [uint64]$_.reflection_ibl_count -gt 0 -and
                    $_.reflection_ibl_pica_derived
                }
        ).Count -eq 0) {
        throw "FidelityFX SSSR did not derive an environment from PICA"
    }
    if (@(
            $frameDiagnostics |
                Where-Object {
                    [uint64]$_.fidelityfx_sssr_count -gt 0 -and
                    (-not $_.fidelityfx_sssr_nri_wrapped -or
                     -not $_.fidelityfx_sssr_output_nri_owned -or
                     -not $_.fidelityfx_sssr_linear_input -or
                     -not $_.linear_working_color_output_nri_owned -or
                     -not $_.linear_working_color_compute_nri_owned -or
                     -not $_.linear_working_color_barriers_nri_owned -or
                     [uint64]$_.reflection_ibl_count -eq 0 -or
                     [uint64]$_.reflection_ibl_environment_mip_count -ne 6 -or
                     -not $_.reflection_ibl_environment_nri_owned -or
                     -not $_.reflection_ibl_brdf_nri_owned -or
                     -not $_.reflection_ibl_compute_nri_owned -or
                     -not $_.reflection_ibl_barriers_nri_owned -or
                     [uint64]$_.reflection_material_resolve_count -eq 0 -or
                     -not $_.reflection_material_resolve_output_nri_owned -or
                     -not $_.reflection_material_resolve_compute_nri_owned -or
                     -not $_.reflection_material_resolve_barriers_nri_owned)
                }
        ).Count -ne 0) {
        throw "FidelityFX SSSR escaped the linear/IBL/NRI ownership contract"
    }

    $fallbackCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property fidelityfx_sssr_fallback_count -Sum
    ).Sum
    $hiZFallbackCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property hiz_reflection_count -Sum
    ).Sum
    if ($fallbackCount -eq 0 -or $hiZFallbackCount -eq 0) {
        throw "FidelityFX SSSR did not preserve the per-view Hi-Z fallback"
    }
    $compositeCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property scene_composite_pass_count -Sum
    ).Sum
    $taaCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property temporal_aa_pass_count -Sum
    ).Sum
    if ($WithTaa -and ($compositeCount -eq 0 -or $taaCount -eq 0)) {
        throw "Linear SSSR did not reach the composite/TAA chain"
    }

    [pscustomobject]@{
        Result = "PASS"
        Frames = $frameDiagnostics.Count
        DrawCount = $drawCount
        FidelityFxSssrCount = $sssrCount
        ReflectionIblCount = $reflectionIblCount
        ReflectionIblProfileUpdates = $reflectionIblProfileUpdateCount
        ReflectionMaterialResolveCount = $reflectionMaterialResolveCount
        ReflectionMaterialCandidateDraws =
            $reflectionMaterialCandidateDrawCount
        ReflectionMaterialCalibratedDraws =
            $reflectionMaterialCalibratedDrawCount
        ReflectionMaterialProfiledDraws =
            $reflectionMaterialProfiledDrawCount
        ReflectionTextureHash = $normalizedTextureHash
        ReflectionTextureProfile = $TextureProfile
        ReflectionTextureProfiledDraws =
            $profiledTextureDrawCount
        ReflectionTextureIdentityCount =
            $discoveredTextureIdentities.Count
        LinearWorkingColorCount = $linearWorkingColorCount
        LinearScanoutCount = $linearScanoutCount
        MotionVectorCount = $motionCount
        CompositeCount = $compositeCount
        TaaCount = $taaCount
        HiZFallbackCount = $hiZFallbackCount
        VulkanErrors = $vulkanErrors
        NriErrors = $nriErrors
        ScreenshotUniqueColors = $screenshotMetrics.UniqueColors
        ScreenshotChannelRange = $screenshotMetrics.ChannelRange
        ScreenshotChromaticFraction =
            $screenshotMetrics.ChromaticFraction
        ScreenshotSha256 = $screenshotMetrics.Sha256
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
