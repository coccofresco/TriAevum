[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureRoot,
    [string]$NativePrefix = "native",
    [string]$X2Prefix = "x2",
    [string]$X3Prefix = "x3",
    [double]$MinimumNormalizedRmse = 0.000001,
    [double]$MaximumX2EndpointRmseImbalance = 0.01,
    [string]$Output = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($MinimumNormalizedRmse -lt 0.0 -or $MinimumNormalizedRmse -gt 1.0) {
    throw "MinimumNormalizedRmse must be in [0, 1]."
}
if ($MaximumX2EndpointRmseImbalance -lt 0.0 -or
    $MaximumX2EndpointRmseImbalance -gt 1.0) {
    throw "MaximumX2EndpointRmseImbalance must be in [0, 1]."
}

$captureRoot = [System.IO.Path]::GetFullPath($CaptureRoot)
if (-not (Test-Path -LiteralPath $captureRoot -PathType Container)) {
    throw "Capture root is missing: $captureRoot"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $captureRoot "frame_composition_pixel_validation.json"
}
$outputPath = [System.IO.Path]::GetFullPath($Output)
$magick = (Get-Command magick -ErrorAction Stop).Source

function Get-ImageRmse {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ReferencePath,
        [Parameter(Mandatory = $true)]
        [string]$CandidatePath
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $magick
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @(
        "compare", "-metric", "RMSE", $ReferencePath, $CandidatePath,
        "null:")) {
        [void]$startInfo.ArgumentList.Add($argument)
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
    return [double]::Parse(
        $Matches[1], [Globalization.CultureInfo]::InvariantCulture)
}

function Read-CaptureSet {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Prefix
    )

    $screenshots = @(Get-ChildItem -LiteralPath $captureRoot `
        -Filter "${Prefix}_*.bmp" | Sort-Object Name)
    if ($screenshots.Count -eq 0) {
        throw "No framebuffer screenshots found for prefix '$Prefix'."
    }
    $diagnosticsPath = Join-Path $captureRoot "${Prefix}_vulkan.json"
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf)) {
        throw "Vulkan diagnostics are missing: $diagnosticsPath"
    }
    $diagnostics = Get-Content -LiteralPath $diagnosticsPath -Raw |
        ConvertFrom-Json
    $frames = @($diagnostics.frames)
    if ($frames.Count -ne $screenshots.Count) {
        throw "Capture '$Prefix' has $($screenshots.Count) screenshots but " +
            "$($frames.Count) diagnostic frames."
    }

    $samples = @()
    foreach ($frame in $frames) {
        if ([uint32]$frame.pica_scene_frame_temporal_sample_count -eq 0U) {
            continue
        }
        $screenshotIndex = [int]$frame.frame_index - 1
        if ($screenshotIndex -lt 0 -or
            $screenshotIndex -ge $screenshots.Count) {
            throw "Capture '$Prefix' has an invalid diagnostic frame index: " +
                "$($frame.frame_index)."
        }
        $screenshot = $screenshots[$screenshotIndex]
        $samples += [pscustomobject][ordered]@{
            frame_index = [uint64]$frame.frame_index
            screenshot_index = $screenshotIndex
            screenshot_path = $screenshot.FullName
            multiplier = [uint32]$frame.pica_scene_frame_temporal_sample_multiplier
            ordinal = [uint32]$frame.pica_scene_frame_temporal_sample_ordinal
            synthetic =
                [uint32]$frame.pica_scene_frame_synthetic_temporal_sample_count -ne 0U
            sha256 = (Get-FileHash -Algorithm SHA256 `
                -LiteralPath $screenshot.FullName).Hash.ToLowerInvariant()
        }
    }
    if ($samples.Count -eq 0) {
        throw "Capture '$Prefix' contains no temporal scene samples."
    }

    return [pscustomobject][ordered]@{
        prefix = $Prefix
        screenshots = $screenshots
        diagnostics_path = $diagnosticsPath
        samples = $samples
    }
}

