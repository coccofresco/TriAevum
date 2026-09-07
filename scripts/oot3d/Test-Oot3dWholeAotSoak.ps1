[CmdletBinding()]
param(
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$InitialState =
        "I:\oot3dre_work\native_game\checkpoints\hudtest_after120.oot3dsav",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [string]$GuestMap = "",
    [string]$OutputRoot =
        "I:\oot3dre_work\whole-aot-product-run\soak-campaigns",
    [ValidateRange(1, 1000)]
    [int]$Chunks = 10,
    [ValidateRange(120, 1000000)]
    [int]$FramesPerChunk = 600,
    [ValidateRange(10, 3600)]
    [double]$ChunkTimeoutSeconds = 120,
    [uint32]$Seed = 0x4F4F5433,
    [ValidateSet("exploration", "idle", "forward")]
    [string]$InputProfile = "exploration",
    [switch]$DiscardSuccessfulFinalState,
    [switch]$KeepSuccessfulMedia
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
$coverageTool = Join-Path $repoRoot `
    "tools\oot3d\native_a32_runtime\summarize_whole_aot_runtime_coverage.py"
$resolvedExecutable = [System.IO.Path]::GetFullPath($ProductExecutable)
$resolvedInitialState = [System.IO.Path]::GetFullPath($InitialState)
$resolvedGraphics = [System.IO.Path]::GetFullPath($GraphicsConfig)
$resolvedSaveData = [System.IO.Path]::GetFullPath($SaveDataDirectory)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
foreach($required in @(
        $launcher,
        $coverageTool,
        $resolvedExecutable,
        $resolvedInitialState,
        $resolvedGraphics)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required whole-AOT soak input is missing: $required"
    }
}
if(-not (Test-Path -LiteralPath $resolvedSaveData -PathType Container)) {
    throw "Required whole-AOT soak savedata is missing: $resolvedSaveData"
}

if([string]::IsNullOrWhiteSpace($GuestMap)) {
    $latestReleasePath =
        "I:\oot3dre_work\whole-aot-product-run\release-gates\latest.json"
    if(-not (Test-Path -LiteralPath $latestReleasePath -PathType Leaf)) {
        throw "No release report is available to resolve the whole-AOT guest map"
    }
    $latestRelease = Get-Content -LiteralPath $latestReleasePath -Raw |
        ConvertFrom-Json
    $releaseReport = Get-Content -LiteralPath $latestRelease.report -Raw |
        ConvertFrom-Json
    if(-not [bool]$releaseReport.passed -or
       $null -eq $releaseReport.package) {
        throw "The latest whole-AOT release has no validated package"
    }
    $GuestMap = Join-Path ([string]$releaseReport.package.package) `
        "symbols\whole_aot_guest_map.json"
}
$resolvedGuestMap = [System.IO.Path]::GetFullPath($GuestMap)
if(-not (Test-Path -LiteralPath $resolvedGuestMap -PathType Leaf)) {
    throw "Whole-AOT guest map is missing: $resolvedGuestMap"
}
$symbolMetadataPath = Join-Path (Split-Path -Parent $resolvedGuestMap) `
    "symbols.json"
if(Test-Path -LiteralPath $symbolMetadataPath -PathType Leaf) {
    $symbolMetadata = Get-Content -LiteralPath $symbolMetadataPath -Raw |
        ConvertFrom-Json
    $executableHash = (Get-FileHash -LiteralPath $resolvedExecutable `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    if($executableHash -ne [string]$symbolMetadata.executable.sha256) {
        throw "Whole-AOT guest map belongs to a different executable"
    }
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
$timeoutText = $ChunkTimeoutSeconds.ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)

function Assert-RunPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if(-not $resolved.StartsWith(
            $runPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Soak artifact escapes run root: $resolved"
    }
    return $resolved
}

