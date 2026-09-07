[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$TemporalStability,
    [ValidateRange(3, 60)]
    [int]$ScreenshotSequenceLength = 4,
    [ValidateRange(4, 120)]
    [int]$Frames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45,
    [string]$Checkpoint = "",
    [ValidatePattern("^[0-9a-fA-F]{16}$")]
    [string]$TextureHash = "be15aff93dfdcd88",
    [ValidateSet("water", "metal", "polished", "custom")]
    [string]$TextureProfile = "polished",
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumChangedPixelFraction = 0.001,
    [ValidateRange(0.0, 1.0)]
    [double]$MaximumChangedPixelFraction = 0.25,
    [ValidateRange(0.0, 255.0)]
    [double]$MinimumMeanAbsoluteChannelDelta = 0.05,
    [ValidateRange(0.1, 1000.0)]
    [double]$MaximumReflectionMilliseconds = 50.0,
    [ValidateRange(1.0, 10.0)]
    [double]$MaximumTemporalDeltaRatio = 1.25,
    [ValidateRange(0.0, 10.0)]
    [double]$MaximumTemporalDeltaExcess = 0.05
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot `
    "tools\Invoke-Oot3dRendererDev.ps1"
$sssrGate = Join-Path $rendererRoot `
    "tools\Test-Oot3dFidelityFxSssr.ps1"
$framebufferValidator = Join-Path $rendererRoot `
    "tools\Test-Oot3dFramebufferArtifact.ps1"
$framebufferDifference = Join-Path $rendererRoot `
    "tools\Measure-Oot3dFramebufferDifference.ps1"
$framebufferModule = Join-Path $rendererRoot `
    "tools\Oot3dFramebufferArtifact.psm1"
Import-Module $framebufferModule -Force
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$diagnosticsPath = Join-Path $artifactRoot `
    "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"
$hiZDiagnosticsPath = Join-Path $artifactRoot `
    "reflection-provider-hiz-diagnostics.json"
$hiZScreenshotPath = Join-Path $artifactRoot `
    "reflection-provider-hiz.bmp"
$sssrDiagnosticsPath = Join-Path $artifactRoot `
    "reflection-provider-sssr-diagnostics.json"
$sssrScreenshotPath = Join-Path $artifactRoot `
    "reflection-provider-sssr.bmp"
$normalizedTextureHash = $TextureHash.ToLowerInvariant()
$diagnosticsModule = Join-Path $rendererRoot `
    "tools\Oot3dReflectionDiagnostics.psm1"
Import-Module $diagnosticsModule -Force

$environmentNames = @(
    "OOT3D_VULKAN_VALIDATION",
    "OOT3D_GRAPHICS_HIZ",
    "OOT3D_GRAPHICS_SSSR",
    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH",
    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS"
)

function Copy-ReflectionSequenceArtifacts {
    param(
        [Parameter(Mandatory)]
        [string]$Provider
    )

    $sources = @(
        Get-ChildItem -LiteralPath $artifactRoot -File `
            -Filter "native-game_*.bmp" |
            Sort-Object -Property Name
    )
    if ($sources.Count -ne $ScreenshotSequenceLength) {
        throw "$Provider produced $($sources.Count) of " +
            "$ScreenshotSequenceLength sequence screenshots"
    }
    @(
        foreach ($source in $sources) {
            $suffix = $source.Name.Substring(
                "native-game".Length)
            $destination = Join-Path $artifactRoot `
                "reflection-provider-$($Provider.ToLowerInvariant())$suffix"
            Copy-Item -LiteralPath $source.FullName `
                -Destination $destination -Force
            [IO.Path]::GetFullPath($destination)
        }
    )
}

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
    $env:OOT3D_GRAPHICS_HIZ = "1"
    $env:OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH =
        $normalizedTextureHash
    $env:OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE =
        $TextureProfile

    $hiZArguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
    }
    if ($SkipBuild) {
        $hiZArguments.SkipBuild = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
        $hiZArguments.Checkpoint =
            [IO.Path]::GetFullPath($Checkpoint)
    }
    if ($TemporalStability) {
        $hiZArguments.ScreenshotSequence = $true
        $hiZArguments.ScreenshotSequenceLength =
            $ScreenshotSequenceLength
    }
    & $harness @hiZArguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Hi-Z comparison run exited with code $LASTEXITCODE"
    }
    $hiZSequence =
        if ($TemporalStability) {
            Copy-ReflectionSequenceArtifacts -Provider HiZ
        } else {
            @()
        }
    $effectiveHiZScreenshot =
        if ($TemporalStability) {
            $hiZSequence[-1]
        } else {
            $screenshotPath
        }
    & $framebufferValidator -Path $effectiveHiZScreenshot |
        Out-Null
    Copy-Item -LiteralPath $diagnosticsPath `
        -Destination $hiZDiagnosticsPath -Force
    Copy-Item -LiteralPath $effectiveHiZScreenshot `
        -Destination $hiZScreenshotPath -Force

    $sssrArguments = @{
        SkipBuild = $true
        Frames = $Frames
        MaxSeconds = $MaxSeconds
        TextureHash = $normalizedTextureHash
        TextureProfile = $TextureProfile
    }
    if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
        $sssrArguments.Checkpoint =
            [IO.Path]::GetFullPath($Checkpoint)
    }
    if ($TemporalStability) {
        $sssrArguments.ScreenshotSequence = $true
        $sssrArguments.ScreenshotSequenceLength =
            $ScreenshotSequenceLength
    }
    & $sssrGate @sssrArguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "FidelityFX comparison run exited with code $LASTEXITCODE"
    }
    Copy-Item -LiteralPath $diagnosticsPath `
        -Destination $sssrDiagnosticsPath -Force
    $sssrSequence =
        if ($TemporalStability) {
            Copy-ReflectionSequenceArtifacts `
                -Provider FidelityFxSssr
        } else {
            @()
        }
    $effectiveSssrScreenshot =
        if ($TemporalStability) {
            $sssrSequence[-1]
        } else {
            $screenshotPath
        }
    Copy-Item -LiteralPath $effectiveSssrScreenshot `
        -Destination $sssrScreenshotPath -Force

    $hiZ = Get-Oot3dReflectionRunMetrics `
        -Path $hiZDiagnosticsPath `
        -Provider HiZ `
        -ExpectedFrames $Frames `
        -TextureHash $normalizedTextureHash `
        -MaximumReflectionMilliseconds `
            $MaximumReflectionMilliseconds
    $sssr = Get-Oot3dReflectionRunMetrics `
        -Path $sssrDiagnosticsPath `
        -Provider FidelityFxSssr `
        -ExpectedFrames $Frames `
        -TextureHash $normalizedTextureHash `
        -MaximumReflectionMilliseconds `
            $MaximumReflectionMilliseconds
    if ($hiZ.DrawCount -ne $sssr.DrawCount -or
        $hiZ.ProfiledDrawCount -ne $sssr.ProfiledDrawCount -or
        $hiZ.TextureIdentityCount -ne $sssr.TextureIdentityCount) {
        throw "Hi-Z/SSSR comparison changed scene or material coverage"
    }
    $difference =
        & $framebufferDifference `
            -ReferencePath $hiZScreenshotPath `
            -CandidatePath $sssrScreenshotPath
    if ($difference.ReferenceSha256 -eq
            $difference.CandidateSha256 -or
        $difference.ChangedPixelFraction -lt
            $MinimumChangedPixelFraction -or
        $difference.ChangedPixelFraction -gt
            $MaximumChangedPixelFraction -or
        $difference.MeanAbsoluteChannelDelta -lt
            $MinimumMeanAbsoluteChannelDelta) {
        throw "Hi-Z/SSSR framebuffer difference is outside the " +
            "declared comparison envelope"
    }
    $hiZTemporal =
        if ($TemporalStability) {
            Get-Oot3dFramebufferSequenceMetrics `
                -Paths $hiZSequence
        } else {
            $null
        }
    $sssrTemporal =
        if ($TemporalStability) {
            Get-Oot3dFramebufferSequenceMetrics `
                -Paths $sssrSequence
        } else {
            $null
        }
    if ($TemporalStability) {
        $maximumTemporalDelta =
            $hiZTemporal.MeanAbsoluteChannelDelta *
                $MaximumTemporalDeltaRatio +
            $MaximumTemporalDeltaExcess
        $maximumTemporalChangedFraction =
            $hiZTemporal.MeanChangedPixelFraction *
                $MaximumTemporalDeltaRatio +
            0.01
        if ($sssrTemporal.MeanAbsoluteChannelDelta -gt
                $maximumTemporalDelta -or
            $sssrTemporal.MeanChangedPixelFraction -gt
                $maximumTemporalChangedFraction) {
            throw "FidelityFX SSSR temporal delta exceeds the Hi-Z envelope"
        }
    }

    [pscustomobject]@{
        Result = "PASS"
        FramesPerProvider = $Frames
        TextureHash = $normalizedTextureHash
        TextureProfile = $TextureProfile
        DrawCountPerProvider = $hiZ.DrawCount
        ProfiledDrawCountPerProvider =
            $hiZ.ProfiledDrawCount
        TextureIdentityCount =
            $hiZ.TextureIdentityCount
        HiZReflectionCount = $hiZ.HiZCount
        SssrDispatchCount = $sssr.FidelityFxSssrCount
        HiZMeanMilliseconds =
            $hiZ.ReflectionMeanMilliseconds
        HiZMaximumMilliseconds =
            $hiZ.ReflectionMaximumMilliseconds
        SssrMeanMilliseconds =
            $sssr.ReflectionMeanMilliseconds
        SssrMaximumMilliseconds =
            $sssr.ReflectionMaximumMilliseconds
        SssrToHiZMeanCostRatio =
            if ($hiZ.ReflectionMeanMilliseconds -gt 0.0) {
                $sssr.ReflectionMeanMilliseconds /
                    $hiZ.ReflectionMeanMilliseconds
            } else {
                $null
            }
        MeanAbsoluteChannelDelta =
            $difference.MeanAbsoluteChannelDelta
        RootMeanSquareChannelDelta =
            $difference.RootMeanSquareChannelDelta
        MaximumChannelDelta =
            $difference.MaximumChannelDelta
        ChangedPixelFraction =
            $difference.ChangedPixelFraction
        HiZScreenshotSha256 =
            $difference.ReferenceSha256
        SssrScreenshotSha256 =
            $difference.CandidateSha256
        TemporalFrameCount =
            if ($TemporalStability) {
                $hiZTemporal.FrameCount
            } else {
                0
            }
        HiZTemporalMeanChannelDelta =
            if ($TemporalStability) {
                $hiZTemporal.MeanAbsoluteChannelDelta
            } else {
                $null
            }
        SssrTemporalMeanChannelDelta =
            if ($TemporalStability) {
                $sssrTemporal.MeanAbsoluteChannelDelta
            } else {
                $null
            }
        HiZTemporalChangedPixelFraction =
            if ($TemporalStability) {
                $hiZTemporal.MeanChangedPixelFraction
            } else {
                $null
            }
        SssrTemporalChangedPixelFraction =
            if ($TemporalStability) {
                $sssrTemporal.MeanChangedPixelFraction
            } else {
                $null
            }
        VulkanErrors = 0
        NriErrors = 0
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
