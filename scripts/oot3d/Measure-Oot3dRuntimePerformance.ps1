[CmdletBinding()]
param(
    [string]$Executable =
        "F:\oot3dre_build\native-renderer-llvm\oot3d_native_game.exe",
    [string]$SourceGraphicsConfig =
        "I:\oot3dre_work\native_game\captures\overall_perf_20260726\p4_gpu_compaction_benchmark_config.json",
    [string]$OutputDirectory =
        "I:\oot3dre_work\native_game\captures\runtime_performance",
    [string]$LoadState =
        "I:\oot3dre_work\native_game\oot3d_native_savedata\quick.oot3dsav",
    [string]$InputTimeline =
        "I:\oot3dre_work\native_game\captures\grass_camera_perf_20260726\move_diagonal_absolute.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [ValidateSet("nri", "vulkan")]
    [string]$Renderer = "vulkan",
    [ValidateRange(1, 100)]
    [int]$RunCount = 5,
    [ValidateRange(2, 1000000)]
    [int]$Frames = 360,
    [ValidateRange(1, 999999)]
    [int]$WarmupFrames = 60,
    [ValidateRange(1, 16384)]
    [int]$Width = 2560,
    [ValidateRange(1, 16384)]
    [int]$Height = 1440,
    [ValidateRange(0, 10000)]
    [double]$MinimumMedianFramesPerSecond = 0,
    [ValidateRange(0, 10000)]
    [double]$MinimumRunFramesPerSecond = 0,
    [switch]$RequireWholeAotProduct
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($WarmupFrames -ge $Frames) {
    throw "WarmupFrames must be lower than Frames"
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$launcher = Join-Path $repoRoot "scripts\oot3d\Invoke-Oot3dNativeGame.ps1"
$resolvedExecutable = [System.IO.Path]::GetFullPath($Executable)
$resolvedSourceConfig = [System.IO.Path]::GetFullPath($SourceGraphicsConfig)
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$resolvedLoadState = [System.IO.Path]::GetFullPath($LoadState)
$resolvedInputTimeline = [System.IO.Path]::GetFullPath($InputTimeline)
$resolvedSaveDataDirectory =
    [System.IO.Path]::GetFullPath($SaveDataDirectory)
$topScreenConfig = Join-Path $repoRoot "config\topscreen_ui.example.json"
$topScreenTextureOverrides = Join-Path $repoRoot `
    "tools\oot3d\ui_topscreen\assets\atlas_overrides.o3tu"

foreach ($path in @(
    $launcher,
    $resolvedExecutable,
    $resolvedSourceConfig,
    $resolvedLoadState,
    $resolvedInputTimeline,
    $topScreenConfig,
    $topScreenTextureOverrides
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required benchmark input is missing: $path"
    }
}
if(-not (Test-Path -LiteralPath $resolvedSaveDataDirectory `
        -PathType Container)) {
    throw "Required benchmark savedata directory is missing: $resolvedSaveDataDirectory"
}

New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force |
    Out-Null

$sourceConfigHash =
    (Get-FileHash -LiteralPath $resolvedSourceConfig -Algorithm SHA256).Hash
$benchmarkConfigPath =
    Join-Path $resolvedOutputDirectory "benchmark_graphics_config.json"
$graphicsConfig =
    Get-Content -LiteralPath $resolvedSourceConfig -Raw | ConvertFrom-Json
if ($null -eq $graphicsConfig.Graphics -or
    $null -eq $graphicsConfig.Graphics.Presentation) {
    throw "Graphics.Presentation is missing from $resolvedSourceConfig"
}

# Throughput must not be capped by the presentation queue. All other saved
# graphics features remain exactly as supplied by SourceGraphicsConfig.
$graphicsConfig.Graphics.Presentation.VSync = $false
$graphicsConfig | ConvertTo-Json -Depth 100 |
    Set-Content -LiteralPath $benchmarkConfigPath -Encoding utf8
$writtenConfig =
    Get-Content -LiteralPath $benchmarkConfigPath -Raw | ConvertFrom-Json
if ([bool]$writtenConfig.Graphics.Presentation.VSync) {
    throw "The derived benchmark configuration still enables VSync"
}