function Get-FileIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $item.FullName `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        retained = $true
    }
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
        throw "Derived soak graphics config is not deterministic"
    }
}

function New-CampaignTimeline {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$Frames,
        [Parameter(Mandatory = $true)][uint32]$ChunkIndex
    )
    if($InputProfile -eq "idle") {
        [ordered]@{
            schema = "oot3d.native_game.input_timeline.v1"
            frame_origin = "run"
            segments = @([ordered]@{
                start_frame = 0
                end_frame_exclusive = $Frames
                circle_x = 0.0
                circle_y = 0.0
            })
        } | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $Path -Encoding utf8
        return
    }
    if($InputProfile -eq "forward") {
        [ordered]@{
            schema = "oot3d.native_game.input_timeline.v1"
            frame_origin = "run"
            segments = @([ordered]@{
                start_frame = 0
                end_frame_exclusive = $Frames
                circle_x = 0.0
                circle_y = 0.85
            })
        } | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $Path -Encoding utf8
        return
    }

    $directions = @(
        @(0.0, 0.85), @(0.65, 0.70), @(0.85, 0.0),
        @(0.65, -0.70), @(0.0, -0.80), @(-0.65, -0.70),
        @(-0.85, 0.0), @(-0.65, 0.70), @(0.0, 0.0))
    $buttons = @("a", "b", "r", "x", "y")
    $segments = [System.Collections.Generic.List[object]]::new()
    $cursor = 0
    $phase = 0
    $randomState = ([uint64]$Seed -bxor
        ([uint64]$ChunkIndex * 0x9E3779B9L)) -band 0xFFFFFFFFL
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
    [ordered]@{
        schema = "oot3d.native_game.input_timeline.v1"
        frame_origin = "run"
        segments = $segments
    } | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $Path -Encoding utf8
}

function Invoke-IsolatedLauncher {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & $hostPowerShell -NoLogo -NoProfile -File $launcher @Arguments
    if($LASTEXITCODE -ne 0) {
        throw "Native game launcher exited with code $LASTEXITCODE"
    }
}

function Invoke-ChunkLane {
    param(
        [Parameter(Mandatory = $true)][string]$Lane,
        [Parameter(Mandatory = $true)][string]$ChunkRoot,
        [Parameter(Mandatory = $true)][string]$StartState,
        [Parameter(Mandatory = $true)][string]$InputTimeline,
        [Parameter(Mandatory = $true)][string]$LaneSaveData,
        [Parameter(Mandatory = $true)][string]$LaneGraphics
    )
    $laneRoot = Assert-RunPath (Join-Path $ChunkRoot $Lane)
    New-Item -ItemType Directory -Force -Path $laneRoot | Out-Null
    $runtimePath = Join-Path $laneRoot "runtime.json"
    $statePath = Join-Path $laneRoot "end.oot3dsav"
    $framePath = Join-Path $laneRoot "frame.bmp"
    $audioPath = Join-Path $laneRoot "audio.wav"
    $logPath = Join-Path $laneRoot "process.log"
    $arguments = @(
        "-SkipBuild",
        "-Executable", $resolvedExecutable,
        "-Renderer", "nri",
        "-Frames", [string]$FramesPerChunk,
        "-MaxSeconds", $timeoutText,
        "-FixedDeltaSeconds", $fixedDelta,
        "-SimulationRate", "30",
        "-PresentationRate", "free",
        "-Width", "1280", "-Height", "720",
        "-GraphicsConfig", $LaneGraphics,
        "-SaveDataDirectory", $LaneSaveData,
        "-LoadState", $StartState,
        "-InputTimeline", $InputTimeline,
        "-ProfileA32Blocks",
        "-SaveState", $statePath,
        "-SaveStateFrame", [string]($FramesPerChunk - 1),
        "-Screenshot", $framePath,
        "-ScreenshotStartFrame", [string]($FramesPerChunk - 1),
        "-AudioPcmDump", $audioPath,
        "-Output", $runtimePath)
    Invoke-IsolatedLauncher -Arguments $arguments *>&1 |
        Tee-Object -FilePath $logPath | Out-Host

    foreach($artifact in @($runtimePath, $statePath, $framePath, $audioPath)) {
        if(-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Soak lane $Lane did not produce: $artifact"
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
    $crashDumps = @(Get-ChildItem (Join-Path $LaneSaveData "crashes") `
        -Filter *.dmp -ErrorAction SilentlyContinue)
    $input = $runtime.hid_input
    $inputActionFrames = [uint64]$input.button_host_frames +
        [uint64]$input.circle_pad_host_frames +
        [uint64]$input.touch_host_frames
    if([uint64]$runtime.run_frames -ne [uint64]$FramesPerChunk -or
       -not [bool]$runtime.compiled_functions.whole_aot_available -or
       -not [bool]$runtime.compiled_functions.whole_aot_enabled -or
       [string]$input.timeline_frame_origin -ne "run" -or
       [uint64]$input.sampled_host_frames -eq 0U -or
       [uint64]$input.active_segment_host_frames -ne
           [uint64]$input.sampled_host_frames -or
       ($InputProfile -ne "idle" -and $inputActionFrames -eq 0U) -or
       $strictFailures.Count -ne 0 -or $crashDumps.Count -ne 0) {
        throw "Soak lane $Lane violated the strict whole-AOT contract"
    }
    return [ordered]@{
        lane = $Lane
        runtime = $runtimePath
        end_state = Get-FileIdentity $statePath
        framebuffer = Get-FileIdentity $framePath
        audio = Get-FileIdentity $audioPath
        savedata_sha256 = Get-SaveDataFingerprint $LaneSaveData
        memory_content_fingerprint =
            [string]$runtime.memory_content_fingerprint
        memory_state_fingerprint = [string]$runtime.memory_state_fingerprint
        process_state_fingerprint = [string]$runtime.process_state_fingerprint
        calls = [ordered]@{
            whole_aot = [uint64]$runtime.compiled_functions.whole_aot_calls
            direct = [uint64]$runtime.compiled_functions.whole_aot_direct_calls
            indirect =
                [uint64]$runtime.compiled_functions.whole_aot_indirect_calls
            external =
                [uint64]$runtime.compiled_functions.whole_aot_external_calls
        }
        block_entries = [uint64]$runtime.a32_block_profile.block_entries
        profile_samples = [uint64]$runtime.a32_block_profile.total_samples
        strict_counters = $strictCounters
        crash_dumps = $crashDumps.Count
        input = [ordered]@{
            frame_origin = [string]$input.timeline_frame_origin
            sampled_frames = [uint64]$input.sampled_host_frames
            active_frames = [uint64]$input.active_segment_host_frames
            button_frames = [uint64]$input.button_host_frames
            circle_pad_frames = [uint64]$input.circle_pad_host_frames
            touch_frames = [uint64]$input.touch_host_frames
        }
    }
}

