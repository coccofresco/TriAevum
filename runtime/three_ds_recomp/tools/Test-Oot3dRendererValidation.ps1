[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(4, 240)]
    [int]$Frames = 8,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45
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
$summaryPath = Join-Path $artifactRoot "native-game-summary.json"
$transitionCheckpoint = Join-Path `
    "I:\oot3dre_work\native_game\checkpoints" `
    "timing_link_house_11800.oot3dsav"
$environmentNames = @(
    "OOT3D_VULKAN_VALIDATION",
    "OOT3D_GRAPHICS_CACAO",
    "OOT3D_GRAPHICS_CACAO_QUALITY",
    "OOT3D_GRAPHICS_HIZ",
    "OOT3D_GRAPHICS_SSSR",
    "OOT3D_GRAPHICS_TAA",
    "OOT3D_GRAPHICS_GRASS_AUTO",
    "OOT3D_GRAPHICS_TOON_MATERIAL",
    "OOT3D_GRAPHICS_NIS",
    "OOT3D_GRAPHICS_FSR",
    "OOT3D_GRAPHICS_DLSS",
    "OOT3D_GRAPHICS_TEST_RELOAD_SAVESTATE_AFTER_FRAMES"
)

function Invoke-ValidationCase {
    param(
        [ValidateSet("Authentic", "Advanced")]
        [string]$Mode,
        [bool]$Build
    )

    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:OOT3D_VULKAN_VALIDATION = "1"
    if ($Mode -eq "Advanced") {
        $env:OOT3D_GRAPHICS_CACAO = "1"
        $env:OOT3D_GRAPHICS_CACAO_QUALITY = "1"
        $env:OOT3D_GRAPHICS_HIZ = "1"
        $env:OOT3D_GRAPHICS_TAA = "1"
    }

    $arguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
    }
    if (-not $Build) {
        $arguments.SkipBuild = $true
    }
    if ($Mode -eq "Authentic") {
        # Exercises the dynamic Shadow2D clear in addition to the
        # render-target initialization clear used by both cases.
        $arguments.PicaMemoryFillSmoke = $true
    }
    & $harness @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Mode validation run exited with code $LASTEXITCODE"
    }
    if (-not (
            Test-Path -LiteralPath $diagnosticsPath -PathType Leaf
        ) -or -not (
            Test-Path -LiteralPath $screenshotPath -PathType Leaf
        )) {
        throw "$Mode validation run did not produce complete artifacts"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $Frames) {
        throw "$Mode validation run produced " +
            "$($frameDiagnostics.Count) of $Frames frames"
    }
    if (@(
            $frameDiagnostics |
                Where-Object {
                    -not $_.vulkan_validation_enabled -or
                    -not $_.nri_validation_enabled
                }
        ).Count -ne 0) {
        throw "$Mode did not keep Vulkan and NRI validation enabled"
    }

    $vulkanErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                nri_validation_error_count -Maximum
    ).Maximum
    if ($vulkanErrors -ne 0 -or $nriErrors -ne 0) {
        throw "$Mode reported validation errors: " +
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
    $drawOwnershipMismatches = @(
        $frameDiagnostics |
            Where-Object {
                [uint64]$_.native_pica_draw_count -ne 0U -and
                [uint64]$_.nri_pica_owned_draw_count -ne
                    [uint64]$_.native_pica_draw_count
            }
    )
    if ($drawCount -eq 0 -or $drawCount -ne $nriDrawCount -or
        $drawOwnershipMismatches.Count -ne 0) {
        throw "$Mode broke native PICA/NRI draw parity"
    }

    $cacaoCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property cacao_pass_count -Sum
    ).Sum
    $cacaoAmbientCompositeCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                cacao_ambient_composite_count -Sum
    ).Sum
    $cacaoAmbientGuideCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property cacao_ambient_guide_draw_count -Sum
    ).Sum
    $cacaoAmbientRgbGuideCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property cacao_ambient_rgb_guide_draw_count -Sum
    ).Sum
    $cacaoAmbientFallbackGuideCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                cacao_ambient_fallback_guide_draw_count -Sum
    ).Sum
    $cacaoNormalGuideCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property `
                cacao_normal_guide_pass_count -Sum
    ).Sum
    $hizCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property hiz_reflection_count -Sum
    ).Sum
    $taaCount = [uint64](
        $frameDiagnostics |
            Measure-Object -Property temporal_aa_pass_count -Sum
    ).Sum
    if ($Mode -eq "Advanced") {
        if ($cacaoCount -eq 0 -or
            $cacaoNormalGuideCount -eq 0 -or
            $cacaoAmbientGuideCount -eq 0 -or
            $cacaoAmbientCompositeCount -eq 0 -or
            $hizCount -eq 0 -or
            $taaCount -eq 0) {
            throw "Advanced validation did not execute ambient-aware " +
                "CACAO, Hi-Z and TAA"
        }
        if ($cacaoAmbientGuideCount -ne
            ($cacaoAmbientRgbGuideCount +
             $cacaoAmbientFallbackGuideCount)) {
            throw "Advanced validation lost exact/fallback ambient " +
                "guide attribution"
        }
    } elseif ($cacaoCount -ne 0 -or
              $cacaoNormalGuideCount -ne 0 -or
              $cacaoAmbientGuideCount -ne 0 -or
              $cacaoAmbientCompositeCount -ne 0 -or
              $hizCount -ne 0 -or
              $taaCount -ne 0) {
        throw "Authentic validation unexpectedly executed advanced effects"
    }

    return [pscustomobject]@{
        Mode = $Mode
        Frames = $frameDiagnostics.Count
        DrawCount = $drawCount
        VulkanWarnings = [uint64](
            $frameDiagnostics |
                Measure-Object -Property `
                    vulkan_validation_warning_count -Maximum
        ).Maximum
        NriWarnings = [uint64](
            $frameDiagnostics |
                Measure-Object -Property `
                    nri_validation_warning_count -Maximum
        ).Maximum
        CacaoCount = $cacaoCount
        CacaoNormalGuideCount = $cacaoNormalGuideCount
        CacaoAmbientCompositeCount =
            $cacaoAmbientCompositeCount
        CacaoAmbientGuideCount = $cacaoAmbientGuideCount
        CacaoAmbientRgbGuideCount = $cacaoAmbientRgbGuideCount
        CacaoAmbientFallbackGuideCount =
            $cacaoAmbientFallbackGuideCount
        HiZCount = $hizCount
        TaaCount = $taaCount
        ScreenshotSha256 = (
            Get-FileHash -LiteralPath $screenshotPath `
                -Algorithm SHA256
        ).Hash
    }
}