$fixedDeltaSeconds = 1.0 / 60.0
$runs = @()
for ($runIndex = 1; $runIndex -le $RunCount; ++$runIndex) {
    $runRoot = Join-Path $resolvedOutputDirectory `
        ("steady_state_run{0}" -f $runIndex)
    $outputPrefix = $resolvedOutputDirectory.TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    $resolvedRunRoot = [System.IO.Path]::GetFullPath($runRoot)
    if(-not $resolvedRunRoot.StartsWith(
            $outputPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Benchmark run directory escapes output root: $resolvedRunRoot"
    }
    if(Test-Path -LiteralPath $resolvedRunRoot) {
        [System.IO.Directory]::Delete($resolvedRunRoot, $true)
    }
    New-Item -ItemType Directory -Force -Path $resolvedRunRoot | Out-Null
    $runSaveData = Join-Path $resolvedRunRoot "savedata"
    Copy-Item -LiteralPath $resolvedSaveDataDirectory `
        -Destination $runSaveData -Recurse -Force
    $copiedCrashes = Join-Path $runSaveData "crashes"
    if(Test-Path -LiteralPath $copiedCrashes) {
        [System.IO.Directory]::Delete($copiedCrashes, $true)
    }
    $reportPath = Join-Path $resolvedOutputDirectory `
        ("steady_state_run{0}.json" -f $runIndex)
    & $launcher -SkipBuild -Executable $resolvedExecutable `
        -Renderer $Renderer -Frames $Frames `
        -BenchmarkWarmupFrames $WarmupFrames `
        -FixedDeltaSeconds $fixedDeltaSeconds `
        -SimulationRate 30 -PresentationRate 60 `
        -Width $Width -Height $Height `
        -GraphicsConfig $benchmarkConfigPath `
        -TopScreenConfig $topScreenConfig `
        -TopScreenTextureOverrides $topScreenTextureOverrides `
        -LoadState $resolvedLoadState `
        -InputTimeline $resolvedInputTimeline `
        -SaveDataDirectory $runSaveData `
        -Output $reportPath
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark run $runIndex failed with exit code $LASTEXITCODE"
    }

    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($null -eq $report.benchmark_window) {
        throw "Benchmark report is missing benchmark_window: $reportPath"
    }
    if ([bool]$report.benchmark_window.vsync) {
        throw "Benchmark run $runIndex unexpectedly used VSync"
    }
    if ([bool]$report.benchmark_window.pacing_enabled) {
        throw "Benchmark run $runIndex unexpectedly used software pacing"
    }
    if ([int]$report.benchmark_window.measured_frames -ne
        ($Frames - $WarmupFrames)) {
        throw "Benchmark run $runIndex measured the wrong frame window"
    }
    if ([int]$report.presentation_frames -ne $Frames -or
        [int]$report.run_frames -ne $Frames) {
        throw "Benchmark run $runIndex did not complete all requested frames"
    }
    if ([int]$report.frame_rate.guest_refreshes_dropped -ne 0) {
        throw "Benchmark run $runIndex dropped guest refreshes"
    }

    $strictCounters = [ordered]@{
        retained_arm_fallbacks =
            [uint64]$report.compiled_functions.retained_arm_fallbacks
        whole_aot_memory_faults =
            [uint64]$report.compiled_functions.whole_aot_memory_faults
        whole_aot_unsupported_exits =
            [uint64]$report.compiled_functions.whole_aot_unsupported_exits
        mass_aot_block_limit_exits =
            [uint64]$report.compiled_functions.mass_aot_block_limit_exits
    }
    if($RequireWholeAotProduct.IsPresent) {
        $strictFailures = @($strictCounters.GetEnumerator() |
            Where-Object { $_.Value -ne 0 })
        if($strictFailures.Count -ne 0 -or
           -not [bool]$report.compiled_functions.whole_aot_available -or
           -not [bool]$report.compiled_functions.whole_aot_enabled) {
            throw "Benchmark run $runIndex violated the whole-AOT product contract"
        }
    }
    $crashDumps = @(Get-ChildItem $copiedCrashes -Filter *.dmp `
        -ErrorAction SilentlyContinue)
    if($crashDumps.Count -ne 0) {
        throw "Benchmark run $runIndex produced $($crashDumps.Count) crash dump(s)"
    }

    $runs += [pscustomobject][ordered]@{
        run = $runIndex
        report = $reportPath
        frames_per_second =
            [double]$report.benchmark_window.frames_per_second
        host_seconds = [double]$report.benchmark_window.host_seconds
        measured_frames = [int]$report.benchmark_window.measured_frames
        game_updates =
            [int]$report.frame_rate.game_state_updates_observed
        guest_refreshes =
            [int]$report.frame_rate.guest_refreshes_scheduled
        guest_refreshes_dropped =
            [int]$report.frame_rate.guest_refreshes_dropped
        draws_submitted = [int]$report.draws_submitted
        memory_content_fingerprint =
            [string]$report.memory_content_fingerprint
        memory_state_fingerprint =
            [string]$report.memory_state_fingerprint
        process_state_fingerprint =
            [string]$report.process_state_fingerprint
        whole_aot_calls =
            [uint64]$report.compiled_functions.whole_aot_calls
        strict_counters = $strictCounters
    }
}