function Compare-Lanes {
    param($Left, $Right)
    $differences = [System.Collections.Generic.List[string]]::new()
    foreach($field in @(
        "memory_content_fingerprint",
        "memory_state_fingerprint",
        "process_state_fingerprint",
        "savedata_sha256")) {
        if([string]$Left[$field] -ne [string]$Right[$field]) {
            $differences.Add($field) | Out-Null
        }
    }
    foreach($artifact in @("end_state", "framebuffer", "audio")) {
        if([string]$Left[$artifact].sha256 -ne
           [string]$Right[$artifact].sha256) {
            $differences.Add("$artifact.sha256") | Out-Null
        }
    }
    foreach($call in @("whole_aot", "direct", "indirect", "external")) {
        if([uint64]$Left.calls[$call] -ne [uint64]$Right.calls[$call]) {
            $differences.Add("calls.$call") | Out-Null
        }
    }
    return @($differences)
}

$laneRoots = [ordered]@{
    a = Join-Path $runRoot "lane_a"
    b = Join-Path $runRoot "lane_b"
}
foreach($lane in $laneRoots.Keys) {
    New-Item -ItemType Directory -Force -Path $laneRoots[$lane] | Out-Null
    Copy-SaveDataBaseline (Join-Path $laneRoots[$lane] "savedata")
    Write-DeterministicGraphicsConfig (
        Join-Path $laneRoots[$lane] "graphics.json")
}
if((Get-SaveDataFingerprint (Join-Path $laneRoots.a "savedata")) -ne
   (Get-SaveDataFingerprint (Join-Path $laneRoots.b "savedata"))) {
    throw "Initial soak savedata sandboxes differ"
}

