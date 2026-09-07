[CmdletBinding()]
param(
    [ValidateRange(0, 1000000)]
    [int]$Frames = 0,
    [double]$MaxSeconds = 0,
    [double]$FixedDeltaSeconds = 0,
    [ValidateSet("native30_interpolated", "native30_no_interpolation", "enhanced60")]
    [string]$GameplayTiming = "native30_interpolated",
    [string]$PresentationRate = "60",
    [ValidateRange(1, 16384)]
    [int]$Width = 1280,
    [ValidateRange(1, 16384)]
    [int]$Height = 720,
    [string]$LoadState = "",
    [string]$InputTimeline = "",
    [string]$Output = "",
    [string]$Screenshot = "",
    [ValidateRange(0, 1000000)]
    [int]$ScreenshotStartFrame = 0,
    [switch]$DisablePicaAotShaders,
    [switch]$PicaAotShaderStrict,
    [switch]$DisableAudio
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = [System.IO.Path]::GetFullPath(
    (Split-Path -Parent $MyInvocation.MyCommand.Path))
$executable = Join-Path $root "oot3d_native_game.exe"
$manifest = Join-Path $root "game\oot3d_native_process_manifest.json"
$resourceRoot = Join-Path $root "resources"
$graphics = Join-Path $root "config\oot3d_native_game.json"
$controls = Join-Path $root "config\oot3d_controls.json"
$topScreen = Join-Path $root "config\topscreen_ui.json"
$topScreenOverrides = Join-Path $root "config\atlas_overrides.o3tu"
$saveData = Join-Path $root "savedata"
$quickState = Join-Path $saveData "quick.oot3dsav"
$picaAotShaderPack = Join-Path $root "oot3d_pica_default.o3ps"

foreach($required in @(
        $executable,
        $manifest,
        $resourceRoot,
        $graphics,
        $controls,
        $topScreen,
        $topScreenOverrides)) {
    if(-not (Test-Path -LiteralPath $required)) {
        throw "Whole-AOT product file is missing: $required"
    }
}
New-Item -ItemType Directory -Force -Path $saveData | Out-Null

$arguments = @(
    "--a32-process-manifest", $manifest,
    "--resource-root", $resourceRoot,
    "--config", $graphics,
    "--controls-config", $controls,
    "--ui-profile", "topscreen",
    "--topscreen-config", $topScreen,
    "--topscreen-texture-overrides", $topScreenOverrides,
    "--save-data", $saveData,
    "--quick-state", $quickState,
    "--renderer", "nri",
    "--gameplay-timing", $GameplayTiming,
    "--presentation-rate", $PresentationRate,
    "--width", [string]$Width,
    "--height", [string]$Height
)
if($Frames -gt 0) {
    $arguments += @("--frames", [string]$Frames)
}
if($MaxSeconds -gt 0) {
    $arguments += @("--max-seconds", [string]$MaxSeconds)
}
if($FixedDeltaSeconds -gt 0) {
    $arguments += @(
        "--fixed-delta-seconds",
        [string]::Format(
            [Globalization.CultureInfo]::InvariantCulture,
            "{0:R}",
            $FixedDeltaSeconds))
}
if(-not [string]::IsNullOrWhiteSpace($LoadState)) {
    $resolvedLoadState = [System.IO.Path]::GetFullPath($LoadState)
    if(-not (Test-Path -LiteralPath $resolvedLoadState -PathType Leaf)) {
        throw "Savestate is missing: $resolvedLoadState"
    }
    $arguments += @("--load-state", $resolvedLoadState)
}
if(-not [string]::IsNullOrWhiteSpace($InputTimeline)) {
    $resolvedTimeline = [System.IO.Path]::GetFullPath($InputTimeline)
    if(-not (Test-Path -LiteralPath $resolvedTimeline -PathType Leaf)) {
        throw "Input timeline is missing: $resolvedTimeline"
    }
    $arguments += @("--input-timeline", $resolvedTimeline)
}
if([string]::IsNullOrWhiteSpace($Output) -and $Frames -gt 0) {
    $Output = Join-Path $root "logs\runtime.json"
}
if(-not [string]::IsNullOrWhiteSpace($Output)) {
    $resolvedOutput = [System.IO.Path]::GetFullPath($Output)
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $resolvedOutput) | Out-Null
    $arguments += @("--output", $resolvedOutput)
}
if(-not [string]::IsNullOrWhiteSpace($Screenshot)) {
    $resolvedScreenshot = [System.IO.Path]::GetFullPath($Screenshot)
    New-Item -ItemType Directory -Force -Path (
        Split-Path -Parent $resolvedScreenshot) | Out-Null
    $arguments += @(
        "--screenshot", $resolvedScreenshot,
        "--screenshot-start-frame", [string]$ScreenshotStartFrame)
}
if($DisableAudio.IsPresent) {
    $arguments += "--disable-audio"
}
if(-not $DisablePicaAotShaders.IsPresent -and
   (Test-Path -LiteralPath $picaAotShaderPack -PathType Leaf)) {
    $arguments += @("--pica-aot-shader-pack", $picaAotShaderPack)
    if($PicaAotShaderStrict.IsPresent) {
        $arguments += "--pica-aot-shader-strict"
    }
} elseif($PicaAotShaderStrict.IsPresent) {
    throw "PicaAotShaderStrict requires the packaged PICA AOT shader pack"
}

Push-Location $root
try {
    & $executable @arguments
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $exitCode
