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
    [string]$GuestMap = "",
    [string]$OutputRoot =
        "I:\oot3dre_work\whole-aot-product-run\scenario-matrices",
    [string[]]$Scenario = @(),
    [ValidateRange(0, 1000)]
    [int]$ChunksOverride = 0,
    [ValidateRange(0, 1000000)]
    [int]$FramesPerChunkOverride = 0,
    [ValidateRange(10, 3600)]
    [double]$ChunkTimeoutSeconds = 120,
    [switch]$KeepSuccessfulMedia,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$soakScript = Join-Path $PSScriptRoot "Test-Oot3dWholeAotSoak.ps1"
$coverageTool = Join-Path $repoRoot `
    "tools\oot3d\native_a32_runtime\summarize_whole_aot_runtime_coverage.py"
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
        $soakScript,
        $coverageTool,
        $resolvedExecutable,
        $resolvedCatalog,
        $resolvedGraphics)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required whole-AOT scenario input is missing: $required"
    }
}
foreach($requiredDirectory in @($resolvedArtifactRoot, $resolvedSaveData)) {
    if(-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required whole-AOT scenario directory is missing: $requiredDirectory"
    }
}

function Get-FileIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $item.FullName `
            -Algorithm SHA256).Hash.ToLowerInvariant()
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
$allowedProfiles = @("exploration", "idle", "forward")
foreach($entry in @($catalog.scenarios)) {
    $name = [string]$entry.name
    if($name -notmatch '^[a-z0-9][a-z0-9_]*$') {
        throw "Invalid scenario name: $name"
    }
    if(-not $seenNames.Add($name)) {
        throw "Duplicate scenario name: $name"
    }
    if($requestedNames.Count -ne 0 -and -not $requestedNames.Contains($name)) {
        continue
    }
    $profile = [string]$entry.input_profile
    if($profile -notin $allowedProfiles) {
        throw "Scenario $name has unsupported input profile: $profile"
    }
    $chunks = if($ChunksOverride -ne 0) {
        $ChunksOverride
    } else {
        [int]$entry.chunks
    }
    $frames = if($FramesPerChunkOverride -ne 0) {
        $FramesPerChunkOverride
    } else {
        [int]$entry.frames_per_chunk
    }
    if($chunks -lt 1 -or $chunks -gt 1000) {
        throw "Scenario $name has invalid chunk count: $chunks"
    }
    if($frames -lt 120 -or $frames -gt 1000000) {
        throw "Scenario $name has invalid frames per chunk: $frames"
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
    $resolvedScenarios.Add([ordered]@{
        name = $name
        category = [string]$entry.category
        description = [string]$entry.description
        state = $identity
        input_profile = $profile
        chunks = $chunks
        frames_per_chunk = $frames
        seed = [uint32]$entry.seed
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
    throw "The whole-AOT scenario selection is empty"
}

if($ValidateOnly.IsPresent) {
    [ordered]@{
        format = "oot3d_whole_aot_scenario_catalog_validation_v1"
        valid = $true
        catalog = Get-FileIdentity $resolvedCatalog
        checkpoint_root = $checkpointRoot
        selected_scenarios = $resolvedScenarios.Count
        categories = @($resolvedScenarios.category | Sort-Object -Unique)
    } | ConvertTo-Json -Depth 5
    return
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
    if(-not [bool]$releaseReport.passed -or $null -eq $releaseReport.package) {
        throw "The latest whole-AOT release has no validated package"
    }
    $GuestMap = Join-Path ([string]$releaseReport.package.package) `
        "symbols\whole_aot_guest_map.json"
}
$resolvedGuestMap = [System.IO.Path]::GetFullPath($GuestMap)
if(-not (Test-Path -LiteralPath $resolvedGuestMap -PathType Leaf)) {
    throw "Whole-AOT guest map is missing: $resolvedGuestMap"
}

$startedUtc = [DateTime]::UtcNow
$runId = $startedUtc.ToString("yyyyMMddTHHmmssZ")
$runRoot = Join-Path $resolvedOutputRoot $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$hostPowerShell = (Get-Process -Id $PID).Path
$timeoutText = $ChunkTimeoutSeconds.ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)

function Invoke-ScenarioSoak {
    param(
        [Parameter(Mandatory = $true)]$ResolvedScenario,
        [Parameter(Mandatory = $true)][string]$ScenarioRoot
    )
    New-Item -ItemType Directory -Force -Path $ScenarioRoot | Out-Null
    $logPath = Join-Path $ScenarioRoot "matrix-launch.log"
    $arguments = @(
        "-NoLogo", "-NoProfile", "-File", $soakScript,
        "-ProductExecutable", $resolvedExecutable,
        "-InitialState", [string]$ResolvedScenario.state.path,
        "-GraphicsConfig", $resolvedGraphics,
        "-SaveDataDirectory", $resolvedSaveData,
        "-GuestMap", $resolvedGuestMap,
        "-OutputRoot", $ScenarioRoot,
        "-Chunks", [string]$ResolvedScenario.chunks,
        "-FramesPerChunk", [string]$ResolvedScenario.frames_per_chunk,
        "-ChunkTimeoutSeconds", $timeoutText,
        "-Seed", [string]$ResolvedScenario.seed,
        "-InputProfile", [string]$ResolvedScenario.input_profile)
    if($KeepSuccessfulMedia.IsPresent) {
        $arguments += "-KeepSuccessfulMedia"
    } else {
        $arguments += "-DiscardSuccessfulFinalState"
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $hostPowerShell
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach($argument in $arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    @($stdout, $stderr) | Set-Content -LiteralPath $logPath -Encoding utf8

    $latestPath = Join-Path $ScenarioRoot "latest.json"
    $reportPath = ""
    $report = $null
    if(Test-Path -LiteralPath $latestPath -PathType Leaf) {
        $latest = Get-Content -LiteralPath $latestPath -Raw | ConvertFrom-Json
        $reportPath = [string]$latest.report
        if(Test-Path -LiteralPath $reportPath -PathType Leaf) {
            $report = Get-Content -LiteralPath $reportPath -Raw |
                ConvertFrom-Json
        }
    }
    return [ordered]@{
        exit_code = $process.ExitCode
        log = $logPath
        report_path = $reportPath
        report = $report
    }
}

$scenarioResults = [System.Collections.Generic.List[object]]::new()
$runtimePaths = [System.Collections.Generic.List[string]]::new()
$failures = [System.Collections.Generic.List[object]]::new()
foreach($resolvedScenario in $resolvedScenarios) {
    $scenarioRoot = Join-Path $runRoot ([string]$resolvedScenario.name)
    $invocation = Invoke-ScenarioSoak `
        -ResolvedScenario $resolvedScenario -ScenarioRoot $scenarioRoot
    $scenarioPassed = $invocation.exit_code -eq 0 -and
        $null -ne $invocation.report -and [bool]$invocation.report.passed
    if($null -ne $invocation.report) {
        foreach($chunk in @($invocation.report.chunks)) {
            foreach($laneName in @("lane_a", "lane_b")) {
                $lane = $chunk.psobject.Properties[$laneName].Value
                if($null -ne $lane -and
                   -not [string]::IsNullOrWhiteSpace([string]$lane.runtime) -and
                   (Test-Path -LiteralPath ([string]$lane.runtime) -PathType Leaf)) {
                    $runtimePaths.Add([string]$lane.runtime) | Out-Null
                }
            }
        }
    }
    $record = [ordered]@{
        name = [string]$resolvedScenario.name
        category = [string]$resolvedScenario.category
        passed = $scenarioPassed
        state = $resolvedScenario.state
        input_profile = [string]$resolvedScenario.input_profile
        requested_chunks = [int]$resolvedScenario.chunks
        frames_per_chunk = [int]$resolvedScenario.frames_per_chunk
        exit_code = [int]$invocation.exit_code
        log = [string]$invocation.log
        report = [string]$invocation.report_path
        completed_chunks = if($null -eq $invocation.report) {
            0
        } else {
            [int]$invocation.report.completed_chunks
        }
        coverage = if($null -eq $invocation.report -or
            $null -eq $invocation.report.coverage) {
            $null
        } else {
            $invocation.report.coverage.summary
        }
        error = if($scenarioPassed) {
            ""
        } elseif($null -ne $invocation.report -and
                 $null -ne $invocation.report.first_failure) {
            [string]$invocation.report.first_failure.error
        } else {
            "Scenario runner did not produce a passing report"
        }
    }
    $scenarioResults.Add($record) | Out-Null
    if(-not $scenarioPassed) {
        $failures.Add([ordered]@{
            scenario = [string]$resolvedScenario.name
            exit_code = [int]$invocation.exit_code
            error = [string]$record.error
            log = [string]$record.log
            report = [string]$record.report
        }) | Out-Null
    }
}

$coverage = $null
$coveragePath = Join-Path $runRoot "coverage.json"
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
        $failures.Add([ordered]@{
            scenario = "aggregate_coverage"
            exit_code = $LASTEXITCODE
            error = "Runtime coverage summarization failed"
            log = ""
            report = $coveragePath
        }) | Out-Null
    } else {
        $coverage = Get-Content -LiteralPath $coveragePath -Raw |
            ConvertFrom-Json
    }
}

$report = [ordered]@{
    format = "oot3d_whole_aot_scenario_matrix_v1"
    passed = $failures.Count -eq 0
    started_utc = $startedUtc.ToString("O")
    completed_utc = [DateTime]::UtcNow.ToString("O")
    catalog = Get-FileIdentity $resolvedCatalog
    executable = Get-FileIdentity $resolvedExecutable
    guest_map = Get-FileIdentity $resolvedGuestMap
    selected_scenarios = $resolvedScenarios.Count
    passed_scenarios = @($scenarioResults | Where-Object { $_.passed }).Count
    deterministic_lanes = 2
    product_frames = [uint64](@($scenarioResults | ForEach-Object {
        [uint64]$_.completed_chunks * [uint64]$_.frames_per_chunk * 2U
    }) | Measure-Object -Sum).Sum
    coverage = $coverage
    scenarios = $scenarioResults
    failures = $failures
}
$reportPath = Join-Path $runRoot "scenario-matrix.json"
$report | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath $reportPath -Encoding utf8
New-Item -ItemType Directory -Force -Path $resolvedOutputRoot | Out-Null
[ordered]@{
    format = "oot3d_whole_aot_scenario_matrix_latest_v1"
    report = $reportPath
    passed = $report.passed
    completed_utc = $report.completed_utc
} | ConvertTo-Json | Set-Content -LiteralPath (
    Join-Path $resolvedOutputRoot "latest.json") -Encoding utf8
[ordered]@{
    passed = $report.passed
    report = $reportPath
    scenarios = $report.selected_scenarios
    passed_scenarios = $report.passed_scenarios
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
    throw "Whole-AOT scenario matrix failed; see $reportPath"
}
