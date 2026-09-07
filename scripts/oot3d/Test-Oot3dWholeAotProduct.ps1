[CmdletBinding()]
param(
    [string]$OracleExecutable =
        "I:\oot3dre_work\native_game\captures\overall_perf_20260726\binaries\baseline_61593d357_82caab12\oot3d_native_game.exe",
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$OutputDirectory =
        "I:\oot3dre_work\whole-aot-product-run\equivalence",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [string]$LoadState = "",
    [string]$InputTimeline = "",
    [ValidateRange(2, 1000000)]
    [int]$Frames = 901,
    [ValidateRange(1, 1000000)]
    [int]$CheckpointInterval = 100,
    [ValidateRange(1, 16384)]
    [int]$Width = 1280,
    [ValidateRange(1, 16384)]
    [int]$Height = 720,
    [switch]$CompareAudio,
    [switch]$EnableAudio
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$resolvedGraphics = [System.IO.Path]::GetFullPath($GraphicsConfig)
$resolvedSaveData = [System.IO.Path]::GetFullPath($SaveDataDirectory)

foreach($required in @(
        $launcher,
        $OracleExecutable,
        $ProductExecutable,
        $resolvedGraphics)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required whole-AOT equivalence input is missing: $required"
    }
}
if($CheckpointInterval -ge $Frames) {
    throw "CheckpointInterval must be lower than Frames"
}
$resolvedLoadState = ""
if(-not [string]::IsNullOrWhiteSpace($LoadState)) {
    $resolvedLoadState = [System.IO.Path]::GetFullPath($LoadState)
    if(-not (Test-Path -LiteralPath $resolvedLoadState -PathType Leaf)) {
        throw "Whole-AOT equivalence savestate is missing: $resolvedLoadState"
    }
}
$resolvedInputTimeline = ""
if(-not [string]::IsNullOrWhiteSpace($InputTimeline)) {
    $resolvedInputTimeline = [System.IO.Path]::GetFullPath($InputTimeline)
    if(-not (Test-Path -LiteralPath $resolvedInputTimeline -PathType Leaf)) {
        throw "Whole-AOT equivalence input timeline is missing: $resolvedInputTimeline"
    }
}

New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null
$templatePath = Join-Path $resolvedOutput "graphics-template.json"
$graphics = Get-Content -LiteralPath $resolvedGraphics -Raw | ConvertFrom-Json
$graphics.Window.Width = $Width
$graphics.Window.Height = $Height
$graphics.Window.PositionX = 100
$graphics.Window.PositionY = 100
$graphics.Window.Fullscreen.Enabled = $false
$graphics.Graphics.Output.Width = $Width
$graphics.Graphics.Output.Height = $Height
$graphics.Graphics.Presentation.VSync = $false
$graphics | ConvertTo-Json -Depth 100 |
    Set-Content -LiteralPath $templatePath -Encoding utf8

$executables = [ordered]@{
    oracle = [System.IO.Path]::GetFullPath($OracleExecutable)
    product = [System.IO.Path]::GetFullPath($ProductExecutable)
}
$runtimeDocuments = @{}

