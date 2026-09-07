param(
    [string]$OutputRoot = "I:\oot3dre\captures\azahar_pica",
    [string]$CaptureName = "",
    [string]$AzaharExe = "E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [int]$Slot = 5,
    [int]$WarmupSeconds = 10,
    [int]$Frames = 3,
    [int]$MaxVerticesPerDraw = 65536,
    [int]$TimeoutSeconds = 60,
    [int]$PostCaptureTraceSeconds = 8,
    [int]$ScreenshotExtraDelayMs = 500,
    [int]$TriggerExtraDelayMs = 0,
    [int]$WriterTraceMaxRows = 750000,
    [int]$ActorDrawTraceMaxRows = 2048,
    [int]$SkelDrawTraceMaxRows = 4096,
    [string]$SeedWatchList = "I:\oot3dre\captures\azahar_pica\derived\watch_addresses.runtime.txt",
    [switch]$DisableWriterTrace,
    [switch]$DisableActorDrawTrace,
    [switch]$DisableSkelDrawTrace,
    [switch]$PreserveSeedWatchList
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$convertTraceScript = Join-Path $PSScriptRoot "Convert-Oot3dNativePicaTrace.ps1"
$watchlistBuilder = Join-Path $repoRoot "tools\oot3d\native_demo_host\oot3d_pica_command_list_watchlist.py"

function Assert-FileExists {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label not found: $Path"
    }
}

