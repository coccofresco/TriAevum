[CmdletBinding()]
param(
    [switch]$Bootstrap,
    [switch]$SkipBuild,
    [switch]$Manual,
    [switch]$FromBoot,
    [switch]$BorderlessTransitionSmoke,
    [switch]$PresentationRollbackSmoke,
    [switch]$PresentationApplyFailureSmoke,
    [switch]$CacaoTransitionSmoke,
    [switch]$CacaoDisableTransitionSmoke,
    [switch]$RenderScaleTransitionSmoke,
    [switch]$UpscalerQualityTransitionSmoke,
    [switch]$SavestateReloadSmoke,
    [switch]$PicaMemoryFillSmoke,
    [switch]$ScreenshotSequence,
    [ValidateRange(2, 60)]
    [int]$ScreenshotSequenceLength = 4,
    [ValidateRange(1, 60)]
    [int]$ScreenshotInterval = 1,
    [ValidateRange(1, 10000)]
    [int]$Frames = 90,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 60,
    [ValidateRange(1, 32)]
    [int]$Parallel = 2,
    [string]$InputTimeline = "",
    [string]$ProjectRoot = "",
    [string]$BuildDirectory = "",
    [string]$Checkpoint =
        "I:\oot3dre_work\native_game\oot3d_native_savedata\quick.oot3dsav",
    [string]$VcpkgRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [IO.Path]::GetFullPath((Join-Path $rendererRoot "..\.."))
} else {
    $ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
}
$projectBuildScript = Join-Path $ProjectRoot "tools\oot3d\build_fast_dev.ps1"
$projectLauncher = Join-Path $ProjectRoot "scripts\oot3d\Invoke-Oot3dNativeGame.ps1"
if (-not (Test-Path -LiteralPath $projectBuildScript -PathType Leaf) -or
    -not (Test-Path -LiteralPath $projectLauncher -PathType Leaf)) {
    throw "ProjectRoot is not an Oot3dRecomp checkout: $ProjectRoot"
}

$devRoot = Join-Path $rendererRoot ".renderer-dev"
$buildRoot = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $devRoot "build"
} else {
    [IO.Path]::GetFullPath($BuildDirectory)
}
$artifactRoot = Join-Path $devRoot "artifacts"
New-Item -ItemType Directory -Force -Path $devRoot | Out-Null
if ($Manual -and $ScreenshotSequence) {
    throw "ScreenshotSequence is available only for bounded automated runs"
}
if ($Bootstrap) {
    Write-Host "Renderer harness initialized at $devRoot"
}

