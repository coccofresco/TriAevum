[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$ReuseExistingRuns,
    [string]$OutputRoot = "I:\oot3dre_work\vulkan",
    [string]$RunName = "",
    [string]$InputTimeline = "",
    [int]$Frames = 361,
    [double]$FixedDeltaSeconds = (1.0 / 30.0),
    [int]$CaptureInterval = 60,
    [double]$MaximumNormalizedRmse = 0.001
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
$buildScript = Join-Path $repoRoot "tools\oot3d\build_fast_dev.ps1"
if ([string]::IsNullOrWhiteSpace($InputTimeline)) {
    $InputTimeline = Join-Path $repoRoot `
        "tools\oot3d\native_demo_host\input_timelines\kokiri_renderer_parity.json"
}
$InputTimeline = [System.IO.Path]::GetFullPath($InputTimeline)

foreach ($required in @($launcher, $buildScript, $InputTimeline)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required renderer-parity input is missing: $required"
    }
}
if ($Frames -le 0 -or $FixedDeltaSeconds -le 0.0 -or
    $FixedDeltaSeconds -gt 0.05 -or $CaptureInterval -le 0) {
    throw "Frames and CaptureInterval must be positive; FixedDeltaSeconds must be in (0, 0.05]."
}
if ($MaximumNormalizedRmse -lt 0.0 -or $MaximumNormalizedRmse -gt 1.0) {
    throw "MaximumNormalizedRmse must be in [0, 1]."
}
$magick = (Get-Command magick -ErrorAction Stop).Source
if ([string]::IsNullOrWhiteSpace($RunName)) {
    $RunName = "kokiri_renderer_parity_{0}" -f (Get-Date).ToString("yyyyMMdd_HHmmss")
}
$runRoot = Join-Path $OutputRoot $RunName
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

function Get-ImageRmse {
    param([string]$ReferencePath, [string]$CandidatePath)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $magick
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @(
        "compare", "-metric", "RMSE", $ReferencePath, $CandidatePath, "null:")) {
        $startInfo.ArgumentList.Add($argument)
    }
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

function ConvertTo-CompactJson {
    param($Value)
    return ($Value | ConvertTo-Json -Depth 100 -Compress)
}

function Assert-JsonEqual {
    param($Reference, $Candidate, [string]$Label)
    if ((ConvertTo-CompactJson $Reference) -cne (ConvertTo-CompactJson $Candidate)) {
        throw "OpenGL/Vulkan runtime state mismatch: $Label"
    }
}

if (-not $SkipBuild) {
    & $buildScript -Target oot3d_native_game -Parallel 16
    if ($LASTEXITCODE -ne 0) {
        throw "oot3d_native_game build failed with exit code $LASTEXITCODE"
    }
}

$runs = [ordered]@{}
foreach ($backend in @("opengl", "vulkan")) {
    $backendDir = Join-Path $runRoot $backend
    New-Item -ItemType Directory -Force -Path $backendDir | Out-Null
    $summaryPath = Join-Path $backendDir "summary.json"
    $screenshotPath = Join-Path $backendDir "kokiri.bmp"
    $logPath = Join-Path $backendDir "run.log"
    if (-not $ReuseExistingRuns) {
        & $launcher -SkipBuild -Renderer $backend -Frames $Frames `
            -FixedDeltaSeconds $FixedDeltaSeconds -InputTimeline $InputTimeline `
            -Output $summaryPath -Screenshot $screenshotPath -ScreenshotSequence `
            -ScreenshotStartFrame 0 -ScreenshotInterval $CaptureInterval *> $logPath
        if ($LASTEXITCODE -ne 0) {
            throw "$backend native-game run failed with exit code $LASTEXITCODE; see $logPath"
        }
    }
    if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
        throw "$backend native-game run did not produce $summaryPath"
    }
    $runs[$backend] = [ordered]@{
        directory = $backendDir
        summary_path = $summaryPath
        summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    }
}