function Measure-Mode {
    param(
        [Parameter(Mandatory = $true)]
        $Capture,
        [Parameter(Mandatory = $true)]
        [uint32]$ExpectedMultiplier,
        [Parameter(Mandatory = $true)]
        [hashtable]$NativeHashes,
        [Collections.Generic.List[string]]$Failures
    )

    $samples = @($Capture.samples)
    $authoritative = @($samples | Where-Object { -not $_.synthetic })
    $synthetic = @($samples | Where-Object { $_.synthetic })
    $authoritativeNativeMatches = @(
        $authoritative | Where-Object { $NativeHashes.ContainsKey($_.sha256) })
    $syntheticNativeMatches = @(
        $synthetic | Where-Object { $NativeHashes.ContainsKey($_.sha256) })
    $distinctSyntheticHashes = @(
        $synthetic.sha256 | Sort-Object -Unique)

    if (@($samples | Where-Object {
            $_.multiplier -ne $ExpectedMultiplier }).Count -ne 0) {
        [void]$Failures.Add(
            "$($Capture.prefix): temporal sample multiplier mismatch.")
    }
    if ($synthetic.Count -eq 0) {
        [void]$Failures.Add(
            "$($Capture.prefix): no synthetic framebuffer samples found.")
    }
    if ($authoritativeNativeMatches.Count -ne $authoritative.Count) {
        [void]$Failures.Add(
            "$($Capture.prefix): only $($authoritativeNativeMatches.Count)/" +
            "$($authoritative.Count) authoritative samples match the native capture.")
    }
    if ($syntheticNativeMatches.Count -ne 0) {
        [void]$Failures.Add(
            "$($Capture.prefix): $($syntheticNativeMatches.Count) synthetic " +
            "samples duplicate native framebuffers.")
    }
    if ($distinctSyntheticHashes.Count -ne $synthetic.Count) {
        [void]$Failures.Add(
            "$($Capture.prefix): synthetic samples are not all distinct in " +
            "the qualified moving sequence.")
    }

    $transitions = @()
    for ($sampleIndex = 0; $sampleIndex -lt $samples.Count; ++$sampleIndex) {
        $sample = $samples[$sampleIndex]
        if (-not $sample.synthetic) {
            continue
        }
        if ($sample.ordinal -eq 0U -or
            $sample.ordinal -ge $ExpectedMultiplier) {
            [void]$Failures.Add(
                "$($Capture.prefix): synthetic frame $($sample.frame_index) " +
                "has invalid ordinal $($sample.ordinal).")
        }

        $previousIndex = $sampleIndex - 1
        while ($previousIndex -ge 0 -and
            $samples[$previousIndex].synthetic) {
            --$previousIndex
        }
        $nextIndex = $sampleIndex + 1
        while ($nextIndex -lt $samples.Count -and
            $samples[$nextIndex].synthetic) {
            ++$nextIndex
        }
        if ($previousIndex -lt 0 -or $nextIndex -ge $samples.Count) {
            continue
        }

        $previous = $samples[$previousIndex]
        $next = $samples[$nextIndex]
        $rmsePrevious = Get-ImageRmse `
            -ReferencePath $previous.screenshot_path `
            -CandidatePath $sample.screenshot_path
        $rmseNext = Get-ImageRmse `
            -ReferencePath $sample.screenshot_path `
            -CandidatePath $next.screenshot_path
        if ($rmsePrevious -le $MinimumNormalizedRmse -or
            $rmseNext -le $MinimumNormalizedRmse) {
            [void]$Failures.Add(
                "$($Capture.prefix): synthetic frame $($sample.frame_index) " +
                "is not pixel-distinct from both native endpoints.")
        }

        $phaseCorrect = $true
        if ($ExpectedMultiplier -eq 2U) {
            $phaseCorrect =
                [math]::Abs($rmsePrevious - $rmseNext) -le
                    $MaximumX2EndpointRmseImbalance
        } elseif ($sample.ordinal -eq 1U) {
            $phaseCorrect = $rmsePrevious -lt $rmseNext
        } elseif ($sample.ordinal -eq 2U) {
            $phaseCorrect = $rmseNext -lt $rmsePrevious
        }
        if (-not $phaseCorrect) {
            [void]$Failures.Add(
                "$($Capture.prefix): synthetic frame $($sample.frame_index) " +
                "does not follow ordinal $($sample.ordinal)/" +
                "$ExpectedMultiplier framebuffer progression.")
        }
        $transitions += [ordered]@{
            frame_index = $sample.frame_index
            ordinal = $sample.ordinal
            previous_frame_index = $previous.frame_index
            next_frame_index = $next.frame_index
            rmse_from_previous = $rmsePrevious
            rmse_from_next = $rmseNext
            phase_correct = $phaseCorrect
        }
    }
    if ($transitions.Count -eq 0) {
        [void]$Failures.Add(
            "$($Capture.prefix): no complete synthetic transition was measured.")
    }

    $previousMetrics = @($transitions | ForEach-Object {
        [double]$_.rmse_from_previous
    })
    $nextMetrics = @($transitions | ForEach-Object {
        [double]$_.rmse_from_next
    })
    return [ordered]@{
        prefix = $Capture.prefix
        expected_multiplier = $ExpectedMultiplier
        temporal_samples = $samples.Count
        authoritative_samples = $authoritative.Count
        authoritative_exact_native_matches =
            $authoritativeNativeMatches.Count
        synthetic_samples = $synthetic.Count
        synthetic_native_hash_collisions = $syntheticNativeMatches.Count
        distinct_synthetic_hashes = $distinctSyntheticHashes.Count
        complete_transition_samples = $transitions.Count
        phase_correct_transition_samples = @(
            $transitions | Where-Object { $_.phase_correct }).Count
        minimum_rmse_from_previous =
            [double](($previousMetrics | Measure-Object -Minimum).Minimum)
        maximum_rmse_from_previous =
            [double](($previousMetrics | Measure-Object -Maximum).Maximum)
        minimum_rmse_from_next =
            [double](($nextMetrics | Measure-Object -Minimum).Minimum)
        maximum_rmse_from_next =
            [double](($nextMetrics | Measure-Object -Maximum).Maximum)
        transitions = $transitions
    }
}

