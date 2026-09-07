param(
    [string]$OutputRoot = "I:\oot3dre_work\azahar",
    [string]$CaptureName = "",
    [string]$AzaharExe = "E:\azahar pcvr\build-pcvr-qt\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [int]$Slot = 6,
    [ValidateSet("opengl", "vulkan")]
    [string[]]$Backends = @("opengl", "vulkan"),
    [int]$StartFrame = 30,
    [int]$FrameInterval = 90,
    [int]$CaptureCount = 6,
    [int]$AutoLoadDelayMs = 1500,
    [int]$AutoExitDelayMs = 2000,
    [int]$TimeoutSeconds = 120,
    [int]$MaxCaptureLagFrames = 3,
    [int]$MaxBackendFrameDelta = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-FileExists {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Set-EnvAndRemember {
    param([hashtable]$OldValues, [string]$Name, [string]$Value)
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

function Get-ImageRmse {
    param([string]$MagickExe, [string]$ReferencePath, [string]$CandidatePath)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $MagickExe
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.ArgumentList.Add("compare")
    $startInfo.ArgumentList.Add("-metric")
    $startInfo.ArgumentList.Add("RMSE")
    $startInfo.ArgumentList.Add($ReferencePath)
    $startInfo.ArgumentList.Add($CandidatePath)
    $startInfo.ArgumentList.Add("null:")

    $process = [Diagnostics.Process]::Start($startInfo)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -gt 1) {
        throw "ImageMagick compare failed ($($process.ExitCode)): $stderr$stdout"
    }

    $metric = ($stderr + $stdout).Trim()
    if ($metric -notmatch '\(([-+0-9.eE]+)\)') {
        throw "Could not parse ImageMagick RMSE: $metric"
    }
    return [ordered]@{
        raw = $metric
        normalized = [double]::Parse(
            $Matches[1], [Globalization.CultureInfo]::InvariantCulture)
    }
}

Assert-FileExists -Path $AzaharExe -Label "Azahar executable"
Assert-FileExists -Path $RomPath -Label "OOT3D image"
if ($StartFrame -lt 1 -or $FrameInterval -lt 1 -or $CaptureCount -lt 1) {
    throw "StartFrame, FrameInterval, and CaptureCount must be positive."
}
if (Get-Process azahar -ErrorAction SilentlyContinue) {
    throw "Azahar is already running; close it before an automated framebuffer reference run."
}
if ([string]::IsNullOrWhiteSpace($CaptureName)) {
    $CaptureName = "slot{0}_framebuffer_{1}" -f $Slot, (Get-Date).ToString("yyyyMMdd_HHmmss")
}

$captureRoot = Join-Path $OutputRoot $CaptureName
New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null
$oldEnv = @{}
$backendResults = @()

try {
    foreach ($backend in $Backends) {
        $backendDir = Join-Path $captureRoot $backend
        New-Item -ItemType Directory -Force -Path $backendDir | Out-Null

        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_LOAD_STATE_SLOT" ([string]$Slot)
        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_LOAD_STATE_DELAY_MS" ([string]$AutoLoadDelayMs)
        Set-EnvAndRemember $oldEnv "OOT3D_DIAGNOSTIC_FORCE_NATIVE_RENDER" "1"
        Set-EnvAndRemember $oldEnv "OOT3D_DIAGNOSTIC_GRAPHICS_API" $backend
        Set-EnvAndRemember $oldEnv "OOT3D_CAPTURE_FRAMEBUFFER_METADATA" "1"
        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_SCREENSHOT_SEQUENCE_DIR" $backendDir
        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_SCREENSHOT_SEQUENCE_START_FRAME" ([string]$StartFrame)
        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_SCREENSHOT_SEQUENCE_FRAME_INTERVAL" ([string]$FrameInterval)
        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_SCREENSHOT_SEQUENCE_COUNT" ([string]$CaptureCount)
        Set-EnvAndRemember $oldEnv "OOT3D_AUTO_EXIT_AFTER_SCREENSHOT_MS" ([string]$AutoExitDelayMs)

        $process = Start-Process -FilePath $AzaharExe -ArgumentList @($RomPath) `
            -WorkingDirectory (Split-Path -Parent $AzaharExe) -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw "Azahar $backend framebuffer capture timed out."
        }
        if ($process.ExitCode -ne 0) {
            throw "Azahar $backend framebuffer capture exited with code $($process.ExitCode)."
        }

        $images = @(Get-ChildItem -LiteralPath $backendDir -Filter "*.png" -File | Sort-Object Name)
        if ($images.Count -ne $CaptureCount) {
            throw "Azahar $backend produced $($images.Count) framebuffer captures; expected $CaptureCount."
        }

        $captures = @()
        foreach ($image in $images) {
            if ($image.BaseName -notmatch '^emulator_f([0-9]+)_i([0-9]+)$') {
                throw "Unexpected framebuffer capture name: $($image.Name)"
            }
            $targetFrame = [int64]$Matches[1]
            $index = [int]$Matches[2]
            $metadataPath = $image.FullName + ".capture.json"
            Assert-FileExists -Path $metadataPath -Label "Framebuffer metadata"
            $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json

            if ([string]$metadata.backend -ne $backend) {
                throw "Capture backend mismatch for $($image.Name): $($metadata.backend)"
            }
            if ([bool]$metadata.depends_on_windows_foreground) {
                throw "Capture incorrectly depends on the Windows foreground: $($image.Name)"
            }
            $actualFrame = [int64]$metadata.presented_frame
            $lag = $actualFrame - $targetFrame
            if ($lag -lt 0 -or $lag -gt $MaxCaptureLagFrames) {
                throw "Capture $($image.Name) completed at frame $actualFrame (target $targetFrame)."
            }

            $captures += [ordered]@{
                index = $index
                target_frame = $targetFrame
                actual_frame = $actualFrame
                capture_lag_frames = $lag
                presented_frame_epoch = [int64]$metadata.presented_frame_epoch
                image_path = $image.FullName
                metadata_path = $metadataPath
                source = [string]$metadata.source
                readback = [string]$metadata.readback
                width = [int]$metadata.width
                height = [int]$metadata.height
            }
        }

        $backendResults += [ordered]@{
            backend = $backend
            capture_dir = $backendDir
            process_exit_code = $process.ExitCode
            captures = $captures
        }
    }
} finally {
    Restore-Env $oldEnv
}

$comparisons = @()
$magick = Get-Command magick -ErrorAction SilentlyContinue
if ($Backends -contains "opengl" -and $Backends -contains "vulkan" -and $null -ne $magick) {
    $glResult = $backendResults | Where-Object { $_.backend -eq "opengl" }
    $vkResult = $backendResults | Where-Object { $_.backend -eq "vulkan" }
    for ($index = 0; $index -lt $CaptureCount; ++$index) {
        $glCapture = $glResult.captures[$index]
        $vkCapture = $vkResult.captures[$index]
        $frameDelta = [Math]::Abs($glCapture.actual_frame - $vkCapture.actual_frame)
        if ($frameDelta -gt $MaxBackendFrameDelta) {
            throw "Backend frame delta $frameDelta exceeds $MaxBackendFrameDelta at index $index."
        }
        $rmse = Get-ImageRmse -MagickExe $magick.Source `
            -ReferencePath $glCapture.image_path -CandidatePath $vkCapture.image_path
        $comparisons += [ordered]@{
            index = $index
            target_frame = $glCapture.target_frame
            opengl_actual_frame = $glCapture.actual_frame
            vulkan_actual_frame = $vkCapture.actual_frame
            actual_frame_delta = $frameDelta
            rmse = $rmse
        }
    }
}

$summary = [ordered]@{
    format = "oot3d_azahar_framebuffer_reference_v1"
    generated_at = (Get-Date).ToString("o")
    evidence_role = "validation_only_not_runtime_input"
    capture_source = "Azahar RendererBase::RequestScreenshot direct renderer framebuffer readback"
    depends_on_windows_foreground = $false
    savestate_slot = $Slot
    start_frame_after_savestate = $StartFrame
    frame_interval = $FrameInterval
    capture_count = $CaptureCount
    azahar_exe = $AzaharExe
    rom_path = $RomPath
    backends = $backendResults
    backend_comparisons = $comparisons
}
$summaryPath = Join-Path $captureRoot "framebuffer_reference.json"
$markdownPath = Join-Path $captureRoot "framebuffer_reference.md"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding ascii

$lines = @(
    "# OOT3D Azahar framebuffer reference",
    "",
    "Captures come from Azahar renderer framebuffer readback. They do not use the Windows desktop or foreground window.",
    "Savestate slot: $Slot. Frame targets after load: $StartFrame + n * $FrameInterval.",
    "Evidence is validation-only and is never a runtime input.",
    "",
    "| Index | Target | OpenGL frame | Vulkan frame | GL/VK normalized RMSE |",
    "| -: | -: | -: | -: | -: |"
)
foreach ($comparison in $comparisons) {
    $lines += [string]::Format(
        [Globalization.CultureInfo]::InvariantCulture,
        "| {0} | {1} | {2} | {3} | {4:G8} |",
        $comparison.index, $comparison.target_frame, $comparison.opengl_actual_frame,
        $comparison.vulkan_actual_frame, $comparison.rmse.normalized)
}
$lines | Set-Content -LiteralPath $markdownPath -Encoding ascii

Write-Host "Azahar framebuffer reference complete: $captureRoot"
Write-Host "Summary: $summaryPath"
Write-Host "Report: $markdownPath"