$gl = $runs.opengl.summary
$vk = $runs.vulkan.summary
foreach ($label in @("OpenGL", "Vulkan")) {
    $summary = if ($label -eq "OpenGL") { $gl } else { $vk }
    if ([int]$summary.frame_count -ne $Frames) {
        throw "$label frame count is $($summary.frame_count), expected $Frames"
    }
    if ([string]$summary.frame_timing.mode -ne "fixed") {
        throw "$label did not use fixed timing"
    }
    if ([string]$summary.input_playback.mode -ne "scripted_timeline" -or
        [int]$summary.input_playback.scripted_frame_count -ne $Frames -or
        [int]$summary.input_playback.movement_frame_count -le 0) {
        throw "$label did not exercise the scripted movement timeline"
    }
    if ([bool]$summary.framebuffer_screenshot.depends_on_windows_foreground) {
        throw "$label framebuffer capture unexpectedly depends on Windows foreground state"
    }
    if ([bool]$summary.scene.runtime_n64_asset_substitution_used -or
        [bool]$summary.scene.shipwright_replacement_path_used -or
        [bool]$summary.actor_runtime.n64_actor_fallback_used) {
        throw "$label used an N64/Shipwright runtime content path"
    }
}

foreach ($section in @(
    "link_instance", "link_motion", "link_animation", "camera",
    "renderer_parity_checkpoints")) {
    Assert-JsonEqual $gl.$section $vk.$section $section
}

$actorStateGl = [ordered]@{
    frame_count = $gl.actor_runtime.frame_count
    live_count = $gl.actor_runtime.live_count
    spawned_room_actor_count = $gl.actor_runtime.spawned_room_actor_count
    n64_actor_fallback_used = $gl.actor_runtime.n64_actor_fallback_used
    archive_selected_visual_replacement_count =
        $gl.actor_runtime.archive_selected_visual_replacement_count
}
$actorStateVk = [ordered]@{
    frame_count = $vk.actor_runtime.frame_count
    live_count = $vk.actor_runtime.live_count
    spawned_room_actor_count = $vk.actor_runtime.spawned_room_actor_count
    n64_actor_fallback_used = $vk.actor_runtime.n64_actor_fallback_used
    archive_selected_visual_replacement_count =
        $vk.actor_runtime.archive_selected_visual_replacement_count
}
Assert-JsonEqual $actorStateGl $actorStateVk "actor_runtime"
if ([bool]$actorStateVk.n64_actor_fallback_used) {
    throw "Kokiri actor runtime used an N64 actor fallback"
}

$adapterKeys = @(
    "backend_draw_call_count",
    "triangle_count",
    "missing_texture_batch_count",
    "shader_input_layout_mismatch_count",
    "native_blend_state_backend_unsupported_batch_count",
    "native_cull_state_backend_unsupported_batch_count",
    "native_pica_alpha_test_backend_unsupported_batch_count",
    "native_pica_texture2_backend_unsupported_batch_count",
    "native_pica_fog_pending_batch_count",
    "native_pica_self_shadow_pending_batch_count"
)
$adapterGl = [ordered]@{}
$adapterVk = [ordered]@{}
foreach ($key in $adapterKeys) {
    $adapterGl[$key] = $gl.fast3d_adapter_lifetime.$key
    $adapterVk[$key] = $vk.fast3d_adapter_lifetime.$key
}
Assert-JsonEqual $adapterGl $adapterVk "fast3d_semantic_counters"

$checkpoints = @($gl.renderer_parity_checkpoints)
if ($checkpoints.Count -lt 2) {
    throw "Renderer parity run produced too few runtime checkpoints"
}
$uniquePositions = @($checkpoints | ForEach-Object {
    $p = $_.link.actor_position
    "{0:R},{1:R},{2:R}" -f [double]$p.x, [double]$p.y, [double]$p.z
} | Select-Object -Unique)
$uniqueAnimations = @($checkpoints.animation.clip_id | Select-Object -Unique)
$uniqueCameraPositions = @($checkpoints | ForEach-Object {
    $p = $_.camera.position
    "{0:R},{1:R},{2:R}" -f [double]$p.x, [double]$p.y, [double]$p.z
} | Select-Object -Unique)
if ($uniquePositions.Count -lt 2 -or $uniqueAnimations.Count -lt 2 -or
    $uniqueCameraPositions.Count -lt 2) {
    throw "Scripted run did not exercise movement, animation, and camera changes"
}

