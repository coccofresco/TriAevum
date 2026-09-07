[CmdletBinding()]
param(
    [string]$Executable =
        "F:\oot3dre_build\native-renderer-llvm\oot3d_native_game_source_overlay.exe",
    [string]$Overlay =
        "I:\oot3dre_work\native_game\source_overlays\oot3d_source_overlay.dll",
    [string]$LoadState =
        "I:\oot3dre_work\native_game\oot3d_native_savedata\quick.oot3dsav",
    [string]$Output =
        "I:\oot3dre_work\native_game\source_overlay_game.json",
    [string]$SaveState = "",
    [int]$SaveStateFrame = -1,
    [int]$Frames = 0,
    [double]$FixedDeltaSeconds = 0,
    [double]$MaxSeconds = 0,
    [switch]$SkipOverlayBuild,
    [switch]$RunOverlayTests,
    [switch]$DisableAudio
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildScript = Join-Path $PSScriptRoot "Build-Oot3dSourceOverlay.ps1"
$invokeScript = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
$resolvedExecutable = [System.IO.Path]::GetFullPath($Executable)
$resolvedOverlay = [System.IO.Path]::GetFullPath($Overlay)

if (-not $SkipOverlayBuild) {
    if ($RunOverlayTests) {
        & $buildScript -RunTests
    } else {
        & $buildScript
    }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

foreach ($requiredFile in @($resolvedExecutable, $resolvedOverlay)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Source-overlay runtime input is missing: $requiredFile"
    }
}

$env:OOT3D_SOURCE_OVERLAY = $resolvedOverlay
$arguments = @{
    SkipBuild = $true
    Executable = $resolvedExecutable
    Renderer = "vulkan"
    UiProfile = "topscreen"
    GameplayTiming = "native30_no_interpolation"
    DisableTypedGameplay = $true
    DisableManualCompiledFunctions = $true
    DisableTrueAotBlocks = $true
    DisableMassAot = $true
    LoadState = $LoadState
    Output = $Output
}
if ($Frames -gt 0) {
    $arguments.Frames = $Frames
}
if ($FixedDeltaSeconds -gt 0) {
    $arguments.FixedDeltaSeconds = $FixedDeltaSeconds
}
if ($MaxSeconds -gt 0) {
    $arguments.MaxSeconds = $MaxSeconds
}
if ($DisableAudio) {
    $arguments.DisableAudio = $true
}
if (-not [string]::IsNullOrWhiteSpace($SaveState) -or
    $SaveStateFrame -ge 0) {
    if ([string]::IsNullOrWhiteSpace($SaveState) -or
        $SaveStateFrame -lt 0) {
        throw "SaveState and SaveStateFrame must be specified together"
    }
    $arguments.SaveState = $SaveState
    $arguments.SaveStateFrame = $SaveStateFrame
}

& $invokeScript @arguments
exit $LASTEXITCODE
