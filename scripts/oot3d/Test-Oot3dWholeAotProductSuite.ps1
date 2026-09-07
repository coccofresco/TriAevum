[CmdletBinding()]
param(
    [string]$OracleExecutable =
        "I:\oot3dre_work\native_game\captures\overall_perf_20260726\binaries\baseline_61593d357_82caab12\oot3d_native_game.exe",
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$OutputDirectory =
        "I:\oot3dre_work\whole-aot-product-run\equivalence-suite",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [string]$KokiriCutsceneState =
        "I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav",
    [string]$KokiriGameplayState =
        "I:\oot3dre_work\native_game\checkpoints\hudtest_after120.oot3dsav",
    [string]$GameplayInputTimeline =
        "I:\oot3dre_work\native_game\captures\grass_camera_perf_20260726\move_diagonal_absolute.json",
    [switch]$ReuseExisting
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$harness = Join-Path $PSScriptRoot "Test-Oot3dWholeAotProduct.ps1"
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
foreach($required in @(
        $harness,
        $OracleExecutable,
        $ProductExecutable,
        $GraphicsConfig,
        $KokiriCutsceneState,
        $KokiriGameplayState,
        $GameplayInputTimeline)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required whole-AOT suite input is missing: $required"
    }
}

$scenarios = @(
    [ordered]@{
        name = "boot_title"
        frames = 901
        interval = 100
        load_state = ""
        input_timeline = ""
        compare_audio = $false
    },
    [ordered]@{
        name = "kokiri_cutscene"
        frames = 481
        interval = 60
        load_state = $KokiriCutsceneState
        input_timeline = ""
        compare_audio = $true
    },
    [ordered]@{
        name = "kokiri_gameplay_input"
        frames = 61
        interval = 30
        load_state = $KokiriGameplayState
        input_timeline = $GameplayInputTimeline
        compare_audio = $true
    }
)

New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null
$results = @()
foreach($scenario in $scenarios) {
    $scenarioOutput = Join-Path $resolvedOutput $scenario.name
    $reportPath = Join-Path $scenarioOutput "equivalence.json"
    $arguments = @{
        OracleExecutable = $OracleExecutable
        ProductExecutable = $ProductExecutable
        OutputDirectory = $scenarioOutput
        GraphicsConfig = $GraphicsConfig
        SaveDataDirectory = $SaveDataDirectory
        Frames = $scenario.frames
        CheckpointInterval = $scenario.interval
    }
    if(-not [string]::IsNullOrWhiteSpace($scenario.load_state)) {
        $arguments["LoadState"] = $scenario.load_state
    }
    if(-not [string]::IsNullOrWhiteSpace($scenario.input_timeline)) {
        $arguments["InputTimeline"] = $scenario.input_timeline
    }
    if($scenario.compare_audio) {
        $arguments["CompareAudio"] = $true
    }

    if(-not $ReuseExisting.IsPresent -or
       -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        & $harness @arguments | Out-Host
        if($LASTEXITCODE -ne 0) {
            throw "Whole-AOT suite scenario failed: $($scenario.name)"
        }
    } else {
        Write-Host "Reusing whole-AOT scenario report: $reportPath"
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if([uint64]$report.frames -ne [uint64]$scenario.frames -or
       [uint64]$report.checkpoint_interval -ne [uint64]$scenario.interval) {
        throw "Existing whole-AOT scenario report has the wrong contract: $reportPath"
    }
    $results += [ordered]@{
        name = $scenario.name
        report = $reportPath
        frames = [uint64]$report.frames
        checkpoints = [uint64]$report.checkpoint_count
        framebuffer_equivalent =
            [bool]$report.exact_framebuffer_equivalence
        audio_requested = [bool]$report.audio.requested
        audio_equivalent =
            -not [bool]$report.audio.requested -or [bool]$report.audio.identical
        strict_runtime = [bool]$report.strict_runtime
        whole_aot_calls = [uint64]$report.product_whole_aot_calls
        direct_calls = [uint64]$report.product_direct_calls
        indirect_calls = [uint64]$report.product_indirect_calls
        external_calls = [uint64]$report.product_external_calls
    }
}

$failed = @($results | Where-Object {
    -not [bool]$_['framebuffer_equivalent'] -or
    -not [bool]$_['audio_equivalent'] -or
    -not [bool]$_['strict_runtime']
})
$totals = [ordered]@{
    checkpoints = [uint64]0
    whole_aot_calls = [uint64]0
    direct_calls = [uint64]0
    indirect_calls = [uint64]0
    external_calls = [uint64]0
}
foreach($result in $results) {
    $totals.checkpoints += [uint64]$result['checkpoints']
    $totals.whole_aot_calls += [uint64]$result['whole_aot_calls']
    $totals.direct_calls += [uint64]$result['direct_calls']
    $totals.indirect_calls += [uint64]$result['indirect_calls']
    $totals.external_calls += [uint64]$result['external_calls']
}
$summary = [ordered]@{
    format = "oot3d_whole_aot_product_equivalence_suite_v1"
    passed = $failed.Count -eq 0
    scenario_count = $results.Count
    checkpoint_count = $totals.checkpoints
    product_whole_aot_calls = $totals.whole_aot_calls
    product_direct_calls = $totals.direct_calls
    product_indirect_calls = $totals.indirect_calls
    product_external_calls = $totals.external_calls
    scenarios = $results
}
$summaryPath = Join-Path $resolvedOutput "suite.json"
$summary | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ConvertTo-Json -Depth 8
if(-not $summary.passed) {
    throw "Whole-AOT equivalence suite failed; see $summaryPath"
}
