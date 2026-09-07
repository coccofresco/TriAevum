[CmdletBinding()]
param(
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$ScenarioCatalog = "",
    [string]$ArtifactRoot = "I:\oot3dre_work\native_game",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [ValidateSet("oot3d", "topscreen")]
    [string]$UiProfile = "topscreen",
    [string]$OutputRoot =
        "I:\oot3dre_work\whole-aot-product-run\resume-matrices",
    [string[]]$Scenario = @(),
    [ValidateRange(10, 3600)]
    [double]$RunTimeoutSeconds = 240,
    [switch]$KeepSuccessfulMedia,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
if([string]::IsNullOrWhiteSpace($ScenarioCatalog)) {
    $ScenarioCatalog = Join-Path $repoRoot `
        "tools\oot3d\native_a32_runtime\whole_aot_product_scenarios.json"
}
$resolvedExecutable = [System.IO.Path]::GetFullPath($ProductExecutable)
$resolvedCatalog = [System.IO.Path]::GetFullPath($ScenarioCatalog)
$resolvedArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$resolvedGraphics = [System.IO.Path]::GetFullPath($GraphicsConfig)
$resolvedSaveData = [System.IO.Path]::GetFullPath($SaveDataDirectory)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
foreach($required in @(
        $launcher,
        $resolvedExecutable,
        $resolvedCatalog,
        $resolvedGraphics)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required whole-AOT resume input is missing: $required"
    }
}
foreach($requiredDirectory in @($resolvedArtifactRoot, $resolvedSaveData)) {
    if(-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required whole-AOT resume directory is missing: $requiredDirectory"
    }
}

function Get-FileIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        bytes = [uint64]$item.Length
        sha256 = (Get-FileHash -LiteralPath $item.FullName `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        retained = $true
    }
}

