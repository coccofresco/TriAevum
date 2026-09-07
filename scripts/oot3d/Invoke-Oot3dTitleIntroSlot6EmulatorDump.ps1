param(
    [string]$OutputRoot = "I:\oot3dre\captures\azahar_pica",
    [string]$CaptureName = "",
    [string]$AzaharExe = "E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [int]$WarmupSeconds = 0,
    [int]$Frames = 6,
    [int]$MaxVerticesPerDraw = 65536,
    [int]$TimeoutSeconds = 90,
    [int]$PostCaptureTraceSeconds = 12,
    [int]$ScreenshotExtraDelayMs = 500,
    [int]$TriggerExtraDelayMs = 0,
    [int]$WriterTraceMaxRows = 1000000,
    [int]$ActorDrawTraceMaxRows = 2048,
    [int]$SkelDrawTraceMaxRows = 4096,
    [string]$DemoStatsPath = "I:\oot3dre\build-codex\title_intro_frame13_frames1_slot6_pica_match_after_gate_run.json",
    [string]$SeedWatchList = "I:\oot3dre\captures\azahar_pica\title_intro_slot6_initial_20260706_092540\derived\watch_addresses.from_stable_frame.txt",
    [string]$FocusedWatchList = "I:\oot3dre\captures\azahar_pica\title_intro_slot6_kankyo_pica_validation_20260707_081821\derived\watch_addresses.title_intro_kankyo_focused.txt",
    [switch]$Focused,
    [switch]$DisableWriterTrace,
    [switch]$DisableActorDrawTrace,
    [switch]$DisableSkelDrawTrace,
    [switch]$PreserveSeedWatchList,
    [switch]$SkipSummary,
    [switch]$SkipComparison
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$baseScript = Join-Path $PSScriptRoot "Invoke-Oot3dKokiriSlot5EmulatorDump.ps1"
if (-not (Test-Path -LiteralPath $baseScript)) {
    throw "Base emulator dump script not found: $baseScript"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$summaryScript = Join-Path $repoRoot "tools\oot3d\decomp_support\scripts\summarize_title_intro_slot6_pica_dump.py"
$compareScript = Join-Path $repoRoot "tools\oot3d\decomp_support\scripts\compare_title_intro_slot6_demo_to_pica_dump.py"

if ($Focused.IsPresent -and
    -not $PSBoundParameters.ContainsKey("SeedWatchList") -and
    (Test-Path -LiteralPath $FocusedWatchList)) {
    $SeedWatchList = $FocusedWatchList
    if (-not $PSBoundParameters.ContainsKey("Frames")) {
        $Frames = 3
    }
    if (-not $PSBoundParameters.ContainsKey("PostCaptureTraceSeconds")) {
        $PostCaptureTraceSeconds = 8
    }
}

if ([string]::IsNullOrWhiteSpace($CaptureName)) {
    $prefix = if ($Focused.IsPresent) {
        "title_intro_slot6_kankyo_focused"
    } else {
        "title_intro_slot6_kankyo_pica_validation"
    }
    $CaptureName = "{0}_{1}" -f $prefix, (Get-Date).ToString("yyyyMMdd_HHmmss")
}

$preserve = $PreserveSeedWatchList.IsPresent

$args = @{
    OutputRoot = $OutputRoot
    CaptureName = $CaptureName
    AzaharExe = $AzaharExe
    RomPath = $RomPath
    Slot = 6
    WarmupSeconds = $WarmupSeconds
    Frames = $Frames
    MaxVerticesPerDraw = $MaxVerticesPerDraw
    TimeoutSeconds = $TimeoutSeconds
    PostCaptureTraceSeconds = $PostCaptureTraceSeconds
    ScreenshotExtraDelayMs = $ScreenshotExtraDelayMs
    TriggerExtraDelayMs = $TriggerExtraDelayMs
    WriterTraceMaxRows = $WriterTraceMaxRows
    ActorDrawTraceMaxRows = $ActorDrawTraceMaxRows
    SkelDrawTraceMaxRows = $SkelDrawTraceMaxRows
    SeedWatchList = $SeedWatchList
    PreserveSeedWatchList = $preserve
    DisableWriterTrace = $DisableWriterTrace.IsPresent
    DisableActorDrawTrace = $DisableActorDrawTrace.IsPresent
    DisableSkelDrawTrace = $DisableSkelDrawTrace.IsPresent
}

& $baseScript @args

$captureDir = Join-Path $OutputRoot $CaptureName
$derivedDir = Join-Path $captureDir "derived"
$picaSummaryPath = Join-Path $derivedDir "title_intro_slot6_pica_dump_summary.json"
$gapPath = Join-Path $derivedDir "title_intro_slot6_demo_vs_emulator_gap.json"
$gapMarkdownPath = Join-Path $derivedDir "title_intro_slot6_demo_vs_emulator_gap.md"

if (-not $SkipSummary.IsPresent) {
    if (-not (Test-Path -LiteralPath $summaryScript)) {
        throw "Title intro PICA summary script not found: $summaryScript"
    }
    $frameIndex = if ($Frames -gt 1) { 1 } else { 0 }
    & python $summaryScript $captureDir --frame-index $frameIndex --output $picaSummaryPath
}

if (-not $SkipComparison.IsPresent -and (Test-Path -LiteralPath $DemoStatsPath)) {
    if (-not (Test-Path -LiteralPath $compareScript)) {
        throw "Title intro demo/emulator compare script not found: $compareScript"
    }
    & python $compareScript --emulator-summary $picaSummaryPath --demo-stats $DemoStatsPath `
        --output $gapPath --markdown-output $gapMarkdownPath
} elseif (-not $SkipComparison.IsPresent) {
    Write-Warning "Demo stats not found; skipped title intro demo/emulator comparison: $DemoStatsPath"
}
