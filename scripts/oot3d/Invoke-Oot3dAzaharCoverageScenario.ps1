param(
    [Parameter(Mandatory = $true)]
    [string]$ScenarioId,
    [string]$CatalogPath = (Join-Path $PSScriptRoot "../../tools/oot3d/native_a32_runtime/oot3d_azahar_coverage_scenarios.json"),
    [string]$AzaharExe = "I:\oot3dre_work\azahar-oot3d-coverage-build\bin\Release\azahar.exe",
    [string]$RomPath = "E:\ppssppvr\oot3d_decomp\oot3d.cci",
    [string]$OutputRoot = "I:\oot3dre_work\azahar-coverage",
    [int]$Slot = 1,
    [string]$SeedSavestatePath = "",
    [string]$AzaharUserDirectory = "$env:APPDATA\Azahar",
    [ValidateSet("opengl", "vulkan")]
    [string]$Backend = "vulkan",
    [int]$AutoLoadDelayMs = 1500,
    [int]$SettleMilliseconds = -1,
    [int]$TimeoutSeconds = 90,
    [int]$MaxVerticesPerDraw = 65536,
    [int]$CaptureFramesOverride = 0,
    [switch]$SkipFramebuffer,
    [switch]$CompactEvidence,
    [switch]$ShaderSeed,
    [switch]$ShowWindow
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
    param([hashtable]$OldValues, [string]$Name, [AllowNull()][string]$Value)
    if (-not $OldValues.ContainsKey($Name)) {
        $OldValues[$Name] = [Environment]::GetEnvironmentVariable($Name, "Process")
    }
    [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

function Restore-Environment {
    param([hashtable]$OldValues)
    foreach ($name in $OldValues.Keys) {
        [Environment]::SetEnvironmentVariable($name, $OldValues[$name], "Process")
    }
}

function Stop-ProcessQuietly {
    param([System.Diagnostics.Process]$Process)
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Read-JsonWhenComplete {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Test-PicaFrameComplete {
    param([System.IO.FileInfo]$Path)
    try {
        return [bool]((Get-Content -LiteralPath $Path.FullName -Tail 1) -match '"event"\s*:\s*"capture_end"')
    } catch {
        return $false
    }
}

Assert-FileExists $CatalogPath "Azahar coverage catalog"
Assert-FileExists $AzaharExe "instrumented Azahar executable"
Assert-FileExists $RomPath "OOT3D game image"
if ($Slot -lt 1 -or $Slot -gt 10) {
    throw "Savestate slot must be between 1 and 10."
}
if ($CaptureFramesOverride -lt 0) {
    throw "CaptureFramesOverride must be zero or greater."
}

$catalog = Get-Content -LiteralPath $CatalogPath -Raw | ConvertFrom-Json
if ([string]$catalog.format -ne "oot3d_azahar_coverage_catalog_v1") {
    throw "Unsupported Azahar coverage catalog format: $($catalog.format)"
}
$matches = @($catalog.scenarios | Where-Object { [string]$_.id -eq $ScenarioId })
if ($matches.Count -ne 1) {
    throw "Scenario '$ScenarioId' matched $($matches.Count) catalog entries."
}
$scenario = $matches[0]
$runtime = $catalog.runtime_contract
$settleMs = if ($SettleMilliseconds -ge 0) {
    $SettleMilliseconds
} else {
    [int][Math]::Ceiling(([int]$scenario.execution.settle_presented_frames * 1000.0) / 60.0)
}
$captureFrames = if ($CaptureFramesOverride -gt 0) {
    $CaptureFramesOverride
} else {
    [Math]::Max(1, [int]$scenario.execution.capture_frames)
}
$effectiveTimeout = [Math]::Max($TimeoutSeconds, [int]$scenario.execution.ready_timeout_seconds + 15)
$safeId = $ScenarioId -replace '[^A-Za-z0-9_.-]+', '_'
$captureName = "{0}_{1}_{2}" -f $safeId, $Backend, (Get-Date).ToString("yyyyMMdd_HHmmss")
$captureDir = Join-Path $OutputRoot $captureName
$derivedDir = Join-Path $captureDir "derived"
$statusPath = Join-Path $captureDir "scenario_status.json"
$picaTriggerPath = Join-Path $captureDir "pica.trigger"
$screenshotTriggerPath = Join-Path $captureDir "screenshot.trigger"
$screenshotPath = Join-Path $captureDir "framebuffer.png"
$metadataPath = $screenshotPath + ".capture.json"
$summaryPath = Join-Path $captureDir "coverage_summary.json"
$convertTraceScript = Join-Path $PSScriptRoot "Convert-Oot3dNativePicaTrace.ps1"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$shaderCoverageScript = Join-Path $repoRoot "tools\oot3d\native_a32_runtime\summarize_azahar_shader_coverage.py"
$shaderCoveragePath = Join-Path $captureDir "shader_coverage.json"
Assert-FileExists $convertTraceScript "PICA trace converter"
Assert-FileExists $shaderCoverageScript "Azahar shader coverage summarizer"

$titleIdHex = "{0:X16}" -f [uint64]$runtime.title_id
$statesDirectory = Join-Path $AzaharUserDirectory "states"
$installedSeedPath = Join-Path $statesDirectory ("{0}.{1:D2}.cst" -f $titleIdHex, $Slot)
$seedBackupPath = Join-Path $captureDir "seed_savestate_backup.cst"
$seedWasInstalled = $false
$seedHadOriginal = $false
New-Item -ItemType Directory -Force -Path $captureDir, $derivedDir | Out-Null
$oldEnvironment = @{}
$process = $null
$readyStatus = $null
$readyAt = $null
$triggeredAt = $null

try {
    if ([string]::IsNullOrWhiteSpace($SeedSavestatePath)) {
        Assert-FileExists $installedSeedPath "Azahar seed savestate slot $Slot"
    } else {
        Assert-FileExists $SeedSavestatePath "Explicit seed savestate"
        New-Item -ItemType Directory -Force -Path $statesDirectory | Out-Null
        $resolvedSeedPath = (Resolve-Path -LiteralPath $SeedSavestatePath).Path
        if (-not [string]::Equals($resolvedSeedPath, $installedSeedPath,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            if (Test-Path -LiteralPath $installedSeedPath -PathType Leaf) {
                Copy-Item -LiteralPath $installedSeedPath -Destination $seedBackupPath -Force
                $seedHadOriginal = $true
            }
            Copy-Item -LiteralPath $resolvedSeedPath -Destination $installedSeedPath -Force
            $seedWasInstalled = $true
        }
    }

    $environment = [ordered]@{
        OOT3D_AUTO_LOAD_STATE_SLOT = [string]$Slot
        OOT3D_AUTO_LOAD_STATE_DELAY_MS = [string]$AutoLoadDelayMs
        OOT3D_DIAGNOSTIC_FORCE_NATIVE_RENDER = "1"
        OOT3D_DIAGNOSTIC_GRAPHICS_API = $Backend
        OOT3D_CAPTURE_FRAMEBUFFER_METADATA = if ($SkipFramebuffer.IsPresent) { $null } else { "1" }
        OOT3D_AUTO_SCREENSHOT_PATH = if ($SkipFramebuffer.IsPresent) { $null } else { $screenshotPath }
        OOT3D_AUTO_SCREENSHOT_TRIGGER = if ($SkipFramebuffer.IsPresent) { $null } else { $screenshotTriggerPath }
        OOT3D_AUTO_SCREENSHOT_DELAY_MS = $null
        OOT3D_AUTO_EXIT_AFTER_SCREENSHOT_MS = $null
        OOT3D_PICA_DUMP = "1"
        OOT3D_PICA_DUMP_DIR = $captureDir
        OOT3D_PICA_DUMP_TRIGGER = $picaTriggerPath
        OOT3D_PICA_DUMP_FRAMES = [string]$captureFrames
        OOT3D_PICA_DUMP_MAX_VERTICES = [string][Math]::Max(1, $MaxVerticesPerDraw)
        OOT3D_PICA_DUMP_IMMEDIATE = "0"
        OOT3D_PICA_DUMP_SHADER_SEED = if ($ShaderSeed.IsPresent) { "1" } else { $null }
        OOT3D_SCENARIO_ID = [string]$scenario.id
        OOT3D_SCENARIO_STATUS_PATH = $statusPath
        OOT3D_SCENARIO_ENTRANCE = [string][int]$scenario.entrance_index
        OOT3D_SCENARIO_EXPECTED_SCENE = [string][int]$scenario.scene_id
        OOT3D_SCENARIO_TITLE_ID = [string][uint64]$runtime.title_id
        OOT3D_SCENARIO_PLAYER_UPDATE = [string][uint32]$runtime.player_update_function
        OOT3D_SCENARIO_GAME_STATE_UPDATE = [string][uint32]$runtime.game_state_update_function
        OOT3D_SCENARIO_PLAY_INIT = [string][uint32]$runtime.play_init_function
        OOT3D_SCENARIO_PLAY_MAIN = [string][uint32]$runtime.play_main_function
        OOT3D_SCENARIO_REQUEST_TRANSITION = [string][uint32]$runtime.request_transition_function
        OOT3D_SCENARIO_PREPARE_TRANSITION = [string][uint32]$runtime.prepare_transition_effect_function
        OOT3D_SCENARIO_CURRENT_ENTRANCE = [string][uint32]$runtime.current_entrance_address
        OOT3D_SCENARIO_ENTRANCE_TABLE = [string][uint32]$runtime.entrance_table_address
        OOT3D_SCENARIO_PLAY_SCENE_ID_OFFSET = [string][uint32]$runtime.play_scene_id_offset
        OOT3D_SCENARIO_PENDING_ENTRANCE_OFFSET = [string][uint32]$runtime.pending_entrance_offset
        OOT3D_SCENARIO_PENDING_TRIGGER_OFFSET = [string][uint32]$runtime.pending_trigger_offset
        OOT3D_SCENARIO_TRANSITION_EFFECT_OFFSET = [string][uint32]$runtime.transition_effect_offset
        OOT3D_SCENARIO_TRANSITION_TRIGGER = [string][uint32]$runtime.transition_trigger
        OOT3D_SCENARIO_AGE = $null
        OOT3D_SCENARIO_CUTSCENE_INDEX = $null
        OOT3D_SCENARIO_DAY_TIME = $null
        OOT3D_SCENARIO_SKYBOX_TIME = $null
    }
    foreach ($property in @("age", "cutscene_index", "day_time", "skybox_time")) {
        $value = $scenario.native_state.PSObject.Properties[$property]
        if ($null -ne $value) {
            $environment["OOT3D_SCENARIO_$($property.ToUpperInvariant())"] = [string]$value.Value
        }
    }
    foreach ($entry in $environment.GetEnumerator()) {
        Set-EnvAndRemember $oldEnvironment $entry.Key $entry.Value
    }

    $startArguments = @{
        FilePath = $AzaharExe
        ArgumentList = @($RomPath)
        WorkingDirectory = (Split-Path -Parent $AzaharExe)
        PassThru = $true
    }
    if (-not $ShowWindow.IsPresent) {
        $startArguments.WindowStyle = "Hidden"
    }
    $process = Start-Process @startArguments
    $deadline = (Get-Date).AddSeconds($effectiveTimeout)
    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) {
            throw "Azahar exited with code $($process.ExitCode) before scenario readiness."
        }
        $status = Read-JsonWhenComplete $statusPath
        if ($null -ne $status) {
            if ([string]$status.status -eq "failed") {
                throw "Scenario injection failed: $($status.detail)"
            }
            if ([string]$status.status -eq "ready") {
                $readyStatus = $status
                $readyAt = Get-Date
                break
            }
        }
        Start-Sleep -Milliseconds 50
    }
    if ($null -eq $readyStatus) {
        throw "Scenario '$ScenarioId' did not become ready within $effectiveTimeout seconds."
    }

    if ($settleMs -gt 0) {
        Start-Sleep -Milliseconds $settleMs
    }
    "capture $ScenarioId" | Set-Content -LiteralPath $picaTriggerPath -Encoding ascii
    if (-not $SkipFramebuffer.IsPresent) {
        "capture $ScenarioId" | Set-Content -LiteralPath $screenshotTriggerPath -Encoding ascii
    }
    $triggeredAt = Get-Date

    $captureDeadline = (Get-Date).AddSeconds([Math]::Max(15, $effectiveTimeout))
    $frames = @()
    while ((Get-Date) -lt $captureDeadline) {
        $frames = @(Get-ChildItem -LiteralPath $captureDir -Filter "oot3d_pica_frame_*.jsonl" -File -ErrorAction SilentlyContinue | Sort-Object Name)
        $completeFrames = @($frames | Where-Object { Test-PicaFrameComplete $_ })
        $screenshotReady = $SkipFramebuffer.IsPresent -or (
            (Test-Path -LiteralPath $screenshotPath -PathType Leaf) -and
            (Test-Path -LiteralPath $metadataPath -PathType Leaf))
        if ($completeFrames.Count -ge $captureFrames -and $screenshotReady) {
            break
        }
        if ($process.HasExited -and (-not $screenshotReady -or $completeFrames.Count -lt $captureFrames)) {
            throw "Azahar exited before producing the requested capture evidence."
        }
        Start-Sleep -Milliseconds 100
    }
    $frames = @($frames | Where-Object { Test-PicaFrameComplete $_ } | Select-Object -First $captureFrames)
    if ($frames.Count -lt $captureFrames) {
        throw "Only $($frames.Count)/$captureFrames complete PICA frame dumps were produced."
    }
    if (-not $SkipFramebuffer.IsPresent) {
        Assert-FileExists $screenshotPath "Azahar framebuffer capture"
        Assert-FileExists $metadataPath "Azahar framebuffer capture metadata"
    }

    $converted = @()
    if (-not $CompactEvidence.IsPresent -and -not $ShaderSeed.IsPresent) {
        foreach ($frame in $frames) {
            $outputPath = Join-Path $derivedDir ($frame.BaseName + ".native_pica_register_trace.json")
            & $convertTraceScript -InputPath $frame.FullName -OutputPath $outputPath -InputFormat jsonl | Out-Null
            $converted += $outputPath
        }
    }

    if (-not $process.HasExited) {
        if (-not $process.CloseMainWindow() -or -not $process.WaitForExit(5000)) {
            Stop-ProcessQuietly $process
        }
    }
    $finalStatus = Read-JsonWhenComplete $statusPath
    $summary = [ordered]@{
        format = "oot3d_azahar_coverage_capture_v1"
        generated_at = (Get-Date).ToString("o")
        evidence_role = if ($ShaderSeed.IsPresent) { "offline_shader_preparation_not_gameplay_replay" } else { "validation_only_not_runtime_input" }
        scenario = $scenario
        catalog_path = (Resolve-Path $CatalogPath).Path
        catalog_decomp_provenance = $catalog.provenance.decomp
        azahar_exe = (Resolve-Path $AzaharExe).Path
        rom_path = (Resolve-Path $RomPath).Path
        backend = $Backend
        shader_seed_capture = $ShaderSeed.IsPresent
        seed_savestate_slot = $Slot
        seed_savestate_path = $installedSeedPath
        ready_at = $readyAt.ToString("o")
        triggered_at = $triggeredAt.ToString("o")
        settle_milliseconds_after_ready = $settleMs
        scenario_status = $finalStatus
        capture_frame_count_requested = $captureFrames
        framebuffer_capture_enabled = -not $SkipFramebuffer.IsPresent
        framebuffer_path = if ($SkipFramebuffer.IsPresent) { $null } else { $screenshotPath }
        framebuffer_metadata_path = if ($SkipFramebuffer.IsPresent) { $null } else { $metadataPath }
        framebuffer_metadata = if (-not $SkipFramebuffer.IsPresent -and
            (Test-Path -LiteralPath $metadataPath)) {
            Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        } else { $null }
        pica_frame_count = $frames.Count
        pica_evidence_retained = -not $CompactEvidence.IsPresent -or $ShaderSeed.IsPresent
        pica_frames = @($frames | ForEach-Object { $_.FullName })
        converted_register_traces = $converted
        process_exit_code = if ($process.HasExited) { $process.ExitCode } else { $null }
    }
    $summary | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $summaryPath -Encoding ascii
    & python $shaderCoverageScript --capture-summary $summaryPath --output $shaderCoveragePath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Azahar shader coverage summarizer failed with exit code $LASTEXITCODE."
    }
    $summary["shader_coverage_path"] = $shaderCoveragePath
    $summary["shader_coverage"] = Get-Content -LiteralPath $shaderCoveragePath -Raw | ConvertFrom-Json
    # Shader seed resources are the corpus, not disposable diagnostic traces.
    if ($CompactEvidence.IsPresent -and -not $ShaderSeed.IsPresent) {
        foreach ($frame in $frames) {
            Remove-Item -LiteralPath $frame.FullName -Force
        }
        $summary["pica_frames"] = @()
    }
    $summary | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $summaryPath -Encoding ascii
    $summary | ConvertTo-Json -Depth 8
} finally {
    Stop-ProcessQuietly $process
    Restore-Environment $oldEnvironment
    if ($seedWasInstalled) {
        if ($seedHadOriginal) {
            Copy-Item -LiteralPath $seedBackupPath -Destination $installedSeedPath -Force
        } else {
            Remove-Item -LiteralPath $installedSeedPath -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "Azahar coverage scenario complete: $ScenarioId"
Write-Host "Capture: $captureDir"