$nativeCapture = Read-CaptureSet -Prefix $NativePrefix
$x2Capture = Read-CaptureSet -Prefix $X2Prefix
$x3Capture = Read-CaptureSet -Prefix $X3Prefix
$nativeHashes = @{}
foreach ($screenshot in $nativeCapture.screenshots) {
    $hash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $screenshot.FullName).Hash.ToLowerInvariant()
    $nativeHashes[$hash] = $true
}

$failures = [Collections.Generic.List[string]]::new()
$x2 = Measure-Mode -Capture $x2Capture -ExpectedMultiplier 2U `
    -NativeHashes $nativeHashes -Failures $failures
$x3 = Measure-Mode -Capture $x3Capture -ExpectedMultiplier 3U `
    -NativeHashes $nativeHashes -Failures $failures

$report = [ordered]@{
    schema = "oot3d.native_frame_composition_pixel_validation.v1"
    status = if ($failures.Count -eq 0) { "pass" } else { "fail" }
    capture_root = $captureRoot
    framebuffer_source = "Fast3D ReadFramebufferToCPU before host presentation"
    native_screenshot_count = $nativeCapture.screenshots.Count
    minimum_normalized_rmse = $MinimumNormalizedRmse
    maximum_x2_endpoint_rmse_imbalance =
        $MaximumX2EndpointRmseImbalance
    x2 = $x2
    x3 = $x3
    failures = @($failures)
}
$outputDirectory = Split-Path -Parent $outputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$report | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath $outputPath -Encoding utf8

Write-Output ((
    "Native-frame composition pixel gate: {0}; x2 synthetic={1}, " +
    "native-collisions={2}; x3 synthetic={3}, native-collisions={4}; " +
    "report {5}") -f
        $report.status,
        $x2.synthetic_samples,
        $x2.synthetic_native_hash_collisions,
        $x3.synthetic_samples,
        $x3.synthetic_native_hash_collisions,
        $outputPath)
if ($failures.Count -ne 0) {
    throw ($failures -join [Environment]::NewLine)
}
