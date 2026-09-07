[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(6, 120)]
    [int]$Frames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45,
    [string]$Checkpoint = "",
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumChangedPixelFraction = 0.001,
    [ValidateRange(0.0, 255.0)]
    [double]$MinimumMeanAbsoluteChannelDelta = 0.01
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot `
    "tools\Invoke-Oot3dRendererDev.ps1"
$buildScript = Join-Path $rendererRoot `
    "..\..\tools\oot3d\build_fast_dev.ps1"
$buildDirectory = Join-Path $rendererRoot ".renderer-dev\build"
$foundationTests = Join-Path $buildDirectory `
    "three_ds_recomp_runtime\tests\oot3d_graphics_foundation_tests.exe"
$fragmentLightingTests = Join-Path $buildDirectory `
    "oot3d_native_pica_fragment_lighting_tests.exe"
$framebufferModule = Join-Path $rendererRoot `
    "tools\Oot3dFramebufferArtifact.psm1"
Import-Module $framebufferModule -Force

$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$diagnosticsPath = Join-Path $artifactRoot `
    "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"
$environmentNames = @(
    "OOT3D_VULKAN_VALIDATION",
    "OOT3D_GRAPHICS_FOV_MULTIPLIER",
    "OOT3D_GRAPHICS_FXAA",
    "OOT3D_GRAPHICS_SMAA",
    "OOT3D_GRAPHICS_MSAA",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS",
    "OOT3D_GRAPHICS_GRASS_AUTO",
    "OOT3D_GRAPHICS_GRASS_HASH",
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_CACAO_QUALITY",
    "OOT3D_GRAPHICS_HIZ",
    "OOT3D_GRAPHICS_SSSR",
    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_HASH",
    "OOT3D_GRAPHICS_REFLECTION_TEXTURE_PROFILE",
    "OOT3D_GRAPHICS_HIZ_DEBUG",
    "OOT3D_GRAPHICS_TOON",
    "OOT3D_GRAPHICS_TOON_STRESS",
    "OOT3D_GRAPHICS_TOON_MATERIAL",
    "OOT3D_GRAPHICS_TOON_OUTLINE",
    "OOT3D_GRAPHICS_TOON_OUTLINE_STRESS"
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Clear-ToonGateEnvironment {
    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    # A neutral override suppresses persisted graphics settings and starts
    # every case from the same Custom/off renderer profile.
    $env:OOT3D_GRAPHICS_FOV_MULTIPLIER = "1"
    $env:OOT3D_VULKAN_VALIDATION = "1"
}

function Invoke-ToonCase {
    param(
        [Parameter(Mandatory)]
        [ValidateSet("baseline", "default", "stress", "outline")]
        [string]$Name,
        [Parameter(Mandatory)]
        [bool]$Build
    )

    Clear-ToonGateEnvironment
    if ($Name -eq "default") {
        $env:OOT3D_GRAPHICS_TOON = "1"
    } elseif ($Name -eq "stress") {
        $env:OOT3D_GRAPHICS_TOON_STRESS = "1"
    } elseif ($Name -eq "outline") {
        $env:OOT3D_GRAPHICS_TOON_STRESS = "1"
        $env:OOT3D_GRAPHICS_TOON_OUTLINE_STRESS = "1"
    }

    $arguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
    }
    if (-not $Build) {
        $arguments.SkipBuild = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
        $arguments.Checkpoint =
            [IO.Path]::GetFullPath($Checkpoint)
    }
    & $harness @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Toon $Name run exited with code $LASTEXITCODE"
    }
    if (-not (
            Test-Path -LiteralPath $diagnosticsPath -PathType Leaf
        ) -or -not (
            Test-Path -LiteralPath $screenshotPath -PathType Leaf
        )) {
        throw "Toon $Name run did not produce complete artifacts"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $runFrames = @($diagnostics.frames)
    if ($runFrames.Count -ne $Frames) {
        throw "Toon $Name run produced " +
            "$($runFrames.Count) of $Frames frames"
    }
    $vulkanErrors = [uint64](
        $runFrames |
            Measure-Object -Property `
                vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $runFrames |
            Measure-Object -Property `
                nri_validation_error_count -Maximum
    ).Maximum
    if ($vulkanErrors -ne 0 -or $nriErrors -ne 0) {
        throw "Toon $Name reported validation errors: " +
            "Vulkan=$vulkanErrors, NRI=$nriErrors"
    }

    $draws = [uint64](
        $runFrames |
            Measure-Object -Property native_pica_draw_count -Sum
    ).Sum
    $ownedDraws = [uint64](
        $runFrames |
            Measure-Object -Property nri_pica_owned_draw_count -Sum
    ).Sum
    if ($draws -eq 0 -or $draws -ne $ownedDraws) {
        throw "Toon $Name broke native PICA/NRI draw parity"
    }
    $toonDraws = [uint64](
        $runFrames |
            Measure-Object -Property toon_draw_count -Sum
    ).Sum
    if (($Name -eq "baseline" -and $toonDraws -ne 0) -or
        ($Name -ne "baseline" -and $toonDraws -eq 0)) {
        throw "Toon $Name reported an invalid toon draw count: " +
            "$toonDraws"
    }

    $imageDestination = Join-Path $artifactRoot `
        "toon-$Name.bmp"
    $diagnosticsDestination = Join-Path $artifactRoot `
        "toon-$Name-diagnostics.json"
    Copy-Item -LiteralPath $screenshotPath `
        -Destination $imageDestination -Force
    Copy-Item -LiteralPath $diagnosticsPath `
        -Destination $diagnosticsDestination -Force
    $framebuffer =
        Read-Oot3dFramebufferBmp -Path $imageDestination
    $metrics =
        Get-Oot3dFramebufferMetrics -Framebuffer $framebuffer
    if ($metrics.UniqueColors -lt 32 -or
        $metrics.ChannelRange -lt 24 -or
        $metrics.ChromaticFraction -lt 0.01) {
        throw "Toon $Name produced a collapsed framebuffer"
    }

    [pscustomobject]@{
        Name = $Name
        Draws = $draws
        ToonDraws = $toonDraws
        Framebuffer = $framebuffer
        Metrics = $metrics
    }
}

