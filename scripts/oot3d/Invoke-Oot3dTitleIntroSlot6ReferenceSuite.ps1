param(
    [string]$OutputRoot = "I:\oot3dre\captures\azahar_pica\title_intro_slot6_reference_suites",
    [string]$SuiteName = "",
    [int[]]$WarmupSeconds = @(0, 6, 12, 18, 24, 36),
    [int]$Frames = 3,
    [int]$RetryCount = 3,
    [int]$TimeoutSeconds = 120,
    [string]$AzaharExe = "E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$captureScript = Join-Path $PSScriptRoot "Invoke-Oot3dTitleIntroSlot6EmulatorDump.ps1"
if (-not (Test-Path -LiteralPath $captureScript)) {
    throw "Title-intro slot-6 capture script not found: $captureScript"
}
if ($WarmupSeconds.Count -ne 6) {
    throw "The title-intro reference suite requires exactly six warmup checkpoints."
}
if ([string]::IsNullOrWhiteSpace($SuiteName)) {
    $SuiteName = "title_intro_slot6_six_point_{0}" -f (Get-Date).ToString("yyyyMMdd_HHmmss")
}

$suiteDir = Join-Path $OutputRoot $SuiteName
New-Item -ItemType Directory -Force -Path $suiteDir | Out-Null
$captures = @()

for ($index = 0; $index -lt $WarmupSeconds.Count; ++$index) {
    $warmup = [Math]::Max(0, $WarmupSeconds[$index])
    $captureName = "checkpoint_{0:D2}_after_{1:D3}s" -f $index, $warmup
    $captureArgs = @{
        OutputRoot = $suiteDir
        CaptureName = $captureName
        AzaharExe = $AzaharExe
        RomPath = $RomPath
        WarmupSeconds = $warmup
        Frames = [Math]::Max(1, $Frames)
        TimeoutSeconds = [Math]::Max($TimeoutSeconds, $warmup + 30)
        PostCaptureTraceSeconds = 0
        ScreenshotExtraDelayMs = 500
        WriterTraceMaxRows = 1
    }
    $captured = $false
    for ($attempt = 1; $attempt -le [Math]::Max(1, $RetryCount); ++$attempt) {
        try {
            & $captureScript @captureArgs -Focused -PreserveSeedWatchList `
                -SkipSummary -SkipComparison | Out-Host
            $captured = $true
            break
        } catch {
            Get-Process azahar -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
            if ($attempt -ge [Math]::Max(1, $RetryCount)) {
                throw
            }
            Write-Warning ("Checkpoint {0} attempt {1} failed; retrying: {2}" -f `
                $index, $attempt, $_.Exception.Message)
            Start-Sleep -Seconds 3
        }
    }
    if (-not $captured) {
        throw "Checkpoint $index was not captured."
    }

    $captureDir = Join-Path $suiteDir $captureName
    $summaryPath = Join-Path $captureDir "emulator_dump_summary.json"
    if (-not (Test-Path -LiteralPath $summaryPath)) {
        throw "Checkpoint summary was not produced: $summaryPath"
    }
    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    $captures += [ordered]@{
        index = $index
        warmup_seconds_after_savestate_load = $warmup
        capture_name = $captureName
        capture_dir = $captureDir
        summary_path = $summaryPath
        screenshot_path = [string]$summary.screenshot_path
        screenshot_exists = [bool]$summary.screenshot_exists
        pica_frame_count = [int]$summary.pica_frame_count
        pica_frames = @($summary.pica_frames | ForEach-Object { [string]$_.path })
        actor_draw_trace_path = [string]$summary.actor_draw_trace_path
        skel_draw_trace_path = [string]$summary.skel_draw_trace_path
        writer_trace_path = [string]$summary.writer_trace_path
    }
}

$indexPath = Join-Path $suiteDir "reference_suite.json"
$markdownPath = Join-Path $suiteDir "reference_suite.md"
$suite = [ordered]@{
    format = "oot3d_title_intro_slot6_six_point_reference_suite_v1"
    generated_at = (Get-Date).ToString("o")
    savestate_slot = 6
    azahar_exe = $AzaharExe
    rom_path = $RomPath
    checkpoint_count = $captures.Count
    captures = $captures
    evidence_role = "validation_only_not_runtime_input"
}
$suite | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $indexPath -Encoding ascii

$lines = @(
    "# OOT3D title-intro slot-6 reference suite",
    "",
    "Each checkpoint reloads savestate slot 6, advances for the listed wall-clock interval, then captures one screenshot and one or more complete PICA frames. These files are validation evidence only and are not runtime inputs.",
    "",
    "| # | Warmup | Screenshot | PICA dump |",
    "| -: | -: | --- | --- |"
)
foreach ($capture in $captures) {
    $relativeScreenshot = [IO.Path]::GetRelativePath($suiteDir, $capture.screenshot_path).Replace("\", "/")
    $relativePica = if ($capture.pica_frames.Count -gt 0) {
        [IO.Path]::GetRelativePath($suiteDir, $capture.pica_frames[0]).Replace("\", "/")
    } else {
        "missing"
    }
    $lines += "| {0} | {1}s | [{2}]({2}) | [{3}]({3}) |" -f `
        $capture.index, $capture.warmup_seconds_after_savestate_load, $relativeScreenshot, $relativePica
}
$lines | Set-Content -LiteralPath $markdownPath -Encoding ascii

Write-Host "Reference suite complete: $suiteDir"
Write-Host "Index: $indexPath"
Write-Host "Markdown: $markdownPath"
