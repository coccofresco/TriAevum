[CmdletBinding()]
param(
    [string]$BuildDirectory =
        "I:\oot3dre_work\whole-aot-product-consumer",
    [string]$ProductExecutable = "",
    [string]$CacheRoot =
        "I:\oot3dre_work\oot3d-whole-aot-product-cache",
    [string]$OutputRoot =
        "I:\oot3dre_work\whole-aot-product-run\release-gates",
    [string]$PackageOutputRoot =
        "I:\oot3dre_work\whole-aot-product-packages",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [string]$GameplayState =
        "I:\oot3dre_work\native_game\checkpoints\hudtest_after120.oot3dsav",
    [string]$GameplayInputTimeline =
        "I:\oot3dre_work\native_game\captures\grass_camera_perf_20260726\move_diagonal_absolute.json",
    [ValidateRange(1, 20)]
    [int]$GoldenRepetitions = 2,
    [ValidateRange(1, 20)]
    [int]$PerformanceRuns = 3,
    [ValidateRange(61, 1000000)]
    [int]$PerformanceFrames = 360,
    [ValidateRange(1, 999999)]
    [int]$PerformanceWarmupFrames = 60,
    [ValidateRange(1, 10000)]
    [double]$MinimumMedianFramesPerSecond = 60,
    [ValidateRange(1, 10000)]
    [double]$MinimumRunFramesPerSecond = 60,
    [ValidateRange(1, 8)]
    [int]$ArchiveParallel = 4,
    [ValidateRange(1, 16)]
    [int]$ConsumerParallel = 2,
    [ValidateRange(0, 1000)]
    [int]$SoakChunks = 0,
    [ValidateRange(120, 1000000)]
    [int]$SoakFramesPerChunk = 600,
    [ValidateRange(10, 3600)]
    [double]$ResumeRunTimeoutSeconds = 240,
    [switch]$RunScenarioMatrix,
    [switch]$RebuildArchive,
    [switch]$ReconfigureConsumer,
    [switch]$SkipBuild,
    [switch]$SkipPackage,
    [switch]$RequireCleanWorktree
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if($PerformanceWarmupFrames -ge $PerformanceFrames) {
    throw "PerformanceWarmupFrames must be lower than PerformanceFrames"
}
if($RunScenarioMatrix.IsPresent -and $SkipPackage.IsPresent) {
    throw "RunScenarioMatrix requires a freshly validated package guest map"
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$resolvedBuild = [System.IO.Path]::GetFullPath($BuildDirectory)
if([string]::IsNullOrWhiteSpace($ProductExecutable)) {
    $ProductExecutable = Join-Path $resolvedBuild "oot3d_native_game.exe"
}
$resolvedExecutable = [System.IO.Path]::GetFullPath($ProductExecutable)
$resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$startedUtc = [DateTime]::UtcNow
$runId = $startedUtc.ToString("yyyyMMddTHHmmssZ")
$runRoot = Join-Path $resolvedOutputRoot $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

$scripts = [ordered]@{
    build_archive = Join-Path $PSScriptRoot "Build-Oot3dWholeAotProduct.ps1"
    configure = Join-Path $PSScriptRoot "Configure-Oot3dWholeAotProduct.ps1"
    build = Join-Path $PSScriptRoot "Build-Oot3dLlvm.ps1"
    golden = Join-Path $PSScriptRoot "Test-Oot3dWholeAotGolden.ps1"
    performance = Join-Path $PSScriptRoot "Measure-Oot3dRuntimePerformance.ps1"
    package = Join-Path $PSScriptRoot "Package-Oot3dWholeAotProduct.ps1"
    package_test = Join-Path $PSScriptRoot "Test-Oot3dWholeAotPackage.ps1"
    soak = Join-Path $PSScriptRoot "Test-Oot3dWholeAotSoak.ps1"
    resume_matrix = Join-Path $PSScriptRoot `
        "Test-Oot3dWholeAotResumeMatrix.ps1"
    scenario_matrix = Join-Path $PSScriptRoot `
        "Test-Oot3dWholeAotScenarioMatrix.ps1"
}
foreach($script in $scripts.Values) {
    if(-not (Test-Path -LiteralPath $script -PathType Leaf)) {
        throw "Required release script is missing: $script"
    }
}
$hostPowerShell = (Get-Process -Id $PID).Path

function Invoke-IsolatedScript {
    param(
        [Parameter(Mandatory = $true)][string]$Script,
        [string[]]$ScriptArguments = @()
    )
    & $hostPowerShell -NoLogo -NoProfile -File $Script @ScriptArguments
    if($LASTEXITCODE -ne 0) {
        throw "$(Split-Path -Leaf $Script) exited with code $LASTEXITCODE"
    }
}

$gitCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $gitCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Unable to resolve the release source commit"
}
$gitStatus = @(& git -C $repoRoot status --porcelain=v1 `
    --untracked-files=normal)
$worktreeClean = $gitStatus.Count -eq 0
if($RequireCleanWorktree.IsPresent -and -not $worktreeClean) {
    throw "The whole-AOT release gate requires a clean worktree"
}

$steps = [System.Collections.Generic.List[object]]::new()
$releaseFailure = $null
function Invoke-ReleaseStep {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    $logPath = Join-Path $runRoot "$Name.log"
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        & $Action *>&1 | Tee-Object -FilePath $logPath
        $timer.Stop()
        $steps.Add([ordered]@{
            name = $Name
            passed = $true
            seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
            log = $logPath
            error = ""
        }) | Out-Null
    } catch {
        $timer.Stop()
        $_ | Out-String | Add-Content -LiteralPath $logPath
        $steps.Add([ordered]@{
            name = $Name
            passed = $false
            seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
            log = $logPath
            error = $_.Exception.Message
        }) | Out-Null
        throw
    }
}

$goldenRoot = Join-Path $runRoot "golden"
$performanceRoot = Join-Path $runRoot "performance"
$minimumMedianText = $MinimumMedianFramesPerSecond.ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)
$minimumRunText = $MinimumRunFramesPerSecond.ToString(
    "R", [Globalization.CultureInfo]::InvariantCulture)
$packageName = ""
$packagePath = ""
try {
    if(-not $SkipBuild.IsPresent) {
        if($RebuildArchive.IsPresent) {
            Invoke-ReleaseStep "01_archive" {
                Invoke-IsolatedScript -Script $scripts.build_archive `
                    -ScriptArguments @(
                        "-CacheRoot", $CacheRoot,
                        "-Parallel", [string]$ArchiveParallel)
            }
        }
        $consumerCache = Join-Path $resolvedBuild "CMakeCache.txt"
        $configureMode = if($ReconfigureConsumer.IsPresent -or
            -not (Test-Path -LiteralPath $consumerCache -PathType Leaf)) {
            "configure"
        } else {
            "validate_configuration"
        }
        Invoke-ReleaseStep "02_$configureMode" {
            $configureArguments = @(
                "-CacheRoot", $CacheRoot,
                "-BuildDirectory", $resolvedBuild,
                "-Configuration", "Release")
            if($configureMode -eq "validate_configuration") {
                $configureArguments += "-ValidateOnly"
            }
            Invoke-IsolatedScript -Script $scripts.configure `
                -ScriptArguments $configureArguments
        }
        foreach($target in @(
            "oot3d_native_game",
            "oot3d_native_a32_runtime_tests",
            "oot3d_native_a32_input_tests",
            "oot3d_native_crash_diagnostics_tests",
            "oot3d_native_pica_submission_tests",
            "oot3d_native_a32_savestate_tests",
            "oot3d_native_pica_visual_savestate_tests",
            "oot3d_native_pica_frontend_tests",
            "oot3d_top_screen_mod_profile_tests",
            "oot3d_native_game_runtime_tests")) {
            Invoke-ReleaseStep "03_build_$target" {
                Invoke-IsolatedScript -Script $scripts.build `
                    -ScriptArguments @(
                        "-BuildDirectory", $resolvedBuild,
                        "-Target", $target,
                        "-Parallel", [string]$ConsumerParallel)
            }
        }
    }

    foreach($test in @(
        "oot3d_native_a32_runtime_tests.exe",
        "oot3d_native_a32_input_tests.exe",
        "oot3d_native_crash_diagnostics_tests.exe",
        "oot3d_native_pica_submission_tests.exe",
        "oot3d_native_a32_savestate_tests.exe",
        "oot3d_native_pica_visual_savestate_tests.exe",
        "oot3d_native_pica_frontend_tests.exe",
        "oot3d_top_screen_mod_profile_tests.exe",
        "oot3d_native_game_runtime_tests.exe")) {
        $testPath = Join-Path $resolvedBuild $test
        if(-not (Test-Path -LiteralPath $testPath -PathType Leaf)) {
            throw "Required release test executable is missing: $testPath"
        }
        Invoke-ReleaseStep "04_test_$($test.Replace('.exe', ''))" {
            & $testPath
            if($LASTEXITCODE -ne 0) {
                throw "$test exited with code $LASTEXITCODE"
            }
        }
    }
    if(-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf)) {
        throw "Whole-AOT product executable is missing: $resolvedExecutable"
    }

    Invoke-ReleaseStep "04_validate_resume_catalog" {
        Invoke-IsolatedScript -Script $scripts.resume_matrix `
            -ScriptArguments @(
                "-ProductExecutable", $resolvedExecutable,
                "-GraphicsConfig", $GraphicsConfig,
                "-SaveDataDirectory", $SaveDataDirectory,
                "-ValidateOnly")
    }

    Invoke-ReleaseStep "05_golden" {
        Invoke-IsolatedScript -Script $scripts.golden -ScriptArguments @(
            "-ProductExecutable", $resolvedExecutable,
            "-OutputDirectory", $goldenRoot,
            "-GraphicsConfig", $GraphicsConfig,
            "-SaveDataDirectory", $SaveDataDirectory,
            "-KokiriGameplayState", $GameplayState,
            "-GameplayInputTimeline", $GameplayInputTimeline,
            "-Repetitions", [string]$GoldenRepetitions)
    }

    Invoke-ReleaseStep "06_resume_matrix" {
        $resumeTimeoutText = $ResumeRunTimeoutSeconds.ToString(
            "R", [Globalization.CultureInfo]::InvariantCulture)
        Invoke-IsolatedScript -Script $scripts.resume_matrix `
            -ScriptArguments @(
                "-ProductExecutable", $resolvedExecutable,
                "-GraphicsConfig", $GraphicsConfig,
                "-SaveDataDirectory", $SaveDataDirectory,
                "-OutputRoot", (Join-Path $runRoot "resume_matrix"),
                "-RunTimeoutSeconds", $resumeTimeoutText)
    }

    Invoke-ReleaseStep "07_performance" {
        Invoke-IsolatedScript -Script $scripts.performance -ScriptArguments @(
            "-Executable", $resolvedExecutable,
            "-SourceGraphicsConfig", $GraphicsConfig,
            "-OutputDirectory", $performanceRoot,
            "-LoadState", $GameplayState,
            "-InputTimeline", $GameplayInputTimeline,
            "-SaveDataDirectory", $SaveDataDirectory,
            "-Renderer", "nri",
            "-RunCount", [string]$PerformanceRuns,
            "-Frames", [string]$PerformanceFrames,
            "-WarmupFrames", [string]$PerformanceWarmupFrames,
            "-Width", "1280", "-Height", "720",
            "-RequireWholeAotProduct",
            "-MinimumMedianFramesPerSecond",
                $minimumMedianText,
            "-MinimumRunFramesPerSecond",
                $minimumRunText)
    }

    if(-not $SkipPackage.IsPresent) {
        $executableHash = (Get-FileHash -LiteralPath $resolvedExecutable `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        $packageName = "oot3d-whole-aot-release-$($executableHash.Substring(0, 12))"
        $packagePath = Join-Path (
            [System.IO.Path]::GetFullPath($PackageOutputRoot)) $packageName
        Invoke-ReleaseStep "08_package" {
            Invoke-IsolatedScript -Script $scripts.package `
                -ScriptArguments @(
                    "-ProductExecutable", $resolvedExecutable,
                    "-ProductBuildDirectory", $resolvedBuild,
                    "-OutputRoot", $PackageOutputRoot,
                    "-CacheRoot", $CacheRoot,
                    "-PackageName", $packageName,
                    "-Force")
        }
        Invoke-ReleaseStep "09_package_test" {
            Invoke-IsolatedScript -Script $scripts.package_test `
                -ScriptArguments @("-PackageDirectory", $packagePath)
        }
    }
    if($RunScenarioMatrix.IsPresent) {
        Invoke-ReleaseStep "10_scenario_matrix" {
            Invoke-IsolatedScript -Script $scripts.scenario_matrix `
                -ScriptArguments @(
                    "-ProductExecutable", $resolvedExecutable,
                    "-GraphicsConfig", $GraphicsConfig,
                    "-SaveDataDirectory", $SaveDataDirectory,
                    "-GuestMap", (Join-Path $packagePath `
                        "symbols\whole_aot_guest_map.json"),
                    "-OutputRoot", (Join-Path $runRoot "scenario_matrix"))
        }
    }
    if($SoakChunks -gt 0) {
        $soakArguments = @(
            "-ProductExecutable", $resolvedExecutable,
            "-InitialState", $GameplayState,
            "-GraphicsConfig", $GraphicsConfig,
            "-SaveDataDirectory", $SaveDataDirectory,
            "-OutputRoot", (Join-Path $runRoot "soak"),
            "-Chunks", [string]$SoakChunks,
            "-FramesPerChunk", [string]$SoakFramesPerChunk)
        if(-not [string]::IsNullOrWhiteSpace($packagePath)) {
            $soakArguments += @(
                "-GuestMap",
                (Join-Path $packagePath "symbols\whole_aot_guest_map.json"))
        }
        Invoke-ReleaseStep "11_soak" {
            Invoke-IsolatedScript -Script $scripts.soak `
                -ScriptArguments $soakArguments
        }
    }
} catch {
    $releaseFailure = $_
}

$goldenSummaryPath = Join-Path $goldenRoot "golden-gate.json"
$performanceSummaryPath = Join-Path $performanceRoot `
    "steady_state_summary.json"
$goldenSummary = if(Test-Path -LiteralPath $goldenSummaryPath) {
    Get-Content -LiteralPath $goldenSummaryPath -Raw | ConvertFrom-Json
} else { $null }
$performanceSummary = if(Test-Path -LiteralPath $performanceSummaryPath) {
    Get-Content -LiteralPath $performanceSummaryPath -Raw | ConvertFrom-Json
} else { $null }
$packageSummaryPath = if([string]::IsNullOrWhiteSpace($packagePath)) {
    ""
} else {
    Join-Path $packagePath "logs\package-validation\package-validation.json"
}
$packageSummary = if(-not [string]::IsNullOrWhiteSpace($packageSummaryPath) -and
    (Test-Path -LiteralPath $packageSummaryPath)) {
    Get-Content -LiteralPath $packageSummaryPath -Raw | ConvertFrom-Json
} else { $null }
$soakLatestPath = Join-Path $runRoot "soak\latest.json"
$soakSummary = if(Test-Path -LiteralPath $soakLatestPath -PathType Leaf) {
    $soakLatest = Get-Content -LiteralPath $soakLatestPath -Raw |
        ConvertFrom-Json
    Get-Content -LiteralPath $soakLatest.report -Raw | ConvertFrom-Json
} else { $null }
$scenarioMatrixLatestPath = Join-Path $runRoot `
    "scenario_matrix\latest.json"
$scenarioMatrixSummary = if(Test-Path -LiteralPath `
        $scenarioMatrixLatestPath -PathType Leaf) {
    $scenarioMatrixLatest = Get-Content -LiteralPath `
        $scenarioMatrixLatestPath -Raw | ConvertFrom-Json
    Get-Content -LiteralPath $scenarioMatrixLatest.report -Raw |
        ConvertFrom-Json
} else { $null }
$resumeMatrixLatestPath = Join-Path $runRoot "resume_matrix\latest.json"
$resumeMatrixSummary = if(Test-Path -LiteralPath `
        $resumeMatrixLatestPath -PathType Leaf) {
    $resumeMatrixLatest = Get-Content -LiteralPath `
        $resumeMatrixLatestPath -Raw | ConvertFrom-Json
    Get-Content -LiteralPath $resumeMatrixLatest.report -Raw |
        ConvertFrom-Json
} else { $null }

$productManifestPath = Join-Path $repoRoot `
    "tools\oot3d\native_a32_runtime\whole_aot_product_manifest.json"
$productManifest = Get-Content -LiteralPath $productManifestPath -Raw |
    ConvertFrom-Json
$consumerCache = Join-Path $resolvedBuild "CMakeCache.txt"
$archiveMetadata = $null
if(Test-Path -LiteralPath $consumerCache -PathType Leaf) {
    $archiveSetting = Select-String -LiteralPath $consumerCache `
        -Pattern '^OOT3D_WHOLE_AOT_PREBUILT_LIBRARY:FILEPATH=(.+)$' |
        Select-Object -First 1
    if($null -ne $archiveSetting) {
        $archivePath = [System.IO.Path]::GetFullPath(
            $archiveSetting.Matches[0].Groups[1].Value)
        $artifactRoot = Split-Path -Parent (
            Split-Path -Parent (
                Split-Path -Parent (
                    Split-Path -Parent $archivePath)))
        $archiveMetadataPath = Join-Path $artifactRoot `
            "whole_aot_archive.json"
        if(Test-Path -LiteralPath $archiveMetadataPath -PathType Leaf) {
            $archiveMetadata = Get-Content -LiteralPath $archiveMetadataPath `
                -Raw | ConvertFrom-Json
        }
    }
}
$executableIdentity = if(Test-Path -LiteralPath $resolvedExecutable) {
    [ordered]@{
        path = $resolvedExecutable
        bytes = (Get-Item -LiteralPath $resolvedExecutable).Length
        sha256 = (Get-FileHash -LiteralPath $resolvedExecutable `
            -Algorithm SHA256).Hash.ToLowerInvariant()
    }
} else { $null }
$report = [ordered]@{
    format = "oot3d_whole_aot_release_gate_v1"
    passed = $null -eq $releaseFailure
    started_utc = $startedUtc.ToString("O")
    completed_utc = [DateTime]::UtcNow.ToString("O")
    source = [ordered]@{
        repo = $repoRoot
        commit = $gitCommit
        clean = $worktreeClean
        status = $gitStatus
    }
    product = [ordered]@{
        executable = $executableIdentity
        archive = $archiveMetadata
        manifest_sha256 = (Get-FileHash -LiteralPath $productManifestPath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        functions = [uint64]$productManifest.closure.counts.functions
        native_functions =
            [uint64]$productManifest.closure.counts.selected_functions
        host_boundaries =
            [uint64]$productManifest.closure.counts.host_boundaries
        residual_a32_entries =
            [uint64]$productManifest.closure.residual_a32_entries
    }
    golden = $goldenSummary
    performance = $performanceSummary
    package = $packageSummary
    resume_matrix = $resumeMatrixSummary
    scenario_matrix = $scenarioMatrixSummary
    soak = $soakSummary
    steps = $steps
    failure = if($null -eq $releaseFailure) {
        ""
    } else {
        $releaseFailure.Exception.Message
    }
}
$reportPath = Join-Path $runRoot "release-gate.json"
$report | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath $reportPath -Encoding utf8
$latestPath = Join-Path $resolvedOutputRoot "latest.json"
[ordered]@{
    format = "oot3d_whole_aot_release_gate_latest_v1"
    report = $reportPath
    passed = $report.passed
    completed_utc = $report.completed_utc
} | ConvertTo-Json | Set-Content -LiteralPath $latestPath -Encoding utf8
$report | ConvertTo-Json -Depth 20
if($null -ne $releaseFailure) {
    throw "Whole-AOT release gate failed; see $reportPath`: $($report.failure)"
}