$frameRows = @()
for ($frame = 0; $frame -lt $Frames; $frame += $CaptureInterval) {
    $name = "kokiri_{0:D6}.bmp" -f $frame
    $glPath = Join-Path $runs.opengl.directory $name
    $vkPath = Join-Path $runs.vulkan.directory $name
    foreach ($path in @($glPath, $vkPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing framebuffer capture: $path"
        }
    }
    $metric = Get-ImageRmse -ReferencePath $glPath -CandidatePath $vkPath
    $frameRows += [ordered]@{
        frame = $frame
        opengl = $glPath
        vulkan = $vkPath
        rmse_raw = $metric.raw
        normalized_rmse = $metric.normalized
        pass = $metric.normalized -le $MaximumNormalizedRmse
    }
}
$maximumRmseValues = @($frameRows | ForEach-Object {
    [double]$_['normalized_rmse']
})
$maximumRmse = [double](($maximumRmseValues | Measure-Object -Maximum).Maximum)
$status = if ($maximumRmse -le $MaximumNormalizedRmse) { "pass" } else { "fail" }
$report = [ordered]@{
    schema = "oot3d.native_game.renderer_parity.v1"
    status = $status
    scene = "Kokiri Forest initial child day, Link house entrance"
    renderers = @("opengl", "vulkan")
    frame_count = $Frames
    fixed_delta_seconds = $FixedDeltaSeconds
    input_timeline = $InputTimeline
    input_timeline_is_test_only = $true
    windows_input_injection_used = $false
    framebuffer_source = "Fast3D rendering API ReadFramebufferToCPU"
    depends_on_windows_foreground = $false
    runtime_n64_asset_substitution_used =
        [bool]$gl.scene.runtime_n64_asset_substitution_used
    shipwright_replacement_path_used = [bool]$gl.scene.shipwright_replacement_path_used
    checkpoint_count = $checkpoints.Count
    distinct_link_positions = $uniquePositions.Count
    animation_clips_exercised = $uniqueAnimations
    distinct_camera_positions = $uniqueCameraPositions.Count
    actor_runtime = $actorStateGl
    fast3d_semantic_counters = $adapterGl
    maximum_normalized_rmse = $maximumRmse
    maximum_allowed_normalized_rmse = $MaximumNormalizedRmse
    frames = $frameRows
}
$reportJson = Join-Path $runRoot "renderer_parity.json"
$reportMd = Join-Path $runRoot "renderer_parity.md"
$report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $reportJson -Encoding utf8
$markdown = @(
    "# OOT3D native game OpenGL/Vulkan parity",
    "",
    "Status: **$status**",
    "",
    "- Scene: Kokiri Forest initial child day, Link house entrance",
    "- Frames: $Frames at fixed delta $FixedDeltaSeconds s",
    "- Runtime checkpoints: $($checkpoints.Count)",
    "- Distinct Link positions: $($uniquePositions.Count)",
    "- Animation clips: $($uniqueAnimations -join ', ')",
    "- Distinct camera positions: $($uniqueCameraPositions.Count)",
    "- Maximum normalized framebuffer RMSE: $maximumRmse",
    "- Captures: direct rendering API framebuffer readback; no Windows foreground dependency",
    "- Runtime evidence: native OOT3D game path; no N64 actor fallback",
    "",
    "| Frame | Normalized RMSE | Pass |",
    "| -: | -: | :--: |"
)
foreach ($row in $frameRows) {
    $markdown += "| $($row['frame']) | $($row['normalized_rmse']) | $($row['pass']) |"
}
$markdown | Set-Content -LiteralPath $reportMd -Encoding utf8

if ($status -ne "pass") {
    throw "OpenGL/Vulkan framebuffer RMSE $maximumRmse exceeds $MaximumNormalizedRmse; see $reportMd"
}
Write-Output $reportMd
