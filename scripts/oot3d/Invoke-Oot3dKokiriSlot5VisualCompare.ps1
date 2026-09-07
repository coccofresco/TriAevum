param(
    [string]$ComparisonDir = "I:\oot3dre_work\standalone_demo\kokiri_forest\native_host\comparison\kokiri_slot5",
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\kokiri_forest\demo_manifest.json",
    [string]$ResourceRoot = "I:\oot3dre\runtime/three_ds_recomp\src\fast",
    [string]$DemoExe = "I:\oot3dre\build-codex-rel\oot3d_native_fast3d_demo.exe",
    [string]$AzaharExe = "E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [int]$Slot = 5,
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$DemoTimeoutSeconds = 45,
    [int]$EmulatorTimeoutSeconds = 60,
    [int]$EmulatorScreenshotDelayMs = 10000,
    [double]$DemoMaterialAnimationFrame = -1.0,
    [switch]$CaptureEmulatorPicaDump,
    [int]$EmulatorDumpWarmupSeconds = 10,
    [int]$EmulatorDumpFrames = 3,
    [int]$EmulatorPostCaptureTraceSeconds = 8,
    [string]$LightingDiagnosticsPicaTraceJson = "I:\oot3dre\captures\azahar_pica\kokiri_slot5_after10s_lighting_probe\derived\oot3d_pica_frame_000000.native_pica_register_trace.json",
    [switch]$SkipDemo,
    [switch]$SkipEmulator
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
New-Item -ItemType Directory -Force -Path $ComparisonDir | Out-Null

$demoJson = Join-Path $ComparisonDir "demo_slot5.json"
$demoImage = Join-Path $ComparisonDir "demo_slot5.bmp"
$emulatorImage = Join-Path $ComparisonDir "emulator_slot5.png"
$matchedEmulatorImage = Join-Path $ComparisonDir "emulator_slot5_matched.png"
$diffImage = Join-Path $ComparisonDir "demo_vs_emulator_diff.png"
$summaryPath = Join-Path $ComparisonDir "visual_compare_summary.json"

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

function Invoke-DemoCapture {
    Remove-Item -LiteralPath $demoJson, $demoImage -ErrorAction SilentlyContinue
    Assert-FileExists -Path $DemoExe -Label "OOT3D native demo exe"
    Assert-FileExists -Path $Manifest -Label "OOT3D Kokiri manifest"
    Assert-FileExists -Path $ResourceRoot -Label "Fast3D resource root"

    $effectiveMaterialAnimationFrame = $DemoMaterialAnimationFrame
    if ($effectiveMaterialAnimationFrame -lt 0.0) {
        $effectiveMaterialAnimationFrame = ([double]$EmulatorScreenshotDelayMs / 1000.0) * 30.0
    }

    $args = @(
        "--manifest", $Manifest,
        "--resource-root", $ResourceRoot,
        "--output", $demoJson,
        "--screenshot", $demoImage,
        "--width", "$Width",
        "--height", "$Height",
        "--render-mode", "native_texture",
        "--material-animation-frame",
        ([string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}", $effectiveMaterialAnimationFrame)),
        "--frames", "1",
        "--max-seconds", "3"
    )

    $process = Start-Process -FilePath $DemoExe -ArgumentList $args -WorkingDirectory "$repoRoot" -PassThru
    $exited = $process.WaitForExit($DemoTimeoutSeconds * 1000)
    $timedOutAfterCapture = $false
    if (-not $exited) {
        $timedOutAfterCapture = Test-Path -LiteralPath $demoImage
        Stop-ProcessQuietly -Process $process
    }
    if (-not (Test-Path -LiteralPath $demoImage)) {
        throw "Demo screenshot was not produced: $demoImage"
    }

    return [ordered]@{
        path = $demoImage
        json_path = $demoJson
        exited = $exited
        exit_code = if ($exited) { $process.ExitCode } else { $null }
        timed_out_after_capture = $timedOutAfterCapture
    }
}

function Invoke-EmulatorCapture {
    Remove-Item -LiteralPath $emulatorImage -ErrorAction SilentlyContinue
    Assert-FileExists -Path $AzaharExe -Label "Azahar exe"
    Assert-FileExists -Path $RomPath -Label "OOT3D CCI"

    $oldSlot = $env:OOT3D_AUTO_LOAD_STATE_SLOT
    $oldScreenshot = $env:OOT3D_AUTO_SCREENSHOT_PATH
    $oldDelay = $env:OOT3D_AUTO_SCREENSHOT_DELAY_MS
    $oldExit = $env:OOT3D_AUTO_EXIT_AFTER_SCREENSHOT_MS
    try {
        $env:OOT3D_AUTO_LOAD_STATE_SLOT = "$Slot"
        $env:OOT3D_AUTO_SCREENSHOT_PATH = $emulatorImage
        $env:OOT3D_AUTO_SCREENSHOT_DELAY_MS = "$EmulatorScreenshotDelayMs"
        $env:OOT3D_AUTO_EXIT_AFTER_SCREENSHOT_MS = "2500"

        $process = Start-Process -FilePath $AzaharExe -ArgumentList @($RomPath) `
            -WorkingDirectory (Split-Path -Parent $AzaharExe) -PassThru
        $exited = $process.WaitForExit($EmulatorTimeoutSeconds * 1000)
        if (-not $exited) {
            Stop-ProcessQuietly -Process $process
        }
    } finally {
        $env:OOT3D_AUTO_LOAD_STATE_SLOT = $oldSlot
        $env:OOT3D_AUTO_SCREENSHOT_PATH = $oldScreenshot
        $env:OOT3D_AUTO_SCREENSHOT_DELAY_MS = $oldDelay
        $env:OOT3D_AUTO_EXIT_AFTER_SCREENSHOT_MS = $oldExit
    }

    if (-not (Test-Path -LiteralPath $emulatorImage)) {
        throw "Emulator screenshot was not produced: $emulatorImage"
    }

    return [ordered]@{
        path = $emulatorImage
        exited = $exited
        exit_code = if ($exited) { $process.ExitCode } else { $null }
    }
}

function Invoke-EmulatorDiagnosticDumpCapture {
    Remove-Item -LiteralPath $emulatorImage -ErrorAction SilentlyContinue
    $dumpScript = Join-Path $PSScriptRoot "Invoke-Oot3dKokiriSlot5EmulatorDump.ps1"
    Assert-FileExists -Path $dumpScript -Label "OOT3D Kokiri slot 5 emulator dump script"

    $dumpRoot = Join-Path $ComparisonDir "emulator_pica_dump"
    $dumpName = "slot5_after10s"
    $dumpJson = & $dumpScript `
        -OutputRoot $dumpRoot `
        -CaptureName $dumpName `
        -AzaharExe $AzaharExe `
        -RomPath $RomPath `
        -Slot $Slot `
        -WarmupSeconds $EmulatorDumpWarmupSeconds `
        -Frames $EmulatorDumpFrames `
        -TimeoutSeconds $EmulatorTimeoutSeconds `
        -PostCaptureTraceSeconds $EmulatorPostCaptureTraceSeconds | ConvertFrom-Json

    if (-not (Test-Path -LiteralPath ([string]$dumpJson.screenshot_path))) {
        throw "Emulator diagnostic dump did not produce a screenshot: $($dumpJson.screenshot_path)"
    }
    Copy-Item -LiteralPath ([string]$dumpJson.screenshot_path) -Destination $emulatorImage -Force

    return [ordered]@{
        path = $emulatorImage
        diagnostic_dump = $true
        dump_summary_path = Join-Path ([string]$dumpJson.capture_dir) "emulator_dump_summary.json"
        capture_dir = [string]$dumpJson.capture_dir
        pica_frame_count = [int]$dumpJson.pica_frame_count
        converted_trace_count = [int]$dumpJson.converted_trace_count
        writer_trace_path = [string]$dumpJson.writer_trace_path
        writer_trace_exists = [bool]$dumpJson.writer_trace_exists
        writer_trace_length = [int64]$dumpJson.writer_trace_length
        warmup_seconds_after_savestate_load = [int]$dumpJson.warmup_seconds_after_savestate_load
        converted_traces = @($dumpJson.converted_traces)
        lighting_trace_json = if (@($dumpJson.converted_traces).Count -gt 0) {
            [string]@($dumpJson.converted_traces)[0]
        } else {
            ""
        }
    }
}

function Get-CropRectangleForAspect {
    param([int]$SourceWidth, [int]$SourceHeight, [int]$TargetWidth, [int]$TargetHeight)
    $sourceAspect = [double]$SourceWidth / [double]$SourceHeight
    $targetAspect = [double]$TargetWidth / [double]$TargetHeight
    if ($sourceAspect -gt $targetAspect) {
        $cropHeight = $SourceHeight
        $cropWidth = [int][Math]::Round($SourceHeight * $targetAspect)
        return [System.Drawing.Rectangle]::new([int](($SourceWidth - $cropWidth) / 2), 0, $cropWidth, $cropHeight)
    }

    $cropWidth = $SourceWidth
    $cropHeight = [int][Math]::Round($SourceWidth / $targetAspect)
    return [System.Drawing.Rectangle]::new(0, [int](($SourceHeight - $cropHeight) / 2), $cropWidth, $cropHeight)
}

function Convert-ToMatchedReference {
    Add-Type -AssemblyName System.Drawing
    $source = [System.Drawing.Bitmap]::new($emulatorImage)
    try {
        $crop = Get-CropRectangleForAspect -SourceWidth $source.Width -SourceHeight $source.Height `
            -TargetWidth $Width -TargetHeight $Height
        $matched = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($matched)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.DrawImage($source, [System.Drawing.Rectangle]::new(0, 0, $Width, $Height), $crop,
                    [System.Drawing.GraphicsUnit]::Pixel)
            } finally {
                $graphics.Dispose()
            }
            $matched.Save($matchedEmulatorImage, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $matched.Dispose()
        }
    } finally {
        $source.Dispose()
    }
}

function Get-ImageStats {
    param([string]$Path)
    Add-Type -AssemblyName System.Drawing
    $image = [System.Drawing.Bitmap]::new($Path)
    try {
        $count = [int64]$image.Width * [int64]$image.Height
        $topHeight = [Math]::Max(1, [int]($image.Height * 0.18))
        $topCount = [int64]$image.Width * [int64]$topHeight
        [double]$sumR = 0; [double]$sumG = 0; [double]$sumB = 0
        [double]$topR = 0; [double]$topG = 0; [double]$topB = 0
        [int64]$black = 0; [int64]$topBlack = 0
        for ($y = 0; $y -lt $image.Height; ++$y) {
            for ($x = 0; $x -lt $image.Width; ++$x) {
                $pixel = $image.GetPixel($x, $y)
                $sumR += $pixel.R; $sumG += $pixel.G; $sumB += $pixel.B
                $isBlack = $pixel.R -lt 8 -and $pixel.G -lt 8 -and $pixel.B -lt 8
                if ($isBlack) { ++$black }
                if ($y -lt $topHeight) {
                    $topR += $pixel.R; $topG += $pixel.G; $topB += $pixel.B
                    if ($isBlack) { ++$topBlack }
                }
            }
        }

        $meanR = $sumR / $count
        $meanG = $sumG / $count
        $meanB = $sumB / $count
        $topMeanR = $topR / $topCount
        $topMeanG = $topG / $topCount
        $topMeanB = $topB / $topCount
        $meanLuma = 0.2126 * $meanR + 0.7152 * $meanG + 0.0722 * $meanB
        $topMeanLuma = 0.2126 * $topMeanR + 0.7152 * $topMeanG + 0.0722 * $topMeanB
        return [ordered]@{
            width = $image.Width
            height = $image.Height
            mean_rgb = [ordered]@{
                r = [Math]::Round($meanR, 4)
                g = [Math]::Round($meanG, 4)
                b = [Math]::Round($meanB, 4)
            }
            top_18_percent_mean_rgb = [ordered]@{
                r = [Math]::Round($topMeanR, 4)
                g = [Math]::Round($topMeanG, 4)
                b = [Math]::Round($topMeanB, 4)
            }
            mean_luma_approx = [Math]::Round($meanLuma, 4)
            top_18_percent_luma_approx = [Math]::Round($topMeanLuma, 4)
            black_pixel_ratio = [Math]::Round([double]$black / [double]$count, 6)
            top_18_percent_black_pixel_ratio = [Math]::Round([double]$topBlack / [double]$topCount, 6)
            magenta_cast_ratio = [Math]::Round(($meanR + $meanB) / [Math]::Max(1.0, 2.0 * $meanG), 6)
            top_18_percent_magenta_cast_ratio = [Math]::Round(($topMeanR + $topMeanB) / [Math]::Max(1.0, 2.0 * $topMeanG), 6)
        }
    } finally {
        $image.Dispose()
    }
}

function Compare-Images {
    param([string]$ActualPath, [string]$ReferencePath)
    Add-Type -AssemblyName System.Drawing
    $actual = [System.Drawing.Bitmap]::new($ActualPath)
    $reference = [System.Drawing.Bitmap]::new($ReferencePath)
    try {
        if ($actual.Width -ne $reference.Width -or $actual.Height -ne $reference.Height) {
            throw "Image dimensions differ: actual=$($actual.Width)x$($actual.Height), reference=$($reference.Width)x$($reference.Height)"
        }

        $diff = [System.Drawing.Bitmap]::new($actual.Width, $actual.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        [double]$sumR = 0; [double]$sumG = 0; [double]$sumB = 0
        [double]$topR = 0; [double]$topG = 0; [double]$topB = 0
        [int64]$count = [int64]$actual.Width * [int64]$actual.Height
        $topHeight = [Math]::Max(1, [int]($actual.Height * 0.18))
        [int64]$topCount = [int64]$actual.Width * [int64]$topHeight
        try {
            for ($y = 0; $y -lt $actual.Height; ++$y) {
                for ($x = 0; $x -lt $actual.Width; ++$x) {
                    $a = $actual.GetPixel($x, $y)
                    $r = $reference.GetPixel($x, $y)
                    $dr = [Math]::Abs([int]$a.R - [int]$r.R)
                    $dg = [Math]::Abs([int]$a.G - [int]$r.G)
                    $db = [Math]::Abs([int]$a.B - [int]$r.B)
                    $sumR += $dr; $sumG += $dg; $sumB += $db
                    if ($y -lt $topHeight) {
                        $topR += $dr; $topG += $dg; $topB += $db
                    }
                    $diff.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(
                            255,
                            [Math]::Min(255, $dr * 3),
                            [Math]::Min(255, $dg * 3),
                            [Math]::Min(255, $db * 3)))
                }
            }
            $diff.Save($diffImage, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $diff.Dispose()
        }

        return [ordered]@{
            diff_path = $diffImage
            mean_abs_error_rgb = [ordered]@{
                r = [Math]::Round($sumR / $count, 4)
                g = [Math]::Round($sumG / $count, 4)
                b = [Math]::Round($sumB / $count, 4)
            }
            mean_abs_error_luma_approx = [Math]::Round((0.2126 * $sumR + 0.7152 * $sumG + 0.0722 * $sumB) / $count, 4)
            top_18_percent_mean_abs_error_rgb = [ordered]@{
                r = [Math]::Round($topR / $topCount, 4)
                g = [Math]::Round($topG / $topCount, 4)
                b = [Math]::Round($topB / $topCount, 4)
            }
        }
    } finally {
        $actual.Dispose()
        $reference.Dispose()
    }
}

$demoResult = if ($SkipDemo) {
    Assert-FileExists -Path $demoImage -Label "existing demo screenshot"
    [ordered]@{ path = $demoImage; json_path = $demoJson; skipped = $true }
} else {
    Invoke-DemoCapture
}

$emulatorResult = if ($SkipEmulator) {
    Assert-FileExists -Path $emulatorImage -Label "existing emulator screenshot"
    [ordered]@{ path = $emulatorImage; skipped = $true }
} elseif ($CaptureEmulatorPicaDump) {
    Invoke-EmulatorDiagnosticDumpCapture
} else {
    Invoke-EmulatorCapture
}

$effectiveLightingDiagnosticsPicaTraceJson = $LightingDiagnosticsPicaTraceJson
if ($CaptureEmulatorPicaDump -and
    $emulatorResult.Contains("lighting_trace_json") -and
    -not [string]::IsNullOrWhiteSpace([string]$emulatorResult["lighting_trace_json"]) -and
    (Test-Path -LiteralPath ([string]$emulatorResult["lighting_trace_json"]))) {
    $effectiveLightingDiagnosticsPicaTraceJson = [string]$emulatorResult["lighting_trace_json"]
}

Convert-ToMatchedReference

$demoStats = Get-ImageStats -Path $demoImage
$emulatorStats = Get-ImageStats -Path $matchedEmulatorImage
$compareStats = Compare-Images -ActualPath $demoImage -ReferencePath $matchedEmulatorImage

$findings = @()
if ([double]$demoStats.magenta_cast_ratio -gt ([double]$emulatorStats.magenta_cast_ratio * 1.35)) {
    $findings += "demo_magenta_cast_exceeds_emulator"
}
if ([double]$demoStats.top_18_percent_black_pixel_ratio -gt ([double]$emulatorStats.top_18_percent_black_pixel_ratio + 0.20)) {
    $findings += "demo_top_region_or_sky_too_black"
}
if ([double]$demoStats.top_18_percent_luma_approx -lt ([double]$emulatorStats.top_18_percent_luma_approx * 0.40)) {
    $findings += "demo_top_region_or_sky_too_dark"
}
if ([double]$demoStats.mean_rgb.g -lt ([double]$emulatorStats.mean_rgb.g * 0.25)) {
    $findings += "demo_green_channel_collapsed_relative_to_emulator"
}
if ((Test-Path -LiteralPath $demoJson)) {
    $demoSummary = Get-Content -LiteralPath $demoJson -Raw | ConvertFrom-Json
    if ([int]$demoSummary.engine_render_scene.native_actor_visual_count -le 2) {
        $findings += "demo_actor_visuals_filtered_to_unambiguous_native_cmbs"
    }
}

$summary = [ordered]@{
    format = "oot3d_kokiri_slot5_visual_compare_v1"
    generated_at = (Get-Date).ToString("o")
    slot = $Slot
    dimensions = [ordered]@{ width = $Width; height = $Height }
    demo = $demoResult
    emulator = $emulatorResult
    matched_emulator_path = $matchedEmulatorImage
    demo_stats = $demoStats
    emulator_matched_stats = $emulatorStats
    comparison = $compareStats
    findings = $findings
}

$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding ascii
$lightingDiagnosticsScript = Join-Path $PSScriptRoot "Export-Oot3dLightingDiagnostics.ps1"
if (Test-Path -LiteralPath $lightingDiagnosticsScript) {
    & $lightingDiagnosticsScript -DemoJson $demoJson -OutputDir $ComparisonDir `
        -PicaTraceJson $effectiveLightingDiagnosticsPicaTraceJson `
        -VisualCompareSummary $summaryPath | Out-Null
    $summary["lighting_diagnostics"] = [ordered]@{
        json_path = Join-Path $ComparisonDir "lighting_diagnostics.json"
        batch_csv_path = Join-Path $ComparisonDir "lighting_batches.csv"
        findings_markdown_path = Join-Path $ComparisonDir "lighting_findings.md"
        pica_trace_json = $effectiveLightingDiagnosticsPicaTraceJson
    }
    $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding ascii
}

$fogWriterAnalyzer = Join-Path $repoRoot "tools\oot3d\decomp_support\scripts\analyze_kokiri_slot5_fog_writer_trace.py"
$candidateWriterTracePath = ""
if ($emulatorResult.Contains("writer_trace_path") -and
    -not [string]::IsNullOrWhiteSpace([string]$emulatorResult["writer_trace_path"])) {
    $candidateWriterTracePath = [string]$emulatorResult["writer_trace_path"]
} elseif (Test-Path -LiteralPath $effectiveLightingDiagnosticsPicaTraceJson) {
    $siblingWriterTracePath = Join-Path (Split-Path -Parent $effectiveLightingDiagnosticsPicaTraceJson) "oot3d_pica_writer_trace.csv"
    if (Test-Path -LiteralPath $siblingWriterTracePath) {
        $candidateWriterTracePath = $siblingWriterTracePath
    }
}
if (-not [string]::IsNullOrWhiteSpace($candidateWriterTracePath) -and
    (Test-Path -LiteralPath $candidateWriterTracePath) -and
    (Test-Path -LiteralPath $effectiveLightingDiagnosticsPicaTraceJson) -and
    (Test-Path -LiteralPath $fogWriterAnalyzer)) {
    $writerTracePath = $candidateWriterTracePath
    $watchlistPath = Join-Path (Split-Path -Parent $writerTracePath) "watch_addresses.from_stable_frame.txt"
    if (Test-Path -LiteralPath $watchlistPath) {
        $fogWriterJson = Join-Path $ComparisonDir "fog_writer_trace_summary.json"
        $fogWriterMarkdown = Join-Path $ComparisonDir "fog_writer_trace_summary.md"
        & python $fogWriterAnalyzer `
            --pica-trace $effectiveLightingDiagnosticsPicaTraceJson `
            --writer-trace $writerTracePath `
            --watchlist $watchlistPath `
            --output-json $fogWriterJson `
            --output-md $fogWriterMarkdown | Out-Null
        $summary["fog_writer_diagnostics"] = [ordered]@{
            json_path = $fogWriterJson
            findings_markdown_path = $fogWriterMarkdown
            pica_trace_json = $effectiveLightingDiagnosticsPicaTraceJson
            writer_trace_path = $writerTracePath
            watchlist_path = $watchlistPath
        }
        $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding ascii
    }
}
$summary | ConvertTo-Json -Depth 12