if (-not $SkipBuild) {
    if (-not [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        if (-not (Test-Path -LiteralPath $VcpkgRoot -PathType Container)) {
            throw "Vcpkg root is missing: $VcpkgRoot"
        }
        $env:VCPKG_ROOT = [IO.Path]::GetFullPath($VcpkgRoot)
    }
    & $projectBuildScript -Target oot3d_native_game `
        -BuildDirectory $buildRoot -Parallel $Parallel `
        -Configure:$Bootstrap -AllowBroadRebuild:$Bootstrap
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$executable = Join-Path $buildRoot "oot3d_native_game.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Dedicated native-game executable is missing; rerun with -Bootstrap"
}
if (-not $FromBoot -and
    -not (Test-Path -LiteralPath $Checkpoint -PathType Leaf)) {
    throw "Playable-scene checkpoint is missing: $Checkpoint"
}

$processManifest = Join-Path $buildRoot "oot3d_native_process_manifest.json"
if (-not (Test-Path -LiteralPath $processManifest -PathType Leaf)) {
    $manifestBuilder = Join-Path $ProjectRoot `
        "tools\oot3d\native_a32_runtime\build_process_manifest.py"
    & python $manifestBuilder --output $processManifest
    if ($LASTEXITCODE -ne 0) { throw "Unable to create the dedicated A32 process manifest" }
}

New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
$env:OOT3D_VULKAN_DIAGNOSTICS_PATH =
    Join-Path $artifactRoot "vulkan-diagnostics.json"
$env:OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES = "240"
if ($BorderlessTransitionSmoke -or
    $PresentationRollbackSmoke -or
    $PresentationApplyFailureSmoke) {
    $env:OOT3D_GRAPHICS_TEST_BORDERLESS_AFTER_FRAMES = "4"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_BORDERLESS_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($PresentationRollbackSmoke) {
    $env:OOT3D_GRAPHICS_TEST_PRESENTATION_ROLLBACK_AFTER_FRAMES = "8"
} else {
    Remove-Item `
        Env:OOT3D_GRAPHICS_TEST_PRESENTATION_ROLLBACK_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($PresentationApplyFailureSmoke) {
    $env:OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY = "1"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY `
        -ErrorAction SilentlyContinue
}
if ($CacaoTransitionSmoke) {
    $env:OOT3D_GRAPHICS_TEST_CACAO_AFTER_FRAMES = "4"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_CACAO_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($CacaoDisableTransitionSmoke) {
    $env:OOT3D_GRAPHICS_CACAO = "1"
    $env:OOT3D_GRAPHICS_TEST_CACAO_OFF_AFTER_FRAMES = "40"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_CACAO_OFF_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($RenderScaleTransitionSmoke) {
    $env:OOT3D_GRAPHICS_TEST_RENDER_SCALE_AFTER_FRAMES = "20"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_RENDER_SCALE_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($UpscalerQualityTransitionSmoke) {
    $env:OOT3D_GRAPHICS_TEST_UPSCALER_QUALITY_AFTER_FRAMES = "20"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_UPSCALER_QUALITY_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($SavestateReloadSmoke) {
    $env:OOT3D_GRAPHICS_TEST_RELOAD_SAVESTATE_AFTER_FRAMES = "6"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_RELOAD_SAVESTATE_AFTER_FRAMES `
        -ErrorAction SilentlyContinue
}
if ($PicaMemoryFillSmoke) {
    $env:OOT3D_GRAPHICS_TEST_PICA_MEMORY_FILL = "1"
} else {
    Remove-Item Env:OOT3D_GRAPHICS_TEST_PICA_MEMORY_FILL `
        -ErrorAction SilentlyContinue
}

$launcher = $projectLauncher
$launch = @{
    SkipBuild = $true
    Executable = $executable
    A32ProcessManifest = $processManifest
    Renderer = "nri"
    UiProfile = "oot3d"
    Output = (Join-Path $artifactRoot "native-game-summary.json")
    Screenshot = (Join-Path $artifactRoot "native-game.bmp")
    ExtendedDiagnostics = $true
    DisableAudio = $true
    SimulationRate = 30
    PresentationRate = "60"
}
if (-not $FromBoot) {
    $launch.LoadState = [IO.Path]::GetFullPath($Checkpoint)
}
if (-not [string]::IsNullOrWhiteSpace($InputTimeline)) {
    $launch.InputTimeline = [IO.Path]::GetFullPath($InputTimeline)
}
if (-not $Manual) {
    $launch.Frames = $Frames
    $launch.MaxSeconds = $MaxSeconds
    if ($ScreenshotSequence) {
        $sequenceSpan =
            ($ScreenshotSequenceLength - 1) * $ScreenshotInterval
        $screenshotStartFrame = $Frames - 1 - $sequenceSpan
        if ($screenshotStartFrame -lt 1) {
            throw "Frames must leave room for the requested screenshot sequence"
        }
        $launch.ScreenshotSequence = $true
        $launch.ScreenshotStartFrame = $screenshotStartFrame
        $launch.ScreenshotInterval = $ScreenshotInterval
    } else {
        $launch.ScreenshotStartFrame = [Math]::Max(1, $Frames - 2)
    }
}

Write-Host "Renderer source: $rendererRoot"
Write-Host "Project harness: $ProjectRoot"
Write-Host "UI profile: oot3d (TopScreen disabled)"
if (-not $Manual -and
    (Test-Path -LiteralPath $launch.Screenshot -PathType Leaf)) {
    Remove-Item -LiteralPath $launch.Screenshot -Force
}
if (-not $Manual -and $ScreenshotSequence) {
    $screenshotDirectory =
        [IO.Path]::GetDirectoryName($launch.Screenshot)
    $screenshotStem =
        [IO.Path]::GetFileNameWithoutExtension($launch.Screenshot)
    Get-ChildItem -LiteralPath $screenshotDirectory -File `
        -Filter "$($screenshotStem)_*.bmp" |
        Remove-Item -Force
}
Push-Location $ProjectRoot
try {
    & $launcher @launch
    $launchExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($launchExitCode -eq 0 -and -not $Manual) {
    $screenshotValidator = Join-Path $rendererRoot `
        "tools\Test-Oot3dFramebufferArtifact.ps1"
    if ($ScreenshotSequence) {
        $sequenceScreenshots = @(
            Get-ChildItem -LiteralPath $screenshotDirectory -File `
                -Filter "$($screenshotStem)_*.bmp" |
                Sort-Object -Property Name
        )
        if ($sequenceScreenshots.Count -ne
            $ScreenshotSequenceLength) {
            throw "Renderer produced $($sequenceScreenshots.Count) of " +
                "$ScreenshotSequenceLength sequence screenshots"
        }
        foreach ($screenshot in $sequenceScreenshots) {
            & $screenshotValidator -Path $screenshot.FullName |
                Out-Null
        }
    } else {
        & $screenshotValidator -Path $launch.Screenshot | Out-Null
    }
}
exit $launchExitCode
