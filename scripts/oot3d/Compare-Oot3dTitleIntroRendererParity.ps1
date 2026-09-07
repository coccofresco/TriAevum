[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureRoot,
    [string]$CheckpointFrames =
        "6,240,600,900,1200,1500,1800,2100,2250,2500",
    [double]$MaximumNormalizedRmse = 0.002
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$CaptureRoot = [System.IO.Path]::GetFullPath($CaptureRoot)
$magick = (Get-Command magick -ErrorAction Stop).Source
$frames = @($CheckpointFrames.Split(',') | ForEach-Object {
    [int]::Parse($_.Trim(), [Globalization.CultureInfo]::InvariantCulture)
})
if ($frames.Count -eq 0 -or @($frames | Where-Object { $_ -lt 0 }).Count -ne 0) {
    throw "CheckpointFrames must contain non-negative comma-separated integers."
}
if ($MaximumNormalizedRmse -lt 0.0 -or $MaximumNormalizedRmse -gt 1.0) {
    throw "MaximumNormalizedRmse must be in [0, 1]."
}

function ConvertTo-CompactJson {
    param($Value)
    return ($Value | ConvertTo-Json -Depth 100 -Compress)
}

function Assert-JsonEqual {
    param($Reference, $Candidate, [string]$Label)
    if ((ConvertTo-CompactJson $Reference) -cne (ConvertTo-CompactJson $Candidate)) {
        throw "OpenGL/Vulkan title-intro state mismatch: $Label"
    }
}

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

$rows = @()
foreach ($frame in $frames) {
    $label = "{0:0000}" -f $frame
    $name = "checkpoint_frame_$label"
    $glJsonPath = Join-Path $CaptureRoot "opengl\$name.json"
    $vkJsonPath = Join-Path $CaptureRoot "vulkan\$name.json"
    $glBmpPath = Join-Path $CaptureRoot "opengl\$name.bmp"
    $vkBmpPath = Join-Path $CaptureRoot "vulkan\$name.bmp"
    $glSmokePath = Join-Path $CaptureRoot "opengl\$name.smoke.json"
    $vkSmokePath = Join-Path $CaptureRoot "vulkan\$name.smoke.json"
    foreach ($path in @(
        $glJsonPath, $vkJsonPath, $glBmpPath, $vkBmpPath,
        $glSmokePath, $vkSmokePath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing title-intro parity artifact: $path"
        }
    }

    $gl = Get-Content -LiteralPath $glJsonPath -Raw | ConvertFrom-Json
    $vk = Get-Content -LiteralPath $vkJsonPath -Raw | ConvertFrom-Json
    $glSmoke = Get-Content -LiteralPath $glSmokePath -Raw | ConvertFrom-Json
    $vkSmoke = Get-Content -LiteralPath $vkSmokePath -Raw | ConvertFrom-Json
    if (-not ([string]$glSmoke.status).StartsWith("pass_") -or
        -not ([string]$vkSmoke.status).StartsWith("pass_")) {
        throw "Title-intro smoke failed at frame $frame"
    }

    $glTitleRuntime = $gl.title_intro_runtime |
        ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $vkTitleRuntime = $vk.title_intro_runtime |
        ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $glTitleRuntime.PSObject.Properties.Remove("performance")
    $vkTitleRuntime.PSObject.Properties.Remove("performance")
    Assert-JsonEqual $glTitleRuntime $vkTitleRuntime "frame $frame title_intro_runtime"
    Assert-JsonEqual $gl.cutscene_player $vk.cutscene_player "frame $frame cutscene_player"
    Assert-JsonEqual $gl.camera $vk.camera "frame $frame camera"
    Assert-JsonEqual $gl.title_intro_visibility $vk.title_intro_visibility `
        "frame $frame visibility"

    $adapterKeys = @(
        "backend_draw_call_count",
        "triangle_count",
        "missing_texture_batch_count",
        "shader_input_layout_mismatch_count",
        "native_blend_state_backend_unsupported_batch_count",
        "native_cull_state_backend_unsupported_batch_count",
        "native_pica_alpha_test_backend_unsupported_batch_count",
        "native_pica_texture2_backend_unsupported_batch_count",
        "native_pica_fog_pending_batch_count"
    )
    $adapterGl = [ordered]@{}
    $adapterVk = [ordered]@{}
    foreach ($key in $adapterKeys) {
        $adapterGl[$key] = $gl.fast3d_adapter_lifetime.$key
        $adapterVk[$key] = $vk.fast3d_adapter_lifetime.$key
    }
    Assert-JsonEqual $adapterGl $adapterVk "frame $frame Fast3D counters"
    if ([int]$adapterGl.missing_texture_batch_count -ne 0 -or
        [int]$adapterVk.missing_texture_batch_count -ne 0 -or
        [int]$adapterGl.shader_input_layout_mismatch_count -ne 0 -or
        [int]$adapterVk.shader_input_layout_mismatch_count -ne 0) {
        throw "Title-intro checkpoint $frame has missing textures or shader layout mismatches"
    }
    if ([bool]$gl.scene.runtime_n64_asset_substitution_used -or
        [bool]$vk.scene.runtime_n64_asset_substitution_used -or
        [bool]$gl.scene.shipwright_replacement_path_used -or
        [bool]$vk.scene.shipwright_replacement_path_used) {
        throw "Title-intro checkpoint $frame used an N64/Shipwright runtime content path"
    }
    foreach ($summary in @($gl, $vk)) {
        if ([bool]$summary.framebuffer_screenshot.depends_on_windows_foreground -or
            [string]$summary.framebuffer_screenshot.source -ne
                "fast3d_rendering_api_read_framebuffer_to_cpu") {
            throw "Title-intro checkpoint $frame lacks direct framebuffer provenance"
        }
    }

    $metric = Get-ImageRmse -ReferencePath $glBmpPath -CandidatePath $vkBmpPath
    $rows += [ordered]@{
        frame = $frame
        draw_calls = [int]$adapterGl.backend_draw_call_count
        triangles = [int]$adapterGl.triangle_count
        normalized_rmse = $metric.normalized
        rmse_raw = $metric.raw
        pass = $metric.normalized -le $MaximumNormalizedRmse
    }
}

$maximumRmse = [double]((@($rows | ForEach-Object {
    [double]$_['normalized_rmse']
}) | Measure-Object -Maximum).Maximum)
$status = if ($maximumRmse -le $MaximumNormalizedRmse) { "pass" } else { "fail" }
$report = [ordered]@{
    schema = "oot3d.title_intro.renderer_parity.v1"
    status = $status
    checkpoint_frames = $frames
    semantic_state_exact = $true
    excluded_semantic_field = "title_intro_runtime.performance"
    exclusion_reason = "wall-clock CPU telemetry is not renderer functional state"
    runtime_n64_asset_substitution_used = $false
    shipwright_replacement_path_used = $false
    framebuffer_source = "Fast3D rendering API ReadFramebufferToCPU"
    depends_on_windows_foreground = $false
    maximum_normalized_rmse = $maximumRmse
    maximum_allowed_normalized_rmse = $MaximumNormalizedRmse
    checkpoints = $rows
}
$reportJson = Join-Path $CaptureRoot "renderer_parity.json"
$reportMd = Join-Path $CaptureRoot "renderer_parity.md"
$report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $reportJson -Encoding utf8
$markdown = @(
    "# OOT3D title intro OpenGL/Vulkan parity",
    "",
    "Status: **$status**",
    "",
    "- Semantic runtime state: exact at all $($frames.Count) checkpoints",
    "- Excluded field: title_intro_runtime.performance (wall-clock telemetry)",
    "- Maximum normalized framebuffer RMSE: $maximumRmse",
    "- Missing texture batches: 0",
    "- Runtime N64/Shipwright content paths: not used",
    "- Captures: direct rendering API framebuffer readback; no Windows foreground dependency",
    "",
    "| Frame | Draw calls | Triangles | Normalized RMSE | Pass |",
    "| -: | -: | -: | -: | :--: |"
)
foreach ($row in $rows) {
    $markdown += "| $($row['frame']) | $($row['draw_calls']) | $($row['triangles']) | " +
        "$($row['normalized_rmse']) | $($row['pass']) |"
}
$markdown | Set-Content -LiteralPath $reportMd -Encoding utf8

if ($status -ne "pass") {
    throw "Title-intro OpenGL/Vulkan RMSE $maximumRmse exceeds $MaximumNormalizedRmse; see $reportMd"
}
Write-Output $reportMd