$finalState = Join-Path $runRoot "final.oot3dsav"
$currentState = $resolvedInitialState
$chunkResults = [System.Collections.Generic.List[object]]::new()
$runtimePaths = [System.Collections.Generic.List[string]]::new()
$firstFailure = $null
for($chunkIndex = 0; $chunkIndex -lt $Chunks; ++$chunkIndex) {
    $chunkRoot = Assert-RunPath (Join-Path $runRoot (
        "chunk_{0:D4}" -f $chunkIndex))
    New-Item -ItemType Directory -Force -Path $chunkRoot | Out-Null
    $startState = Join-Path $chunkRoot "start.oot3dsav"
    Copy-Item -LiteralPath $currentState -Destination $startState -Force
    $inputPath = Join-Path $chunkRoot "input.json"
    New-CampaignTimeline -Path $inputPath -Frames $FramesPerChunk `
        -ChunkIndex ([uint32]$chunkIndex)
    $laneA = $null
    $laneB = $null
    $differences = @()
    $errorMessage = ""
    try {
        $laneA = Invoke-ChunkLane -Lane "a" -ChunkRoot $chunkRoot `
            -StartState $startState -InputTimeline $inputPath `
            -LaneSaveData (Join-Path $laneRoots.a "savedata") `
            -LaneGraphics (Join-Path $laneRoots.a "graphics.json")
        $laneB = Invoke-ChunkLane -Lane "b" -ChunkRoot $chunkRoot `
            -StartState $startState -InputTimeline $inputPath `
            -LaneSaveData (Join-Path $laneRoots.b "savedata") `
            -LaneGraphics (Join-Path $laneRoots.b "graphics.json")
        $runtimePaths.Add([string]$laneA.runtime) | Out-Null
        $runtimePaths.Add([string]$laneB.runtime) | Out-Null
        $differences = @(Compare-Lanes $laneA $laneB)
        if($differences.Count -ne 0) {
            throw "Deterministic lane mismatch: $($differences -join ', ')"
        }
    } catch {
        $errorMessage = $_.Exception.Message
    }
    $passed = [string]::IsNullOrWhiteSpace($errorMessage)
    $chunkRecord = [ordered]@{
        index = $chunkIndex
        passed = $passed
        start_state = Get-FileIdentity $startState
        input = Get-FileIdentity $inputPath
        lane_a = $laneA
        lane_b = $laneB
        differences = $differences
        error = $errorMessage
    }
    $chunkResults.Add($chunkRecord) | Out-Null
    if(-not $passed) {
        $firstFailure = $chunkRecord
        break
    }

    Copy-Item -LiteralPath $laneA.end_state.path -Destination $finalState -Force
    $currentState = $finalState
    if(-not $KeepSuccessfulMedia.IsPresent) {
        foreach($path in @(
            $startState,
            $laneA.end_state.path,
            $laneB.end_state.path,
            $laneA.framebuffer.path,
            $laneB.framebuffer.path,
            $laneA.audio.path,
            $laneB.audio.path)) {
            $safePath = Assert-RunPath $path
            if(Test-Path -LiteralPath $safePath -PathType Leaf) {
                [System.IO.File]::Delete($safePath)
            }
        }
        $chunkRecord.start_state.retained = $false
        foreach($laneRecord in @($laneA, $laneB)) {
            foreach($artifact in @("end_state", "framebuffer", "audio")) {
                $laneRecord[$artifact].retained = $false
            }
        }
    }
}

$coveragePath = Join-Path $runRoot "coverage.json"
$coverage = $null
if($runtimePaths.Count -ne 0) {
    $coverageArguments = @(
        $coverageTool,
        "--guest-map", $resolvedGuestMap,
        "--output", $coveragePath)
    foreach($runtimePath in $runtimePaths) {
        $coverageArguments += @("--runtime", $runtimePath)
    }
    & python @coverageArguments
    if($LASTEXITCODE -ne 0) {
        if($null -eq $firstFailure) {
            $firstFailure = [ordered]@{
                index = -1
                error = "Runtime coverage summarization failed"
            }
        }
    } else {
        $coverage = Get-Content -LiteralPath $coveragePath -Raw |
            ConvertFrom-Json
    }
}

$finalStateIdentity = if(Test-Path -LiteralPath $finalState -PathType Leaf) {
    Get-FileIdentity $finalState
} else {
    $null
}
if($DiscardSuccessfulFinalState.IsPresent -and
   $null -eq $firstFailure -and $null -ne $finalStateIdentity) {
    $safeFinalState = Assert-RunPath $finalState
    [System.IO.File]::Delete($safeFinalState)
    $finalStateIdentity.retained = $false
}

$report = [ordered]@{
    format = "oot3d_whole_aot_soak_campaign_v1"
    passed = $null -eq $firstFailure
    started_utc = $startedUtc.ToString("O")
    completed_utc = [DateTime]::UtcNow.ToString("O")
    seed = [uint64]$Seed
    input_profile = $InputProfile
    requested_chunks = $Chunks
    completed_chunks = @($chunkResults | Where-Object { $_.passed }).Count
    frames_per_chunk = $FramesPerChunk
    deterministic_lanes = 2
    product_frames =
        [uint64]$FramesPerChunk * [uint64]$chunkResults.Count * 2U
    executable = Get-FileIdentity $resolvedExecutable
    initial_state = Get-FileIdentity $resolvedInitialState
    guest_map = Get-FileIdentity $resolvedGuestMap
    final_state = $finalStateIdentity
    coverage = $coverage
    chunks = $chunkResults
    first_failure = $firstFailure
}
$reportPath = Join-Path $runRoot "soak-campaign.json"
$report | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath $reportPath -Encoding utf8
New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
[ordered]@{
    format = "oot3d_whole_aot_soak_latest_v1"
    report = $reportPath
    passed = $report.passed
    completed_utc = $report.completed_utc
} | ConvertTo-Json | Set-Content -LiteralPath (
    Join-Path $resolvedOutputRoot "latest.json") -Encoding utf8
[ordered]@{
    passed = $report.passed
    report = $reportPath
    completed_chunks = $report.completed_chunks
    product_frames = $report.product_frames
    observed_functions = if($null -eq $coverage) {
        0
    } else {
        [uint64]$coverage.summary.observed_function_count
    }
    observed_function_percent = if($null -eq $coverage) {
        0.0
    } else {
        [double]$coverage.summary.observed_function_percent
    }
} | ConvertTo-Json
if(-not $report.passed) {
    throw "Whole-AOT soak campaign failed; see $reportPath"
}
