param(
    [string]$OutputRoot = "I:\oot3dre\captures\azahar_pica",
    [string]$CaptureName = "",
    [string]$AzaharExe = "E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [int]$TriggerAfterMs = 4500,
    [int]$Frames = 8,
    [int]$MaxVerticesPerDraw = 65536,
    [int]$TraceMaxRows = 100000,
    [switch]$TraceAllPackets,
    [string]$WriterWatchlist = "",
    [int]$WriterTraceMaxRows = 50000,
    [int]$FallbackCaptureAfterMs = 75000,
    [int]$TimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$convertScript = Join-Path $PSScriptRoot "Convert-Oot3dNativePicaTrace.ps1"
$analyzeScript = Join-Path $repoRoot "tools\oot3d\decomp_support\scripts\analyze_title_intro_slot6_lens_dump.py"

function Assert-InputFile {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Set-CaptureEnvironment {
    param([hashtable]$Previous, [string]$Name, [AllowNull()][string]$Value)
    if (-not $Previous.ContainsKey($Name)) {
        $Previous[$Name] = [Environment]::GetEnvironmentVariable($Name, "Process")
    }
    [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

function Restore-CaptureEnvironment {
    param([hashtable]$Previous)
    foreach ($name in $Previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $Previous[$name], "Process")
    }
}

function Test-PicaCaptureComplete {
    param([System.IO.FileInfo]$Path)
    try {
        return [bool]((Get-Content -LiteralPath $Path.FullName -Tail 1 -ErrorAction Stop) -match '"event"\s*:\s*"capture_end"')
    } catch {
        return $false
    }
}

Assert-InputFile -Path $AzaharExe -Label "Instrumented Azahar executable"
Assert-InputFile -Path $RomPath -Label "OOT3D image"
Assert-InputFile -Path $convertScript -Label "PICA trace converter"
Assert-InputFile -Path $analyzeScript -Label "Lens dump analyzer"

if ([string]::IsNullOrWhiteSpace($CaptureName)) {
    $CaptureName = "title_intro_slot6_lens_runtime_{0}" -f (Get-Date).ToString("yyyyMMdd_HHmmss")
}

$captureDir = Join-Path $OutputRoot $CaptureName
$derivedDir = Join-Path $captureDir "derived"
$tracePath = Join-Path $captureDir "oot3d_kankyo_lens_runtime_trace.jsonl"
$picaTriggerPath = Join-Path $captureDir "pica_capture.trigger"
$screenshotTriggerPath = Join-Path $captureDir "screenshot_capture.trigger"
$screenshotPath = Join-Path $captureDir "emulator_slot6_native_lens.png"
$summaryPath = Join-Path $captureDir "lens_dump_summary.json"
$writerTracePath = Join-Path $captureDir "oot3d_cloud_sun_writer_trace.csv"
$analysisJsonPath = Join-Path $derivedDir "title_intro_slot6_lens_dump_analysis.json"
$analysisMarkdownPath = Join-Path $derivedDir "title_intro_slot6_lens_dump_analysis.md"

New-Item -ItemType Directory -Force -Path $captureDir, $derivedDir | Out-Null
Remove-Item -LiteralPath $tracePath, $picaTriggerPath, $screenshotTriggerPath, $screenshotPath,
    $summaryPath, $writerTracePath, $analysisJsonPath, $analysisMarkdownPath -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $captureDir -Filter "oot3d_pica_frame_*.jsonl" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
Get-ChildItem -LiteralPath $derivedDir -Filter "oot3d_pica_frame_*.native_pica_register_trace.json" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

$oldEnvironment = @{}
$process = $null
$frameFiles = @()
$convertedTraces = @()
$timedOut = $false
$fallbackTriggered = $false
$startedAt = $null
$nativeTriggerKind = ""

try {
    Set-CaptureEnvironment $oldEnvironment "OOT3D_AUTO_LOAD_STATE_SLOT" "6"
    Set-CaptureEnvironment $oldEnvironment "OOT3D_AUTO_SCREENSHOT_PATH" $screenshotPath
    Set-CaptureEnvironment $oldEnvironment "OOT3D_AUTO_SCREENSHOT_TRIGGER" $screenshotTriggerPath
    Set-CaptureEnvironment $oldEnvironment "OOT3D_AUTO_SCREENSHOT_DELAY_MS" $null
    Set-CaptureEnvironment $oldEnvironment "OOT3D_AUTO_EXIT_AFTER_SCREENSHOT_MS" $null
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE" "1"
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_OUTPUT" $tracePath
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_MAX_ROWS" ([string][Math]::Max(1, $TraceMaxRows))
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_TRIGGER_AFTER_MS" ([string][Math]::Max(0, $TriggerAfterMs))
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_TRIGGER_TERMINAL" "1"
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_ALL_PACKETS" $(if ($TraceAllPackets) { "1" } else { "0" })
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_PICA_TRIGGER" $picaTriggerPath
    Set-CaptureEnvironment $oldEnvironment "OOT3D_KANKYO_LENS_TRACE_SCREENSHOT_TRIGGER" $screenshotTriggerPath
    Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_DUMP" "1"
    Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_DUMP_DIR" $captureDir
    Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_DUMP_TRIGGER" $picaTriggerPath
    Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_DUMP_FRAMES" ([string][Math]::Max(1, $Frames))
    Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_DUMP_MAX_VERTICES" ([string][Math]::Max(1, $MaxVerticesPerDraw))
    Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_DUMP_IMMEDIATE" "0"
    if (-not [string]::IsNullOrWhiteSpace($WriterWatchlist)) {
        Assert-InputFile -Path $WriterWatchlist -Label "PICA writer watchlist"
        Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_WRITER_TRACE" "1"
        Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_WRITER_TRACE_WATCHLIST" (Resolve-Path -LiteralPath $WriterWatchlist).Path
        Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_WRITER_TRACE_OUTPUT" $writerTracePath
        Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_WRITER_TRACE_MAX_ROWS" ([string][Math]::Max(1, $WriterTraceMaxRows))
    } else {
        Set-CaptureEnvironment $oldEnvironment "OOT3D_PICA_WRITER_TRACE" "0"
    }
    Set-CaptureEnvironment $oldEnvironment "OOT3D_TITLE_ACTOR_DRAW_TRACE" "0"
    Set-CaptureEnvironment $oldEnvironment "OOT3D_TITLE_SKEL_DRAW_TRACE" "0"

    Get-Process azahar -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $AzaharExe -ArgumentList @($RomPath) `
        -WorkingDirectory (Split-Path -Parent $AzaharExe) -PassThru
    $startedAt = Get-Date

    $deadline = (Get-Date).AddSeconds([Math]::Max(10, $TimeoutSeconds))
    while ((Get-Date) -lt $deadline) {
        $frameFiles = @(Get-ChildItem -LiteralPath $captureDir -Filter "oot3d_pica_frame_*.jsonl" -File -ErrorAction SilentlyContinue |
            Sort-Object Name)
        $completeFrames = @($frameFiles | Where-Object { Test-PicaCaptureComplete $_ })
        $hasLensBatch = $false
        $hasTerminalPacket = $false
        if (Test-Path -LiteralPath $tracePath) {
            $hasLensBatch = [bool](Select-String -LiteralPath $tracePath -SimpleMatch '"event":"lens_quad_batch_draw"' -Quiet)
            $hasTerminalPacket = [bool](Select-String -LiteralPath $tracePath -SimpleMatch '"is_terminal_lens_object":true' -Quiet)
            if ($hasLensBatch) {
                $nativeTriggerKind = "native_lens_quad_batch"
            } elseif ($hasTerminalPacket) {
                $nativeTriggerKind = "tracked_terminal_lens_object_packet"
            }
        }
        if (-not $hasLensBatch -and -not $hasTerminalPacket -and -not $fallbackTriggered -and
            ((Get-Date) - $startedAt).TotalMilliseconds -ge [Math]::Max(1000, $FallbackCaptureAfterMs)) {
            "no_native_lens_batch_observed" | Set-Content -LiteralPath $picaTriggerPath -Encoding ascii
            "no_native_lens_batch_observed" | Set-Content -LiteralPath $screenshotTriggerPath -Encoding ascii
            $fallbackTriggered = $true
        }
        if (($hasLensBatch -or $hasTerminalPacket -or $fallbackTriggered) -and
            $completeFrames.Count -ge [Math]::Max(1, $Frames) -and
            (Test-Path -LiteralPath $screenshotPath)) {
            $frameFiles = @($completeFrames | Select-Object -First ([Math]::Max(1, $Frames)))
            break
        }
        if ($process.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }

    if ($frameFiles.Count -lt [Math]::Max(1, $Frames)) {
        $timedOut = $true
    }
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    Restore-CaptureEnvironment $oldEnvironment
}

if (-not (Test-Path -LiteralPath $tracePath)) {
    throw "Azahar did not produce the native lens runtime trace: $tracePath"
}
if (-not (Test-Path -LiteralPath $screenshotPath)) {
    throw "Azahar did not produce the event-aligned lens screenshot: $screenshotPath"
}
if ($timedOut) {
    throw "Azahar produced $($frameFiles.Count) complete PICA frames; expected $Frames before timeout"
}

foreach ($frame in $frameFiles) {
    $convertedPath = Join-Path $derivedDir ($frame.BaseName + ".native_pica_register_trace.json")
    & $convertScript -InputPath $frame.FullName -OutputPath $convertedPath -InputFormat jsonl | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "PICA conversion failed for $($frame.FullName)"
    }
    $convertedTraces += $convertedPath
}

& python $analyzeScript --capture-dir $captureDir --trace $tracePath `
    --output-json $analysisJsonPath --output-md $analysisMarkdownPath
if ($LASTEXITCODE -ne 0) {
    throw "Lens dump analysis failed"
}

$summary = [ordered]@{
    format = "oot3d_title_intro_slot6_native_lens_dump_v1"
    generated_at = (Get-Date).ToString("o")
    evidence_role = "validation_only_not_runtime_input"
    savestate_slot = 6
    trigger_after_process_start_ms = [Math]::Max(0, $TriggerAfterMs)
    fallback_capture_after_process_start_ms = [Math]::Max(1000, $FallbackCaptureAfterMs)
    capture_trigger_kind = if ($fallbackTriggered) { "no_native_lens_batch_fallback" } else { $nativeTriggerKind }
    azahar_exe = $AzaharExe
    rom_path = $RomPath
    trace_path = $tracePath
    writer_trace_path = if (Test-Path -LiteralPath $writerTracePath) { $writerTracePath } else { $null }
    screenshot_path = $screenshotPath
    pica_frame_count = $frameFiles.Count
    pica_frames = @($frameFiles | ForEach-Object { $_.FullName })
    converted_pica_traces = $convertedTraces
    analysis_json = $analysisJsonPath
    analysis_markdown = $analysisMarkdownPath
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding ascii
$summary | ConvertTo-Json -Depth 8