foreach($kind in $executables.Keys) {
    $runDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $resolvedOutput $kind))
    $outputPrefix = $resolvedOutput.TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    if(-not $runDirectory.StartsWith(
            $outputPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean run directory outside output root: $runDirectory"
    }
    New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
    Get-ChildItem -LiteralPath $runDirectory -Force -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force

    $runGraphics = Join-Path $runDirectory "graphics.json"
    Copy-Item -LiteralPath $templatePath -Destination $runGraphics -Force
    $runSaveData = Join-Path $runDirectory "savedata"
    if(Test-Path -LiteralPath $resolvedSaveData -PathType Container) {
        Copy-Item -LiteralPath $resolvedSaveData -Destination $runSaveData `
            -Recurse -Force
    } else {
        New-Item -ItemType Directory -Path $runSaveData | Out-Null
    }

    $runtimePath = Join-Path $runDirectory "runtime.json"
    $screenshotPath = Join-Path $runDirectory "frame.bmp"
    $arguments = @{
        SkipBuild = $true
        Executable = $executables[$kind]
        Renderer = "nri"
        Frames = $Frames
        FixedDeltaSeconds = 1.0 / 60.0
        SimulationRate = 30
        PresentationRate = "free"
        Width = $Width
        Height = $Height
        GraphicsConfig = $runGraphics
        SaveDataDirectory = $runSaveData
        Screenshot = $screenshotPath
        ScreenshotSequence = $true
        ScreenshotStartFrame = 0
        ScreenshotInterval = $CheckpointInterval
        Output = $runtimePath
    }
    if(-not $EnableAudio.IsPresent) {
        $arguments["DisableAudio"] = $true
    }
    if($CompareAudio.IsPresent) {
        $arguments["AudioPcmDump"] = Join-Path $runDirectory "audio.wav"
    }
    if(-not [string]::IsNullOrWhiteSpace($resolvedLoadState)) {
        $arguments["LoadState"] = $resolvedLoadState
    }
    if(-not [string]::IsNullOrWhiteSpace($resolvedInputTimeline)) {
        $arguments["InputTimeline"] = $resolvedInputTimeline
    }
    & $launcher @arguments
    if($LASTEXITCODE -ne 0) {
        throw "$kind runtime exited with code $LASTEXITCODE"
    }
    $runtimeDocuments[$kind] =
        Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json
}

$oracleDirectory = Join-Path $resolvedOutput "oracle"
$productDirectory = Join-Path $resolvedOutput "product"
$checkpoints = @()
Get-ChildItem -LiteralPath $oracleDirectory -Filter "frame_*.bmp" |
    Sort-Object Name |
    ForEach-Object {
        $productPath = Join-Path $productDirectory $_.Name
        if(-not (Test-Path -LiteralPath $productPath -PathType Leaf)) {
            throw "Product checkpoint is missing: $productPath"
        }
        $oracleHash =
            (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $productHash =
            (Get-FileHash -LiteralPath $productPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $checkpoints += [ordered]@{
            frame = [int]$_.BaseName.Substring(6)
            identical = $oracleHash -eq $productHash
            oracle_sha256 = $oracleHash
            product_sha256 = $productHash
        }
    }

$productRuntime = $runtimeDocuments.product
$audioComparison = [ordered]@{
    requested = $CompareAudio.IsPresent
    identical = $null
    oracle_sha256 = ""
    product_sha256 = ""
}
if($CompareAudio.IsPresent) {
    $oracleAudio = Join-Path $oracleDirectory "audio.wav"
    $productAudio = Join-Path $productDirectory "audio.wav"
    foreach($requiredAudio in @($oracleAudio, $productAudio)) {
        if(-not (Test-Path -LiteralPath $requiredAudio -PathType Leaf)) {
            throw "Audio comparison output is missing: $requiredAudio"
        }
    }
    $audioComparison.oracle_sha256 =
        (Get-FileHash -LiteralPath $oracleAudio -Algorithm SHA256).Hash.ToLowerInvariant()
    $audioComparison.product_sha256 =
        (Get-FileHash -LiteralPath $productAudio -Algorithm SHA256).Hash.ToLowerInvariant()
    $audioComparison.identical =
        $audioComparison.oracle_sha256 -eq $audioComparison.product_sha256
}
$strictFailures = @()
$strictCounters = [ordered]@{
    retained_arm_fallbacks =
        [uint64]$productRuntime.compiled_functions.retained_arm_fallbacks
    whole_aot_memory_faults =
        [uint64]$productRuntime.compiled_functions.whole_aot_memory_faults
    whole_aot_unsupported_exits =
        [uint64]$productRuntime.compiled_functions.whole_aot_unsupported_exits
    mass_aot_block_limit_exits =
        [uint64]$productRuntime.compiled_functions.mass_aot_block_limit_exits
}
foreach($entry in $strictCounters.GetEnumerator()) {
    if($entry.Value -ne 0) {
        $strictFailures += "$($entry.Key)=$($entry.Value)"
    }
}
if(-not [bool]$productRuntime.compiled_functions.whole_aot_available -or
   -not [bool]$productRuntime.compiled_functions.whole_aot_enabled) {
    $strictFailures += "whole_aot_not_enabled"
}
if([uint64]$productRuntime.run_frames -ne [uint64]$Frames) {
    $strictFailures += "run_frames=$($productRuntime.run_frames)"
}
if(-not [string]::IsNullOrWhiteSpace($resolvedInputTimeline)) {
    if(-not [bool]$productRuntime.hid_input.timeline_enabled) {
        $strictFailures += "input_timeline_not_enabled"
    }
    if([uint64]$productRuntime.hid_input.scripted_host_frames -eq 0 -or
       [uint64]$productRuntime.hid_input.active_segment_host_frames -eq 0) {
        $strictFailures += "input_timeline_not_consumed"
    }
}

$summary = [ordered]@{
    format = "oot3d_whole_aot_product_equivalence_v1"
    frames = $Frames
    checkpoint_interval = $CheckpointInterval
    load_state = $resolvedLoadState
    input_timeline = $resolvedInputTimeline
    checkpoint_count = $checkpoints.Count
    exact_framebuffer_equivalence =
        $checkpoints.Count -gt 0 -and
        @($checkpoints | Where-Object { -not $_.identical }).Count -eq 0
    audio = $audioComparison
    strict_runtime = $strictFailures.Count -eq 0
    strict_counters = $strictCounters
    product_whole_aot_calls =
        [uint64]$productRuntime.compiled_functions.whole_aot_calls
    product_direct_calls =
        [uint64]$productRuntime.compiled_functions.whole_aot_direct_calls
    product_indirect_calls =
        [uint64]$productRuntime.compiled_functions.whole_aot_indirect_calls
    product_external_calls =
        [uint64]$productRuntime.compiled_functions.whole_aot_external_calls
    performance_observation = [ordered]@{
        frames_per_second_with_checkpoint_readback =
            [double]$productRuntime.benchmark_window.frames_per_second
        suitable_for_throughput_comparison = $false
        reason = "synchronous framebuffer checkpoints perturb frame timing"
    }
    strict_failures = $strictFailures
    checkpoints = $checkpoints
}
$summaryPath = Join-Path $resolvedOutput "equivalence.json"
$summary | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ConvertTo-Json -Depth 8

if(-not $summary.exact_framebuffer_equivalence) {
    throw "Whole-AOT framebuffer equivalence failed; see $summaryPath"
}
if($CompareAudio.IsPresent -and -not $summary.audio.identical) {
    throw "Whole-AOT PCM equivalence failed; see $summaryPath"
}
if(-not $summary.strict_runtime) {
    throw "Whole-AOT strict runtime validation failed; see $summaryPath"
}