function Invoke-SavestateSceneTransitionCase {
    $transitionFrames = [Math]::Max(16, $Frames)
    if (-not (Test-Path -LiteralPath $transitionCheckpoint -PathType Leaf)) {
        throw "Scene-transition checkpoint is missing: $transitionCheckpoint"
    }
    foreach ($name in $environmentNames) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:OOT3D_VULKAN_VALIDATION = "1"
    $env:OOT3D_GRAPHICS_CACAO = "1"
    $env:OOT3D_GRAPHICS_CACAO_QUALITY = "1"
    $env:OOT3D_GRAPHICS_HIZ = "1"
    $env:OOT3D_GRAPHICS_TAA = "1"

    & $harness -SkipBuild -SavestateReloadSmoke `
        -Checkpoint $transitionCheckpoint -Frames $transitionFrames `
        -MaxSeconds $MaxSeconds | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Savestate scene transition exited with code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
        throw "Savestate scene transition did not produce complete artifacts"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $transitionFrames) {
        throw "Savestate scene transition produced " +
            "$($frameDiagnostics.Count) of $transitionFrames frames"
    }
    $usefulFrames = @(
        $frameDiagnostics |
            Where-Object { [uint64]$_.native_pica_draw_count -ne 0U }
    )
    $beforeReloadUsefulFrames = @(
        $frameDiagnostics[0..6] |
            Where-Object { [uint64]$_.native_pica_draw_count -ne 0U }
    )
    $afterReloadUsefulFrames = @(
        $frameDiagnostics[7..($frameDiagnostics.Count - 1)] |
            Where-Object { [uint64]$_.native_pica_draw_count -ne 0U }
    )
    $ownershipMismatches = @(
        $usefulFrames |
            Where-Object {
                [uint64]$_.nri_pica_owned_draw_count -ne
                    [uint64]$_.native_pica_draw_count -or
                -not $_.nri_pica_rendering_scope_owned
            }
    )
    $vulkanErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property vulkan_validation_error_count -Maximum
    ).Maximum
    $nriErrors = [uint64](
        $frameDiagnostics |
            Measure-Object -Property nri_validation_error_count -Maximum
    ).Maximum
    if ($beforeReloadUsefulFrames.Count -eq 0 -or
        $afterReloadUsefulFrames.Count -eq 0 -or
        $ownershipMismatches.Count -ne 0 -or
        $vulkanErrors -ne 0U -or $nriErrors -ne 0U) {
        throw "Savestate scene transition broke exact NRI ownership or validation"
    }

    $summary =
        Get-Content -LiteralPath $summaryPath -Raw |
        ConvertFrom-Json
    $initialPath = [IO.Path]::GetFullPath(
        [string]$summary.savestates.initial_load_path)
    $quickPath = [IO.Path]::GetFullPath(
        [string]$summary.savestates.quick_path)
    if ([uint64]$summary.savestates.load_count -ne 2U -or
        $initialPath -ne [IO.Path]::GetFullPath($transitionCheckpoint) -or
        $quickPath -eq $initialPath) {
        throw "Savestate scene transition did not reload the distinct quick checkpoint"
    }

    $cacaoPasses = [uint64](
        $frameDiagnostics |
            Measure-Object -Property cacao_pass_count -Sum
    ).Sum
    $hizPasses = [uint64](
        $frameDiagnostics |
            Measure-Object -Property hiz_reflection_count -Sum
    ).Sum
    $taaPasses = [uint64](
        $frameDiagnostics |
            Measure-Object -Property temporal_aa_pass_count -Sum
    ).Sum
    if ($cacaoPasses -eq 0U -or $hizPasses -eq 0U -or
        $taaPasses -eq 0U) {
        throw "Advanced effects did not survive the savestate scene transition"
    }

    return [pscustomobject]@{
        Frames = $frameDiagnostics.Count
        UsefulFrames = $usefulFrames.Count
        UsefulFramesBeforeReload = $beforeReloadUsefulFrames.Count
        UsefulFramesAfterReload = $afterReloadUsefulFrames.Count
        DrawCount = [uint64](
            $usefulFrames |
                Measure-Object -Property native_pica_draw_count -Sum
        ).Sum
        LoadCount = [uint64]$summary.savestates.load_count
        InitialCheckpoint = $initialPath
        ReloadCheckpoint = $quickPath
        CacaoCount = $cacaoPasses
        HiZCount = $hizPasses
        TaaCount = $taaPasses
    }
}