function Assert-VisibleDifference {
    param(
        [Parameter(Mandatory)]
        [psobject]$Reference,
        [Parameter(Mandatory)]
        [psobject]$Candidate,
        [Parameter(Mandatory)]
        [string]$Label
    )

    $difference = Compare-Oot3dFramebufferBmp `
        -Reference $Reference.Framebuffer `
        -Candidate $Candidate.Framebuffer `
        -ChangedPixelThreshold 2
    if ($difference.ChangedPixelFraction -lt
            $MinimumChangedPixelFraction -or
        $difference.MeanAbsoluteChannelDelta -lt
            $MinimumMeanAbsoluteChannelDelta -or
        $difference.MaximumChannelDelta -eq 0) {
        throw "$Label did not produce a visible framebuffer change: " +
            "changed=$($difference.ChangedPixelFraction), " +
            "mean=$($difference.MeanAbsoluteChannelDelta), " +
            "max=$($difference.MaximumChannelDelta)"
    }
    return $difference
}

function Invoke-ToonContractTests {
    if (-not $SkipBuild) {
        & $buildScript `
            -Target oot3d_graphics_foundation_tests `
            -BuildDirectory $buildDirectory -Parallel 2 |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the toon foundation tests"
        }
        & $buildScript `
            -Target oot3d_native_pica_fragment_lighting_tests `
            -BuildDirectory $buildDirectory -Parallel 2 |
            Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build the PICA fragment-lighting tests"
        }
    }
    if (-not (
            Test-Path -LiteralPath $foundationTests -PathType Leaf
        ) -or -not (
            Test-Path -LiteralPath $fragmentLightingTests -PathType Leaf
        )) {
        throw "Toon contract tests are missing; rerun without -SkipBuild"
    }
    & $foundationTests `
        "--gtest_filter=Oot3dPicaToon.*:Oot3dSceneComposite.*" |
        Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Toon shader/composite contract tests failed"
    }
    & $fragmentLightingTests | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "PICA fragment-lighting contract tests failed"
    }
}

try {
    $baseline = Invoke-ToonCase `
        -Name baseline -Build:(-not $SkipBuild)
    Invoke-ToonContractTests
    $default = Invoke-ToonCase -Name default -Build:$false
    $stress = Invoke-ToonCase -Name stress -Build:$false
    $outline = Invoke-ToonCase -Name outline -Build:$false

    foreach ($run in @($default, $stress, $outline)) {
        if ($run.Draws -ne $baseline.Draws) {
            throw "Toon $($run.Name) changed the draw signature: " +
                "$($run.Draws) vs $($baseline.Draws)"
        }
    }

    $defaultDifference = Assert-VisibleDifference `
        -Reference $baseline -Candidate $default `
        -Label "Approved toon preset"
    $stressDifference = Assert-VisibleDifference `
        -Reference $default -Candidate $stress `
        -Label "Toon style controls"
    $outlineDifference = Assert-VisibleDifference `
        -Reference $stress -Candidate $outline `
        -Label "Toon outline controls"

    Write-Host (
        ("Toon gate passed: {0} draws/case; changed pixels " +
         "preset={1:P3}, style={2:P3}, outline={3:P3}; " +
         "mean deltas {4:F3}/{5:F3}/{6:F3}") -f
        $baseline.Draws,
        $defaultDifference.ChangedPixelFraction,
        $stressDifference.ChangedPixelFraction,
        $outlineDifference.ChangedPixelFraction,
        $defaultDifference.MeanAbsoluteChannelDelta,
        $stressDifference.MeanAbsoluteChannelDelta,
        $outlineDifference.MeanAbsoluteChannelDelta)
} finally {
    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        if ($null -ne $savedEnvironment[$name]) {
            Set-Item "Env:$name" $savedEnvironment[$name]
        }
    }
}