$sourceConfigHashAfter =
    (Get-FileHash -LiteralPath $resolvedSourceConfig -Algorithm SHA256).Hash
if ($sourceConfigHashAfter -ne $sourceConfigHash) {
    throw "The source graphics configuration changed during the benchmark"
}

$signatureGroups = @($runs | Group-Object `
    game_updates,guest_refreshes,guest_refreshes_dropped,draws_submitted,`
    memory_content_fingerprint,memory_state_fingerprint,`
    process_state_fingerprint)
if ($signatureGroups.Count -ne 1) {
    throw "Deterministic replay signatures differ across benchmark runs"
}

$sortedFps = @($runs.frames_per_second | Sort-Object)
$meanFps = ($sortedFps | Measure-Object -Average).Average
$middleIndex = [int][Math]::Floor($sortedFps.Count / 2)
$medianFps = if (($sortedFps.Count % 2) -eq 0) {
    ($sortedFps[$middleIndex - 1] + $sortedFps[$middleIndex]) / 2.0
} else {
    $sortedFps[$middleIndex]
}
$variance = 0.0
foreach ($fps in $sortedFps) {
    $variance += [Math]::Pow($fps - $meanFps, 2.0)
}
$standardDeviation = [Math]::Sqrt($variance / $sortedFps.Count)

$summaryPath = Join-Path $resolvedOutputDirectory `
    "steady_state_summary.json"
$summary = [ordered]@{
    schema = "oot3d.runtime_performance.steady_state.v1"
    measurement = [ordered]@{
        clock = "steady_clock"
        scope = "completed presentation frames after warm-up"
        process_startup_included = $false
        initial_savestate_load_included = $false
        warmup_included = $false
        vsync = $false
        software_pacing = $false
        fixed_delta_seconds = $fixedDeltaSeconds
        simulation_hz = 30
        presentation_contract_hz = 60
        frames_per_run = $Frames
        warmup_frames_per_run = $WarmupFrames
        measured_frames_per_run = $Frames - $WarmupFrames
        run_count = $RunCount
    }
    graphics = [ordered]@{
        source_config = $resolvedSourceConfig
        source_config_sha256 = $sourceConfigHash
        derived_config = $benchmarkConfigPath
        only_forced_setting = "Graphics.Presentation.VSync=false"
        renderer = $Renderer
        width = $Width
        height = $Height
    }
    result = [ordered]@{
        mean_frames_per_second = $meanFps
        median_frames_per_second = $medianFps
        minimum_frames_per_second = $sortedFps[0]
        maximum_frames_per_second = $sortedFps[-1]
        standard_deviation_frames_per_second = $standardDeviation
        median_frame_milliseconds = 1000.0 / $medianFps
        deterministic_replay = $true
        required_whole_aot_product = $RequireWholeAotProduct.IsPresent
        minimum_median_frames_per_second =
            $MinimumMedianFramesPerSecond
        minimum_run_frames_per_second = $MinimumRunFramesPerSecond
    }
    runs = $runs
}
$summary | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8

$thresholdFailures = @()
if($MinimumMedianFramesPerSecond -gt 0 -and
   $medianFps -lt $MinimumMedianFramesPerSecond) {
    $thresholdFailures += ("median:{0:N3}<{1:N3}" -f
        $medianFps, $MinimumMedianFramesPerSecond)
}
if($MinimumRunFramesPerSecond -gt 0 -and
   $sortedFps[0] -lt $MinimumRunFramesPerSecond) {
    $thresholdFailures += ("minimum:{0:N3}<{1:N3}" -f
        $sortedFps[0], $MinimumRunFramesPerSecond)
}
if($thresholdFailures.Count -ne 0) {
    throw "Runtime performance gate failed: $($thresholdFailures -join ', ')"
}

$runs |
    Select-Object run,frames_per_second,host_seconds,measured_frames,
        game_updates,draws_submitted |
    Format-Table -AutoSize
[pscustomobject]@{
    MedianFPS = $medianFps
    MeanFPS = $meanFps
    MinFPS = $sortedFps[0]
    MaxFPS = $sortedFps[-1]
    StandardDeviation = $standardDeviation
    Summary = $summaryPath
} | Format-List