$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}
try {
    $authentic = Invoke-ValidationCase `
        -Mode "Authentic" -Build (-not $SkipBuild)
    $advanced = Invoke-ValidationCase `
        -Mode "Advanced" -Build $false
    $transition = Invoke-SavestateSceneTransitionCase

    [pscustomobject]@{
        Result = "PASS"
        AuthenticFrames = $authentic.Frames
        AdvancedFrames = $advanced.Frames
        AuthenticDrawCount = $authentic.DrawCount
        AdvancedDrawCount = $advanced.DrawCount
        TransitionFrames = $transition.Frames
        TransitionUsefulFrames = $transition.UsefulFrames
        TransitionUsefulFramesBeforeReload =
            $transition.UsefulFramesBeforeReload
        TransitionUsefulFramesAfterReload =
            $transition.UsefulFramesAfterReload
        TransitionDrawCount = $transition.DrawCount
        TransitionSavestateLoads = $transition.LoadCount
        TransitionInitialCheckpoint =
            $transition.InitialCheckpoint
        TransitionReloadCheckpoint =
            $transition.ReloadCheckpoint
        TransitionCacaoCount = $transition.CacaoCount
        TransitionHiZCount = $transition.HiZCount
        TransitionTaaCount = $transition.TaaCount
        VulkanWarnings = [Math]::Max(
            $authentic.VulkanWarnings,
            $advanced.VulkanWarnings)
        NriWarnings = [Math]::Max(
            $authentic.NriWarnings,
            $advanced.NriWarnings)
        AdvancedCacaoCount = $advanced.CacaoCount
        AdvancedCacaoNormalGuideCount =
            $advanced.CacaoNormalGuideCount
        AdvancedCacaoAmbientCompositeCount =
            $advanced.CacaoAmbientCompositeCount
        AdvancedCacaoAmbientGuideCount =
            $advanced.CacaoAmbientGuideCount
        AdvancedCacaoAmbientRgbGuideCount =
            $advanced.CacaoAmbientRgbGuideCount
        AdvancedCacaoAmbientFallbackGuideCount =
            $advanced.CacaoAmbientFallbackGuideCount
        AdvancedHiZCount = $advanced.HiZCount
        AdvancedTaaCount = $advanced.TaaCount
        AuthenticScreenshotSha256 =
            $authentic.ScreenshotSha256
        AdvancedScreenshotSha256 =
            $advanced.ScreenshotSha256
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