function Assert-PathBelow {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $resolved = [System.IO.Path]::GetFullPath($Path)
    $prefix = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    if(-not $resolved.StartsWith(
            $prefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escapes its root: $resolved"
    }
    return $resolved
}

function Get-SaveDataFingerprint {
    param([Parameter(Mandatory = $true)][string]$Path)
    $records = @(
        Get-ChildItem -LiteralPath $Path -Recurse -File |
            Where-Object {
                $_.Extension -ne ".oot3dsav" -and
                $_.DirectoryName -notlike "*\crashes*"
            } |
            Sort-Object FullName |
            ForEach-Object {
                $relative = [System.IO.Path]::GetRelativePath(
                    $Path, $_.FullName).Replace('\', '/')
                $hash = (Get-FileHash -LiteralPath $_.FullName `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
                "$relative|$($_.Length)|$hash"
            })
    $payload = [Text.Encoding]::UTF8.GetBytes(($records -join "`n"))
    return [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($payload)).ToLowerInvariant()
}

function Copy-SaveDataBaseline {
    param([Parameter(Mandatory = $true)][string]$Destination)
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $resolvedSaveData -Force |
        Where-Object {
            $_.Name -ne "crashes" -and $_.Extension -ne ".oot3dsav"
        } |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $Destination `
                -Recurse -Force
        }
}

function Write-DeterministicGraphicsConfig {
    param([Parameter(Mandatory = $true)][string]$Destination)
    $config = Get-Content -LiteralPath $resolvedGraphics -Raw |
        ConvertFrom-Json
    if($null -eq $config.Graphics -or
       $null -eq $config.Graphics.Output -or
       $null -eq $config.Graphics.Presentation -or
       $null -eq $config.Window) {
        throw "Graphics config lacks deterministic presentation settings"
    }
    $config.Graphics.Output.Width = 1280
    $config.Graphics.Output.Height = 720
    $config.Graphics.Output.RefreshRate = 60
    $config.Graphics.Presentation.VSync = $false
    $config.Window.Width = 1280
    $config.Window.Height = 720
    $config | ConvertTo-Json -Depth 32 |
        Set-Content -LiteralPath $Destination -Encoding utf8
    $written = Get-Content -LiteralPath $Destination -Raw |
        ConvertFrom-Json
    if([bool]$written.Graphics.Presentation.VSync -or
       [int]$written.Graphics.Output.Width -ne 1280 -or
       [int]$written.Graphics.Output.Height -ne 720) {
        throw "Derived resume graphics config is not deterministic"
    }
}

function New-InputTimeline {
    param(
        [Parameter(Mandatory = $true)][int]$Frames,
        [Parameter(Mandatory = $true)][string]$Profile,
        [Parameter(Mandatory = $true)][uint32]$Seed
    )
    if($Profile -eq "idle") {
        return [ordered]@{
            schema = "oot3d.native_game.input_timeline.v1"
            frame_origin = "run"
            segments = @([ordered]@{
                start_frame = 0
                end_frame_exclusive = $Frames
                circle_x = 0.0
                circle_y = 0.0
            })
        }
    }
    if($Profile -eq "forward") {
        return [ordered]@{
            schema = "oot3d.native_game.input_timeline.v1"
            frame_origin = "run"
            segments = @([ordered]@{
                start_frame = 0
                end_frame_exclusive = $Frames
                circle_x = 0.0
                circle_y = 0.85
            })
        }
    }

    $directions = @(
        @(0.0, 0.85), @(0.65, 0.70), @(0.85, 0.0),
        @(0.65, -0.70), @(0.0, -0.80), @(-0.65, -0.70),
        @(-0.85, 0.0), @(-0.65, 0.70), @(0.0, 0.0))
    $buttons = @("a", "b", "r", "x", "y")
    $segments = [System.Collections.Generic.List[object]]::new()
    $cursor = 0
    $phase = 0
    $randomState = [uint64]$Seed
    while($cursor -lt $Frames) {
        $randomState = ($randomState * 1664525L + 1013904223L) `
            -band 0xFFFFFFFFL
        if(($phase % 6) -eq 5) {
            $duration = 1
            $record = [ordered]@{
                start_frame = $cursor
                end_frame_exclusive = [Math]::Min($cursor + 1, $Frames)
                buttons = @($buttons[[int]($randomState % $buttons.Count)])
            }
        } else {
            $duration = 55 + [int]($randomState % 86)
            $direction = $directions[[int]($randomState % $directions.Count)]
            $record = [ordered]@{
                start_frame = $cursor
                end_frame_exclusive =
                    [Math]::Min($cursor + $duration, $Frames)
                circle_x = [double]$direction[0]
                circle_y = [double]$direction[1]
            }
            if(($phase % 3) -eq 2) {
                $record.right_x = if(($randomState -band 0x100U) -ne 0) {
                    0.45
                } else {
                    -0.45
                }
            }
        }
        $segments.Add($record) | Out-Null
        $cursor = [int]$record.end_frame_exclusive
        ++$phase
    }
    return [ordered]@{
        schema = "oot3d.native_game.input_timeline.v1"
        frame_origin = "run"
        segments = $segments
    }
}

function Get-TimelineSlice {
    param(
        [Parameter(Mandatory = $true)]$Timeline,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Frames
    )
    $sliceEnd = $Offset + $Frames
    $segments = [System.Collections.Generic.List[object]]::new()
    foreach($segment in @($Timeline.segments)) {
        $start = [int]$segment.start_frame
        $end = [int]$segment.end_frame_exclusive
        if($end -le $Offset -or $start -ge $sliceEnd) {
            continue
        }
        $record = [ordered]@{
            start_frame = [Math]::Max($start, $Offset) - $Offset
            end_frame_exclusive = [Math]::Min($end, $sliceEnd) - $Offset
        }
        if($segment -is [System.Collections.IDictionary]) {
            foreach($key in @($segment.Keys)) {
                if([string]$key -notin @(
                        "start_frame", "end_frame_exclusive")) {
                    $record[[string]$key] = $segment[$key]
                }
            }
        } else {
            foreach($property in $segment.PSObject.Properties) {
                if($property.Name -notin @(
                        "start_frame", "end_frame_exclusive")) {
                    $record[$property.Name] = $property.Value
                }
            }
        }
        $segments.Add($record) | Out-Null
    }
    if($segments.Count -eq 0 -or
       [int]$segments[0].start_frame -ne 0 -or
       [int]$segments[$segments.Count - 1].end_frame_exclusive -ne $Frames) {
        throw "Input timeline slice does not cover all requested frames"
    }
    return [ordered]@{
        schema = "oot3d.native_game.input_timeline.v1"
        frame_origin = [string]$Timeline.frame_origin
        segments = $segments
    }
}

function Write-JsonArtifact {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $Value | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-WavePayload {
    param([Parameter(Mandatory = $true)][string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if($bytes.Length -lt 12 -or
       [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "RIFF" -or
       [Text.Encoding]::ASCII.GetString($bytes, 8, 4) -ne "WAVE") {
        throw "Audio artifact is not a RIFF/WAVE file: $Path"
    }
    $format = $null
    $payload = $null
    $offset = 12
    while($offset + 8 -le $bytes.Length) {
        $chunkName = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $chunkSize = [uint32][BitConverter]::ToUInt32($bytes, $offset + 4)
        $chunkStart = $offset + 8
        $chunkEnd = [uint64]$chunkStart + [uint64]$chunkSize
        if($chunkEnd -gt [uint64]$bytes.Length) {
            throw "Truncated WAVE chunk '$chunkName': $Path"
        }
        if($chunkName -eq "fmt ") {
            $format = [byte[]]::new($chunkSize)
            [Array]::Copy($bytes, $chunkStart, $format, 0, $chunkSize)
        } elseif($chunkName -eq "data") {
            $payload = [byte[]]::new($chunkSize)
            [Array]::Copy($bytes, $chunkStart, $payload, 0, $chunkSize)
        }
        $offset = [int]($chunkEnd + ($chunkSize -band 1U))
    }
    if($null -eq $format -or $null -eq $payload) {
        throw "WAVE file has no fmt or data chunk: $Path"
    }
    return [pscustomobject]@{
        format_sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($format)).ToLowerInvariant()
        pcm_bytes = [uint64]$payload.Length
        pcm_sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($payload)).ToLowerInvariant()
        payload = $payload
    }
}

function Get-ConcatenatedSha256 {
    param([Parameter(Mandatory = $true)][byte[][]]$Payloads)
    $hash = [Security.Cryptography.IncrementalHash]::CreateHash(
        [Security.Cryptography.HashAlgorithmName]::SHA256)
    try {
        foreach($payload in $Payloads) {
            $hash.AppendData($payload)
        }
        return [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

$catalog = Get-Content -LiteralPath $resolvedCatalog -Raw | ConvertFrom-Json
if([string]$catalog.format -ne "oot3d_whole_aot_scenario_catalog_v1") {
    throw "Unsupported whole-AOT scenario catalog: $resolvedCatalog"
}
$checkpointRoot = Assert-PathBelow `
    -Path (Join-Path $resolvedArtifactRoot ([string]$catalog.checkpoint_root)) `
    -Root $resolvedArtifactRoot -Description "Checkpoint root"
if(-not (Test-Path -LiteralPath $checkpointRoot -PathType Container)) {
    throw "Scenario checkpoint root is missing: $checkpointRoot"
}

$requestedNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach($name in $Scenario) {
    if(-not [string]::IsNullOrWhiteSpace($name)) {
        $null = $requestedNames.Add($name)
    }
}
$seenNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$resolvedScenarios = [System.Collections.Generic.List[object]]::new()
foreach($entry in @($catalog.scenarios)) {
    $name = [string]$entry.name
    $null = $seenNames.Add($name)
    if($requestedNames.Count -ne 0 -and -not $requestedNames.Contains($name)) {
        continue
    }
    if($null -eq $entry.resume_validation) {
        if($requestedNames.Contains($name)) {
            throw "Scenario $name has no resume_validation contract"
        }
        continue
    }
    $totalFrames = [int]$entry.resume_validation.total_frames
    $splitFrame = [int]$entry.resume_validation.split_frame
    if($totalFrames -lt 2 -or $splitFrame -lt 1 -or
       $splitFrame -ge $totalFrames) {
        throw "Scenario $name has an invalid resume split"
    }
    $profile = [string]$entry.input_profile
    if($profile -notin @("exploration", "idle", "forward")) {
        throw "Scenario $name has unsupported input profile: $profile"
    }
    $statePath = Assert-PathBelow `
        -Path (Join-Path $checkpointRoot ([string]$entry.state.path)) `
        -Root $checkpointRoot -Description "Scenario state"
    if(-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw "Scenario $name state is missing: $statePath"
    }
    $identity = Get-FileIdentity $statePath
    if([uint64]$identity.bytes -ne [uint64]$entry.state.bytes -or
       [string]$identity.sha256 -ne [string]$entry.state.sha256) {
        throw "Scenario $name state identity differs from the catalog"
    }
    $resolvedScenarios.Add([pscustomobject][ordered]@{
        name = $name
        category = [string]$entry.category
        description = [string]$entry.description
        state = $identity
        input_profile = $profile
        seed = [uint32]$entry.seed
        total_frames = $totalFrames
        split_frame = $splitFrame
    }) | Out-Null
}
if($requestedNames.Count -ne 0) {
    $missingNames = @($requestedNames | Where-Object {
        -not $seenNames.Contains($_)
    })
    if($missingNames.Count -ne 0) {
        throw "Unknown requested scenarios: $($missingNames -join ', ')"
    }
}
if($resolvedScenarios.Count -eq 0) {
    throw "The whole-AOT resume scenario selection is empty"
}

if($ValidateOnly.IsPresent) {
    [ordered]@{
        format = "oot3d_whole_aot_resume_catalog_validation_v1"
        valid = $true
        catalog = Get-FileIdentity $resolvedCatalog
        checkpoint_root = $checkpointRoot
        selected_scenarios = $resolvedScenarios.Count
        categories = @($resolvedScenarios.category | Sort-Object -Unique)
    } | ConvertTo-Json -Depth 5
    return
}

$startedUtc = [DateTime]::UtcNow
$runId = $startedUtc.ToString("yyyyMMddTHHmmssZ")
$runRoot = Join-Path $resolvedOutputRoot $runId
$runPrefix = [System.IO.Path]::GetFullPath($runRoot).TrimEnd('\', '/') +
    [System.IO.Path]::DirectorySeparatorChar
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$hostPowerShell = (Get-Process -Id $PID).Path
$fixedDelta = (1.0 / 60.0).ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)
$timeoutText = $RunTimeoutSeconds.ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)

function Assert-RunPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if(-not $resolved.StartsWith(
            $runPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Resume artifact escapes run root: $resolved"
    }
    return $resolved
}

function Invoke-IsolatedLauncher {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $hostPowerShell
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach($argument in @("-NoLogo", "-NoProfile", "-File", $launcher) +
        $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    @($stdout, $stderr) | Set-Content -LiteralPath $LogPath -Encoding utf8
    return $process.ExitCode
}

function Invoke-ResumeRun {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$Frames,
        [Parameter(Mandatory = $true)][string]$StartState,
        [Parameter(Mandatory = $true)][string]$InputTimeline,
        [Parameter(Mandatory = $true)][string]$SaveData,
        [Parameter(Mandatory = $true)][string]$Graphics,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ExpectedInputProfile,
        [switch]$RequireInputAction
    )
    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    $runtimePath = Join-Path $Root "runtime.json"
    $statePath = Join-Path $Root "end.oot3dsav"
    $framePath = Join-Path $Root "frame.bmp"
    $audioPath = Join-Path $Root "audio.wav"
    $logPath = Join-Path $Root "process.log"
    $arguments = @(
        "-SkipBuild",
        "-Executable", $resolvedExecutable,
        "-Renderer", "nri",
        "-UiProfile", $UiProfile,
        "-Frames", [string]$Frames,
        "-MaxSeconds", $timeoutText,
        "-FixedDeltaSeconds", $fixedDelta,
        "-SimulationRate", "30",
        "-PresentationRate", "free",
        "-Width", "1280", "-Height", "720",
        "-GraphicsConfig", $Graphics,
        "-SaveDataDirectory", $SaveData,
        "-LoadState", $StartState,
        "-InputTimeline", $InputTimeline,
        "-SaveState", $statePath,
        "-SaveStateFrame", [string]($Frames - 1),
        "-Screenshot", $framePath,
        "-ScreenshotStartFrame", [string]($Frames - 1),
        "-AudioPcmDump", $audioPath,
        "-Output", $runtimePath)
    $exitCode = Invoke-IsolatedLauncher -Arguments $arguments -LogPath $logPath
    if($exitCode -ne 0) {
        throw "$Name launcher exited with code $exitCode; see $logPath"
    }
    foreach($artifact in @($runtimePath, $statePath, $framePath, $audioPath)) {
        if(-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "$Name did not produce: $artifact"
        }
    }
    $runtime = Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json
    $strictCounters = [ordered]@{
        retained_arm_fallbacks =
            [uint64]$runtime.compiled_functions.retained_arm_fallbacks
        whole_aot_memory_faults =
            [uint64]$runtime.compiled_functions.whole_aot_memory_faults
        whole_aot_unsupported_exits =
            [uint64]$runtime.compiled_functions.whole_aot_unsupported_exits
        mass_aot_block_limit_exits =
            [uint64]$runtime.compiled_functions.mass_aot_block_limit_exits
    }
    $strictFailures = @($strictCounters.GetEnumerator() |
        Where-Object { $_.Value -ne 0 })
    $crashDumps = @(Get-ChildItem (Join-Path $SaveData "crashes") `
        -Filter *.dmp -ErrorAction SilentlyContinue)
    $input = $runtime.hid_input
    $inputActionFrames = [uint64]$input.button_host_frames +
        [uint64]$input.circle_pad_host_frames +
        [uint64]$input.touch_host_frames
    if([uint64]$runtime.run_frames -ne [uint64]$Frames -or
       -not [bool]$runtime.compiled_functions.whole_aot_available -or
       -not [bool]$runtime.compiled_functions.whole_aot_enabled -or
       [string]$input.timeline_frame_origin -ne "run" -or
       [uint64]$input.sampled_host_frames -eq 0U -or
       [uint64]$input.active_segment_host_frames -ne
           [uint64]$input.sampled_host_frames -or
       ($RequireInputAction.IsPresent -and
           $ExpectedInputProfile -ne "idle" -and
           $inputActionFrames -eq 0U) -or
       $strictFailures.Count -ne 0 -or $crashDumps.Count -ne 0) {
        throw "$Name violated the strict whole-AOT contract"
    }
    $wave = Get-WavePayload $audioPath
    $record = [pscustomobject][ordered]@{
        name = $Name
        frames = $Frames
        runtime = Get-FileIdentity $runtimePath
        end_state = Get-FileIdentity $statePath
        framebuffer = Get-FileIdentity $framePath
        audio = Get-FileIdentity $audioPath
        audio_format_sha256 = $wave.format_sha256
        pcm_bytes = $wave.pcm_bytes
        pcm_sha256 = $wave.pcm_sha256
        savedata_sha256 = Get-SaveDataFingerprint $SaveData
        savestate_semantic_fingerprint =
            [string]$runtime.savestates.last_semantic_fingerprint
        memory_content_fingerprint =
            [string]$runtime.memory_content_fingerprint
        memory_state_fingerprint = [string]$runtime.memory_state_fingerprint
        process_state_fingerprint = [string]$runtime.process_state_fingerprint
        terminal_guest_frame = [uint64]$runtime.frames
        terminal_guest_refresh_frame = [uint64]$runtime.guest_refresh_frames
        calls = [ordered]@{
            whole_aot = [uint64]$runtime.compiled_functions.whole_aot_calls
            direct = [uint64]$runtime.compiled_functions.whole_aot_direct_calls
            indirect = [uint64]$runtime.compiled_functions.whole_aot_indirect_calls
            external = [uint64]$runtime.compiled_functions.whole_aot_external_calls
            draws = [uint64]$runtime.draws_submitted
            display_transfers = [uint64]$runtime.display_transfers_submitted
        }
        strict_counters = $strictCounters
        crash_dumps = $crashDumps.Count
        log = $logPath
        input = [ordered]@{
            frame_origin = [string]$input.timeline_frame_origin
            sampled_frames = [uint64]$input.sampled_host_frames
            active_frames = [uint64]$input.active_segment_host_frames
            button_frames = [uint64]$input.button_host_frames
            circle_pad_frames = [uint64]$input.circle_pad_host_frames
            touch_frames = [uint64]$input.touch_host_frames
        }
    }
    return [pscustomobject]@{
        record = $record
        wave = $wave
    }
}

function Add-Difference {
    param(
        [Parameter(Mandatory = $true)]$List,
        [Parameter(Mandatory = $true)][string]$Field,
        $Continuous,
        $Resumed
    )
    if([string]$Continuous -ne [string]$Resumed) {
        $List.Add([ordered]@{
            field = $Field
            continuous = $Continuous
            resumed = $Resumed
        }) | Out-Null
    }
}

function Remove-SuccessArtifact {
    param(
        [Parameter(Mandatory = $true)]$Identity,
        [Parameter(Mandatory = $true)][string]$RunRoot
    )
    $path = [System.IO.Path]::GetFullPath([string]$Identity.path)
    $rootPrefix = [System.IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    if(-not $path.StartsWith(
            $rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove artifact outside scenario root: $path"
    }
    if(Test-Path -LiteralPath $path -PathType Leaf) {
        [System.IO.File]::Delete($path)
    }
    $Identity.retained = $false
}

$scenarioResults = [System.Collections.Generic.List[object]]::new()
$failures = [System.Collections.Generic.List[object]]::new()
foreach($resolvedScenario in $resolvedScenarios) {
    $scenarioRoot = Assert-RunPath (Join-Path $runRoot (
        [string]$resolvedScenario.name))
    New-Item -ItemType Directory -Force -Path $scenarioRoot | Out-Null
    $continuousRoot = Join-Path $scenarioRoot "continuous"
    $segmentARoot = Join-Path $scenarioRoot "resumed\segment_a"
    $segmentBRoot = Join-Path $scenarioRoot "resumed\segment_b"
    $continuousSaveData = Join-Path $continuousRoot "savedata"
    $resumedSaveData = Join-Path $scenarioRoot "resumed\savedata"
    $continuousGraphics = Join-Path $continuousRoot "graphics.json"
    $resumedGraphics = Join-Path $scenarioRoot "resumed\graphics.json"
    $timelineRoot = Join-Path $scenarioRoot "timelines"
    New-Item -ItemType Directory -Force -Path $timelineRoot | Out-Null
    Copy-SaveDataBaseline $continuousSaveData
    Copy-SaveDataBaseline $resumedSaveData
    Write-DeterministicGraphicsConfig $continuousGraphics
    Write-DeterministicGraphicsConfig $resumedGraphics

    $fullTimeline = New-InputTimeline -Frames $resolvedScenario.total_frames `
        -Profile $resolvedScenario.input_profile -Seed $resolvedScenario.seed
    $firstTimeline = Get-TimelineSlice -Timeline $fullTimeline -Offset 0 `
        -Frames $resolvedScenario.split_frame
    $remainingFrames = $resolvedScenario.total_frames -
        $resolvedScenario.split_frame
    $secondTimeline = Get-TimelineSlice -Timeline $fullTimeline `
        -Offset $resolvedScenario.split_frame -Frames $remainingFrames
    $fullTimelinePath = Join-Path $timelineRoot "continuous.json"
    $firstTimelinePath = Join-Path $timelineRoot "segment_a.json"
    $secondTimelinePath = Join-Path $timelineRoot "segment_b.json"
    Write-JsonArtifact $fullTimeline $fullTimelinePath
    Write-JsonArtifact $firstTimeline $firstTimelinePath
    Write-JsonArtifact $secondTimeline $secondTimelinePath

    $continuous = $null
    $segmentA = $null
    $segmentB = $null
    $differences = [System.Collections.Generic.List[object]]::new()
    $errorMessage = ""
    try {
        $continuous = Invoke-ResumeRun -Name "continuous" `
            -Frames $resolvedScenario.total_frames `
            -StartState $resolvedScenario.state.path `
            -InputTimeline $fullTimelinePath -SaveData $continuousSaveData `
            -Graphics $continuousGraphics -Root $continuousRoot `
            -ExpectedInputProfile $resolvedScenario.input_profile `
            -RequireInputAction
        $segmentA = Invoke-ResumeRun -Name "segment_a" `
            -Frames $resolvedScenario.split_frame `
            -StartState $resolvedScenario.state.path `
            -InputTimeline $firstTimelinePath -SaveData $resumedSaveData `
            -Graphics $resumedGraphics -Root $segmentARoot `
            -ExpectedInputProfile $resolvedScenario.input_profile
        $segmentB = Invoke-ResumeRun -Name "segment_b" `
            -Frames $remainingFrames `
            -StartState $segmentA.record.end_state.path `
            -InputTimeline $secondTimelinePath -SaveData $resumedSaveData `
            -Graphics $resumedGraphics -Root $segmentBRoot `
            -ExpectedInputProfile $resolvedScenario.input_profile

        foreach($field in @(
            "framebuffer.sha256",
            "savedata_sha256",
            "savestate_semantic_fingerprint",
            "memory_content_fingerprint",
            "terminal_guest_frame",
            "terminal_guest_refresh_frame")) {
            $parts = $field.Split('.')
            if($parts.Count -eq 2) {
                $continuousParent =
                    $continuous.record.PSObject.Properties[$parts[0]].Value
                $resumedParent =
                    $segmentB.record.PSObject.Properties[$parts[0]].Value
                $continuousValue = $continuousParent[$parts[1]]
                $resumedValue = $resumedParent[$parts[1]]
            } else {
                $continuousValue =
                    $continuous.record.PSObject.Properties[$field].Value
                $resumedValue =
                    $segmentB.record.PSObject.Properties[$field].Value
            }
            Add-Difference $differences $field $continuousValue $resumedValue
        }
        foreach($counter in @(
            "whole_aot", "direct", "indirect", "external", "draws",
            "display_transfers")) {
            $resumedTotal = [uint64]$segmentA.record.calls[$counter] +
                [uint64]$segmentB.record.calls[$counter]
            Add-Difference $differences "calls.$counter" `
                ([uint64]$continuous.record.calls[$counter]) $resumedTotal
        }
        Add-Difference $differences "input.frame_origin" `
            $continuous.record.input.frame_origin `
            $segmentB.record.input.frame_origin
        foreach($counter in @(
            "sampled_frames", "active_frames", "button_frames",
            "circle_pad_frames", "touch_frames")) {
            $resumedTotal = [uint64]$segmentA.record.input[$counter] +
                [uint64]$segmentB.record.input[$counter]
            Add-Difference $differences "input.$counter" `
                ([uint64]$continuous.record.input[$counter]) $resumedTotal
        }
        Add-Difference $differences "audio.format.segment_a" `
            $continuous.record.audio_format_sha256 `
            $segmentA.record.audio_format_sha256
        Add-Difference $differences "audio.format.segment_b" `
            $continuous.record.audio_format_sha256 `
            $segmentB.record.audio_format_sha256
        $resumedPcmBytes = [uint64]$segmentA.record.pcm_bytes +
            [uint64]$segmentB.record.pcm_bytes
        Add-Difference $differences "audio.pcm_bytes" `
            ([uint64]$continuous.record.pcm_bytes) $resumedPcmBytes
        $resumedPcmHash = Get-ConcatenatedSha256 @(
            $segmentA.wave.payload, $segmentB.wave.payload)
        Add-Difference $differences "audio.pcm_sha256" `
            $continuous.record.pcm_sha256 $resumedPcmHash
        if($differences.Count -ne 0) {
            throw "Continuous/resume mismatch in $($differences.Count) fields"
        }
    } catch {
        $errorMessage = $_.Exception.Message
    }

    $passed = [string]::IsNullOrWhiteSpace($errorMessage)
    $record = [ordered]@{
        name = [string]$resolvedScenario.name
        category = [string]$resolvedScenario.category
        passed = $passed
        initial_state = $resolvedScenario.state
        input_profile = [string]$resolvedScenario.input_profile
        seed = [uint64]$resolvedScenario.seed
        total_frames = [int]$resolvedScenario.total_frames
        split_frame = [int]$resolvedScenario.split_frame
        continuous = if($null -eq $continuous) { $null } else {
            $continuous.record
        }
        segment_a = if($null -eq $segmentA) { $null } else {
            $segmentA.record
        }
        segment_b = if($null -eq $segmentB) { $null } else {
            $segmentB.record
        }
        differences = $differences
        error = $errorMessage
    }
    if($passed -and -not $KeepSuccessfulMedia.IsPresent) {
        foreach($run in @($continuous, $segmentA, $segmentB)) {
            foreach($artifact in @(
                $run.record.end_state,
                $run.record.framebuffer,
                $run.record.audio)) {
                Remove-SuccessArtifact $artifact $scenarioRoot
            }
        }
        foreach($saveRoot in @($continuousSaveData, $resumedSaveData)) {
            $safeSaveRoot = [System.IO.Path]::GetFullPath($saveRoot)
            $scenarioPrefix = [System.IO.Path]::GetFullPath(
                $scenarioRoot).TrimEnd('\', '/') +
                [System.IO.Path]::DirectorySeparatorChar
            if(-not $safeSaveRoot.StartsWith(
                    $scenarioPrefix,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove savedata outside scenario root"
            }
            Remove-Item -LiteralPath $safeSaveRoot -Recurse -Force
        }
    }
    Write-JsonArtifact $record (Join-Path $scenarioRoot `
        "resume-equivalence.json")
    $scenarioResults.Add($record) | Out-Null
    if(-not $passed) {
        $failures.Add([ordered]@{
            name = [string]$resolvedScenario.name
            error = $errorMessage
            differences = $differences
        }) | Out-Null
    }
}

$totalScenarioFrames = [uint64]0
foreach($scenarioResult in $scenarioResults) {
    $totalScenarioFrames += [uint64]$scenarioResult.total_frames
}
$report = [ordered]@{
    format = "oot3d_whole_aot_resume_matrix_v1"
    passed = $failures.Count -eq 0
    started_utc = $startedUtc.ToString("O")
    completed_utc = [DateTime]::UtcNow.ToString("O")
    catalog = Get-FileIdentity $resolvedCatalog
    executable = Get-FileIdentity $resolvedExecutable
    ui_profile = $UiProfile
    selected_scenarios = $resolvedScenarios.Count
    passed_scenarios = @($scenarioResults | Where-Object { $_.passed }).Count
    runtime_processes = [uint64]$scenarioResults.Count * 3U
    product_frames = $totalScenarioFrames * 2U
    scenarios = $scenarioResults
    failures = $failures
}
$reportPath = Join-Path $runRoot "resume-matrix.json"
Write-JsonArtifact $report $reportPath
New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
Write-JsonArtifact ([ordered]@{
    format = "oot3d_whole_aot_resume_matrix_latest_v1"
    report = $reportPath
    passed = $report.passed
    completed_utc = $report.completed_utc
}) (Join-Path $resolvedOutputRoot "latest.json")
[ordered]@{
    passed = $report.passed
    report = $reportPath
    scenarios = "$($report.passed_scenarios)/$($report.selected_scenarios)"
    runtime_processes = $report.runtime_processes
    product_frames = $report.product_frames
} | ConvertTo-Json
if(-not $report.passed) {
    throw "Whole-AOT resume matrix failed; see $reportPath"
}