function Stop-ProcessQuietly {
    param([System.Diagnostics.Process]$Process)
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Wait-ForPath {
    param(
        [string]$Path,
        [int]$TimeoutSeconds
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Test-PicaFrameComplete {
    param([System.IO.FileInfo]$Path)
    try {
        $tail = Get-Content -LiteralPath $Path.FullName -Tail 1 -ErrorAction Stop
        return [string]$tail -match '"event"\s*:\s*"capture_end"'
    } catch {
        return $false
    }
}

function Wait-ForPicaFrames {
    param(
        [string]$CaptureDir,
        [int]$ExpectedCount,
        [int]$TimeoutSeconds,
        [System.Diagnostics.Process]$Process
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $frames = @(Get-ChildItem -LiteralPath $CaptureDir -Filter "oot3d_pica_frame_*.jsonl" -File -ErrorAction SilentlyContinue |
            Sort-Object Name)
        if ($frames.Count -ge $ExpectedCount) {
            $complete = $true
            foreach ($frame in ($frames | Select-Object -First $ExpectedCount)) {
                if (-not (Test-PicaFrameComplete -Path $frame)) {
                    $complete = $false
                    break
                }
            }
            if ($complete) {
                return $frames
            }
        }
        if ($null -ne $Process -and $Process.HasExited) {
            return $frames
        }
        Start-Sleep -Milliseconds 500
    }
    return @(Get-ChildItem -LiteralPath $CaptureDir -Filter "oot3d_pica_frame_*.jsonl" -File -ErrorAction SilentlyContinue |
        Sort-Object Name)
}

function Set-EnvAndRemember {
    param(
        [hashtable]$OldValues,
        [string]$Name,
        [string]$Value
    )
    if (-not $OldValues.ContainsKey($Name)) {
        $OldValues[$Name] = [Environment]::GetEnvironmentVariable($Name, "Process")
    }
    [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

function Restore-Env {
    param([hashtable]$OldValues)
    foreach ($name in $OldValues.Keys) {
        [Environment]::SetEnvironmentVariable($name, $OldValues[$name], "Process")
    }
}

Assert-FileExists -Path $AzaharExe -Label "Azahar exe"
Assert-FileExists -Path $RomPath -Label "OOT3D CCI"
Assert-FileExists -Path $convertTraceScript -Label "PICA trace converter"
Assert-FileExists -Path $watchlistBuilder -Label "PICA command-list watchlist builder"

if ([string]::IsNullOrWhiteSpace($CaptureName)) {
    $CaptureName = "kokiri_slot5_after10s_{0}" -f (Get-Date).ToString("yyyyMMdd_HHmmss")
}

$captureDir = Join-Path $OutputRoot $CaptureName
$derivedDir = Join-Path $captureDir "derived"
$triggerPath = Join-Path $captureDir "capture.trigger"
$screenshotPath = Join-Path $captureDir ("emulator_slot{0}_after{1}s.png" -f $Slot, $WarmupSeconds)
$writerTracePath = Join-Path $derivedDir "oot3d_pica_writer_trace.csv"
$actorDrawTracePath = Join-Path $derivedDir "oot3d_title_actor_draw_trace.csv"
$skelDrawTracePath = Join-Path $derivedDir "oot3d_title_skel_draw_trace.csv"
$watchListPath = Join-Path $derivedDir "watch_addresses.from_stable_frame.txt"
$generatedWatchListPath = Join-Path $derivedDir "watch_addresses.from_stable_frame.generated.txt"
$watchSummaryPath = Join-Path $derivedDir "watch_addresses.from_stable_frame.summary.json"
$summaryPath = Join-Path $captureDir "emulator_dump_summary.json"

New-Item -ItemType Directory -Force -Path $captureDir, $derivedDir | Out-Null
Remove-Item -LiteralPath $triggerPath, $screenshotPath, $writerTracePath, $actorDrawTracePath,
    $skelDrawTracePath, $watchListPath,
    $watchSummaryPath, $summaryPath -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $captureDir "oot3d_pica_frame_*.jsonl") -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $derivedDir "oot3d_pica_frame_*.native_pica_register_trace.json") -ErrorAction SilentlyContinue

if (Test-Path -LiteralPath $SeedWatchList) {
    Copy-Item -LiteralPath $SeedWatchList -Destination $watchListPath -Force
} else {
    Set-Content -LiteralPath $watchListPath -Value "" -Encoding ascii
}

$oldEnv = @{}
$process = $null
$triggeredAt = $null
$frameFiles = @()
$convertedTraces = @()
$selectedWatchSource = $null

try {
    $warmupMs = [Math]::Max(0, $WarmupSeconds * 1000)
    $azaharAutoLoadDelayMs = 1500
    $screenshotDelayMs = $warmupMs + [Math]::Max(0, $ScreenshotExtraDelayMs)
    $triggerDelayMs = $azaharAutoLoadDelayMs + $warmupMs + [Math]::Max(0, $TriggerExtraDelayMs)

    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_AUTO_LOAD_STATE_SLOT" -Value ([string]$Slot)
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_DIAGNOSTIC_FORCE_NATIVE_RENDER" -Value "1"
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_AUTO_SCREENSHOT_PATH" -Value $screenshotPath
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_AUTO_SCREENSHOT_DELAY_MS" -Value ([string]$screenshotDelayMs)
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_DUMP" -Value "1"
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_DUMP_DIR" -Value $captureDir
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_DUMP_TRIGGER" -Value $triggerPath
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_DUMP_FRAMES" -Value ([string][Math]::Max(1, $Frames))
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_DUMP_MAX_VERTICES" -Value ([string][Math]::Max(1, $MaxVerticesPerDraw))
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_WRITER_TRACE" -Value $(if ($DisableWriterTrace.IsPresent) { "0" } else { "1" })
    if (-not $DisableWriterTrace.IsPresent) {
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_WRITER_TRACE_WATCHLIST" -Value $watchListPath
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_WRITER_TRACE_OUTPUT" -Value $writerTracePath
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_PICA_WRITER_TRACE_MAX_ROWS" -Value ([string][Math]::Max(1, $WriterTraceMaxRows))
    }
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_TITLE_ACTOR_DRAW_TRACE" -Value $(if ($DisableActorDrawTrace.IsPresent) { "0" } else { "1" })
    if (-not $DisableActorDrawTrace.IsPresent) {
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_TITLE_ACTOR_DRAW_TRACE_OUTPUT" -Value $actorDrawTracePath
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_TITLE_ACTOR_DRAW_TRACE_MAX_ROWS" -Value ([string][Math]::Max(1, $ActorDrawTraceMaxRows))
    }
    Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_TITLE_SKEL_DRAW_TRACE" -Value $(if ($DisableSkelDrawTrace.IsPresent) { "0" } else { "1" })
    if (-not $DisableSkelDrawTrace.IsPresent) {
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_TITLE_SKEL_DRAW_TRACE_OUTPUT" -Value $skelDrawTracePath
        Set-EnvAndRemember -OldValues $oldEnv -Name "OOT3D_TITLE_SKEL_DRAW_TRACE_MAX_ROWS" -Value ([string][Math]::Max(1, $SkelDrawTraceMaxRows))
    }

    $process = Start-Process -FilePath $AzaharExe -ArgumentList @($RomPath) `
        -WorkingDirectory (Split-Path -Parent $AzaharExe) -PassThru

    Start-Sleep -Milliseconds $triggerDelayMs
    "trigger" | Set-Content -LiteralPath $triggerPath -Encoding ascii
    $triggeredAt = (Get-Date).ToString("o")

    $remaining = [Math]::Max(5, $TimeoutSeconds - [int][Math]::Ceiling($triggerDelayMs / 1000.0))
    $expectedFrames = [Math]::Max(1, $Frames)
    $frameFiles = @(Wait-ForPicaFrames -CaptureDir $captureDir -ExpectedCount $expectedFrames `
        -TimeoutSeconds $remaining -Process $process | Select-Object -First $expectedFrames)

    if ($frameFiles.Count -gt 0) {
        $selected = $frameFiles | Sort-Object Length -Descending | Select-Object -First 1
        $selectedWatchSource = $selected.FullName
        $generatedWatchOutput = if ($PreserveSeedWatchList.IsPresent) { $generatedWatchListPath } else { $watchListPath }
        & python $watchlistBuilder --input $selected.FullName --output $generatedWatchOutput `
            --summary-out $watchSummaryPath --mode all --span-bytes 64 | Out-Null
    }

    if ($PostCaptureTraceSeconds -gt 0) {
        Start-Sleep -Seconds $PostCaptureTraceSeconds
    }

    foreach ($frame in $frameFiles) {
        $outputPath = Join-Path $derivedDir ($frame.BaseName + ".native_pica_register_trace.json")
        & $convertTraceScript -InputPath $frame.FullName -OutputPath $outputPath -InputFormat jsonl | Out-Null
        $convertedTraces += $outputPath
    }

    $screenshotExists = Wait-ForPath -Path $screenshotPath -TimeoutSeconds 5
    $exited = $process.WaitForExit(1000)
    if (-not $exited) {
        Stop-ProcessQuietly -Process $process
    }

    $writerTraceItem = if (Test-Path -LiteralPath $writerTracePath) {
        Get-Item -LiteralPath $writerTracePath
    } else {
        $null
    }
    $actorDrawTraceItem = if (Test-Path -LiteralPath $actorDrawTracePath) {
        Get-Item -LiteralPath $actorDrawTracePath
    } else {
        $null
    }
    $skelDrawTraceItem = if (Test-Path -LiteralPath $skelDrawTracePath) {
        Get-Item -LiteralPath $skelDrawTracePath
    } else {
        $null
    }

    $summary = [ordered]@{
        format = "oot3d_azahar_slot_emulator_dump_v2"
        generated_at = (Get-Date).ToString("o")
        slot = $Slot
        warmup_seconds_after_savestate_load = $WarmupSeconds
        azahar_auto_load_delay_ms = $azaharAutoLoadDelayMs
        trigger_delay_ms_after_process_start = $triggerDelayMs
        trigger_extra_delay_ms = [Math]::Max(0, $TriggerExtraDelayMs)
        screenshot_delay_ms_env = $screenshotDelayMs
        screenshot_delay_basis = "Azahar adds 1500ms internally, matching the savestate auto-load delay; effective screenshot time is warmup plus ScreenshotExtraDelayMs after slot load."
        azahar_exe = $AzaharExe
        rom_path = $RomPath
        capture_dir = $captureDir
        derived_dir = $derivedDir
        trigger_path = $triggerPath
        triggered_at = $triggeredAt
        screenshot_path = $screenshotPath
        screenshot_exists = [bool]$screenshotExists
        requested_pica_frame_count = [int][Math]::Max(1, $Frames)
        pica_frame_count = [int]$frameFiles.Count
        pica_frames = @($frameFiles | ForEach-Object {
            [ordered]@{
                path = $_.FullName
                length = [int64]$_.Length
                complete = [bool](Test-PicaFrameComplete -Path $_)
            }
        })
        selected_watch_source_frame = $selectedWatchSource
        watchlist_path = $watchListPath
        generated_watchlist_path = $generatedWatchListPath
        seed_watchlist_preserved = [bool]$PreserveSeedWatchList.IsPresent
        watchlist_summary_path = $watchSummaryPath
        writer_trace_path = $writerTracePath
        writer_trace_exists = [bool]($null -ne $writerTraceItem)
        writer_trace_length = if ($null -ne $writerTraceItem) { [int64]$writerTraceItem.Length } else { 0 }
        actor_draw_trace_path = $actorDrawTracePath
        actor_draw_trace_exists = [bool]($null -ne $actorDrawTraceItem)
        actor_draw_trace_length = if ($null -ne $actorDrawTraceItem) { [int64]$actorDrawTraceItem.Length } else { 0 }
        skel_draw_trace_path = $skelDrawTracePath
        skel_draw_trace_exists = [bool]($null -ne $skelDrawTraceItem)
        skel_draw_trace_length = if ($null -ne $skelDrawTraceItem) { [int64]$skelDrawTraceItem.Length } else { 0 }
        converted_trace_count = [int]$convertedTraces.Count
        converted_traces = $convertedTraces
        process_exited_before_stop = [bool]$exited
        process_exit_code = if ($exited) { $process.ExitCode } else { $null }
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding ascii
    $summary | ConvertTo-Json -Depth 8

    if (-not $screenshotExists) {
        throw "Emulator screenshot was not produced: $screenshotPath"
    }
    if ($frameFiles.Count -lt [Math]::Max(1, $Frames)) {
        throw "Only $($frameFiles.Count) PICA frame dump(s) were produced, expected $Frames"
    }
} finally {
    Stop-ProcessQuietly -Process $process
    Restore-Env -OldValues $oldEnv
}
