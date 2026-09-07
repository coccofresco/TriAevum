[CmdletBinding()]
param(
    [string]$ProductExecutable =
        "I:\oot3dre_work\whole-aot-product-consumer\oot3d_native_game.exe",
    [string]$GoldenContract = "",
    [string]$OutputDirectory =
        "I:\oot3dre_work\whole-aot-product-run\golden-gate",
    [string]$GraphicsConfig =
        "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$SaveDataDirectory =
        "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [string]$KokiriCutsceneState =
        "I:\oot3dre_work\native_game\checkpoints\navi_kokiri_main_forest.oot3dsav",
    [string]$KokiriGameplayState =
        "I:\oot3dre_work\native_game\checkpoints\hudtest_after120.oot3dsav",
    [string]$GameplayInputTimeline =
        "I:\oot3dre_work\native_game\captures\grass_camera_perf_20260726\move_diagonal_absolute.json",
    [ValidateRange(1, 20)]
    [int]$Repetitions = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$launcher = Join-Path $PSScriptRoot "Invoke-Oot3dNativeGame.ps1"
if([string]::IsNullOrWhiteSpace($GoldenContract)) {
    $GoldenContract = Join-Path $repoRoot `
        "tools\oot3d\native_a32_runtime\whole_aot_product_golden.json"
}
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$contract = Get-Content -LiteralPath $GoldenContract -Raw | ConvertFrom-Json
if([string]$contract.format -ne "oot3d_whole_aot_product_golden_v1") {
    throw "Unsupported whole-AOT golden contract: $GoldenContract"
}

$artifactPaths = [ordered]@{
    graphics = [System.IO.Path]::GetFullPath($GraphicsConfig)
    kokiri_cutscene_state = [System.IO.Path]::GetFullPath($KokiriCutsceneState)
    kokiri_gameplay_state = [System.IO.Path]::GetFullPath($KokiriGameplayState)
    gameplay_input = [System.IO.Path]::GetFullPath($GameplayInputTimeline)
}
foreach($required in @($launcher, $ProductExecutable, $GoldenContract)) {
    if(-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required whole-AOT golden input is missing: $required"
    }
}
foreach($property in $contract.artifacts.psobject.Properties) {
    $path = $artifactPaths[[string]$property.Name]
    if([string]::IsNullOrWhiteSpace($path) -or
       -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Golden artifact is missing: $($property.Name)"
    }
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if($item.Length -ne [long]$property.Value.bytes -or
       $hash -ne [string]$property.Value.sha256) {
        throw "Golden artifact identity differs: $($property.Name)"
    }
}

New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null
$templatePath = Join-Path $resolvedOutput "graphics-template.json"
$graphics = Get-Content -LiteralPath $artifactPaths.graphics -Raw |
    ConvertFrom-Json
$graphics.Window.Width = [int]$contract.width
$graphics.Window.Height = [int]$contract.height
$graphics.Window.PositionX = 100
$graphics.Window.PositionY = 100
$graphics.Window.Fullscreen.Enabled = $false
$graphics.Graphics.Output.Width = [int]$contract.width
$graphics.Graphics.Output.Height = [int]$contract.height
$graphics.Graphics.Presentation.VSync = $false
$graphics | ConvertTo-Json -Depth 100 |
    Set-Content -LiteralPath $templatePath -Encoding utf8

$strictFields = @(
    "retained_arm_fallbacks",
    "whole_aot_memory_faults",
    "whole_aot_unsupported_exits",
    "mass_aot_block_limit_exits"
)

function Invoke-IsolatedLauncher {
    param([System.Collections.IDictionary]$Parameters)

    $hostExecutable = (Get-Process -Id $PID).Path
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $hostExecutable
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.ArgumentList.Add("-NoLogo")
    $startInfo.ArgumentList.Add("-NoProfile")
    $startInfo.ArgumentList.Add("-File")
    $startInfo.ArgumentList.Add($launcher)
    foreach($entry in $Parameters.GetEnumerator()) {
        if($entry.Value -is [bool]) {
            if([bool]$entry.Value) {
                $startInfo.ArgumentList.Add("-$($entry.Key)")
            }
            continue
        }
        $startInfo.ArgumentList.Add("-$($entry.Key)")
        if($entry.Value -is [System.IFormattable]) {
            $value = $entry.Value.ToString(
                $null,
                [Globalization.CultureInfo]::InvariantCulture)
        } else {
            $value = [string]$entry.Value
        }
        $startInfo.ArgumentList.Add($value)
    }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    return $process.ExitCode
}

$results = @()
$failures = @()
foreach($scenario in @($contract.scenarios)) {
    for($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $runDirectory = Join-Path $resolvedOutput (
            "$($scenario.name)\run_{0:D2}" -f $repetition)
        $outputPrefix = $resolvedOutput.TrimEnd('\', '/') +
            [System.IO.Path]::DirectorySeparatorChar
        $resolvedRun = [System.IO.Path]::GetFullPath($runDirectory)
        if(-not $resolvedRun.StartsWith(
                $outputPrefix,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Golden run directory escapes output root: $resolvedRun"
        }
        if(Test-Path -LiteralPath $resolvedRun) {
            [System.IO.Directory]::Delete($resolvedRun, $true)
        }
        New-Item -ItemType Directory -Force -Path $resolvedRun | Out-Null
        $runGraphics = Join-Path $resolvedRun "graphics.json"
        Copy-Item -LiteralPath $templatePath -Destination $runGraphics -Force
        $runSaveData = Join-Path $resolvedRun "savedata"
        if(Test-Path -LiteralPath $SaveDataDirectory -PathType Container) {
            Copy-Item -LiteralPath $SaveDataDirectory -Destination $runSaveData `
                -Recurse -Force
        } else {
            New-Item -ItemType Directory -Path $runSaveData | Out-Null
        }

        $runtimePath = Join-Path $resolvedRun "runtime.json"
        $screenshotPath = Join-Path $resolvedRun "frame.bmp"
        $audioPath = Join-Path $resolvedRun "audio.wav"
        $arguments = @{
            SkipBuild = $true
            Executable = $ProductExecutable
            Renderer = "nri"
            Frames = [int]$scenario.frames
            FixedDeltaSeconds = 1.0 / 60.0
            SimulationRate = 30
            PresentationRate = "free"
            Width = [int]$contract.width
            Height = [int]$contract.height
            GraphicsConfig = $runGraphics
            SaveDataDirectory = $runSaveData
            Screenshot = $screenshotPath
            ScreenshotSequence = $true
            ScreenshotStartFrame = 0
            ScreenshotInterval = [int]$scenario.checkpoint_interval
            Output = $runtimePath
            PicaAotShaderStrict = $true
        }
        if(-not [string]::IsNullOrWhiteSpace([string]$scenario.load_artifact)) {
            $arguments["LoadState"] =
                $artifactPaths[[string]$scenario.load_artifact]
        }
        if(-not [string]::IsNullOrWhiteSpace([string]$scenario.input_artifact)) {
            $arguments["InputTimeline"] =
                $artifactPaths[[string]$scenario.input_artifact]
        }
        if([string]::IsNullOrWhiteSpace([string]$scenario.audio_sha256)) {
            $arguments["DisableAudio"] = $true
        } else {
            $arguments["AudioPcmDump"] = $audioPath
        }

        $exitCode = Invoke-IsolatedLauncher -Parameters $arguments
        $runFailures = @()
        if($exitCode -ne 0) {
            $runFailures += "exit_code:$exitCode"
        }
        $dumps = @(Get-ChildItem (Join-Path $runSaveData "crashes") `
            -Filter *.dmp -ErrorAction SilentlyContinue)
        if($dumps.Count -ne 0) {
            $runFailures += "crash_dumps:$($dumps.Count)"
        }
        $runtime = $null
        if(Test-Path -LiteralPath $runtimePath -PathType Leaf) {
            $runtime = Get-Content -LiteralPath $runtimePath -Raw |
                ConvertFrom-Json
        } else {
            $runFailures += "runtime_missing"
        }
        if($null -ne $runtime) {
            if([uint64]$runtime.run_frames -ne [uint64]$scenario.frames -or
               -not [bool]$runtime.compiled_functions.whole_aot_available -or
               -not [bool]$runtime.compiled_functions.whole_aot_enabled) {
                $runFailures += "runtime_contract"
            }
            foreach($field in $strictFields) {
                $counter = $runtime.compiled_functions.psobject.Properties[
                    $field].Value
                if([uint64]$counter -ne 0) {
                    $runFailures += "strict:$field"
                }
            }
            $actualCalls = [ordered]@{
                whole_aot = [uint64]$runtime.compiled_functions.whole_aot_calls
                direct = [uint64]$runtime.compiled_functions.whole_aot_direct_calls
                indirect = [uint64]$runtime.compiled_functions.whole_aot_indirect_calls
                external = [uint64]$runtime.compiled_functions.whole_aot_external_calls
            }
            foreach($field in $actualCalls.Keys) {
                $expectedCalls = $scenario.calls.psobject.Properties[
                    $field].Value
                if([uint64]$actualCalls[$field] -ne
                   [uint64]$expectedCalls) {
                    $runFailures += "calls:$field"
                }
            }
        } else {
            $actualCalls = $null
        }

        $checkpointResults = @()
        foreach($checkpoint in $scenario.checkpoints.psobject.Properties) {
            $frame = [int]$checkpoint.Name
            $path = Join-Path $resolvedRun ("frame_{0:D6}.bmp" -f $frame)
            $actualHash = ""
            if(Test-Path -LiteralPath $path -PathType Leaf) {
                $actualHash = (Get-FileHash -LiteralPath $path `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            $identical = $actualHash -eq [string]$checkpoint.Value
            if(-not $identical) {
                $runFailures += "frame:$frame"
            }
            $checkpointResults += [ordered]@{
                frame = $frame
                identical = $identical
                expected_sha256 = [string]$checkpoint.Value
                actual_sha256 = $actualHash
            }
        }
        $audioHash = ""
        if(-not [string]::IsNullOrWhiteSpace([string]$scenario.audio_sha256)) {
            if(Test-Path -LiteralPath $audioPath -PathType Leaf) {
                $audioHash = (Get-FileHash -LiteralPath $audioPath `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            if($audioHash -ne [string]$scenario.audio_sha256) {
                $runFailures += "audio"
            }
        }
        $result = [ordered]@{
            scenario = [string]$scenario.name
            repetition = $repetition
            passed = $runFailures.Count -eq 0
            directory = $resolvedRun
            exit_code = $exitCode
            crash_dumps = $dumps.Count
            calls = $actualCalls
            audio_sha256 = $audioHash
            checkpoints = $checkpointResults
            failures = $runFailures
        }
        $results += $result
        if($runFailures.Count -ne 0) {
            $failures += $result
            break
        }
    }
    if($failures.Count -ne 0) {
        break
    }
}

$summary = [ordered]@{
    format = "oot3d_whole_aot_golden_gate_v1"
    passed = $failures.Count -eq 0
    golden_contract = [System.IO.Path]::GetFullPath($GoldenContract)
    golden_sha256 = (Get-FileHash -LiteralPath $GoldenContract `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    repetitions = $Repetitions
    completed_runs = $results.Count
    expected_runs = @($contract.scenarios).Count * $Repetitions
    results = $results
}
$summaryPath = Join-Path $resolvedOutput "golden-gate.json"
$summary | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ConvertTo-Json -Depth 12
if(-not $summary.passed) {
    throw "Whole-AOT golden gate failed; see $summaryPath"
}
