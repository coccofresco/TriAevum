[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [string]$Executable = "",
    [switch]$ValidateOnly,
    [int]$Frames = 0,
    [ValidateRange(0, 1000000)]
    [int]$BenchmarkWarmupFrames = 0,
    [double]$FixedDeltaSeconds = 0,
    [string]$InputTimeline = "",
    [int[]]$RoomRequestSmoke = @(),
    [double]$MaxSeconds = 0,
    [ValidateRange(0, 300)]
    [int]$BoundedStartupGraceSeconds = 30,
    [ValidateRange(1, 16384)]
    [int]$Width = 1280,
    [ValidateRange(1, 16384)]
    [int]$Height = 720,
    [ValidateSet("nri", "opengl", "vulkan")]
    [string]$Renderer = "nri",
    [ValidateSet("oot3d", "topscreen")]
    [string]$UiProfile = "topscreen",
    [string]$GraphicsConfig = "I:\oot3dre_work\native_game\oot3d_native_game.json",
    [string]$ControlsConfig = "",
    [string]$TopScreenConfig = "",
    [string]$TopScreenTextureOverrides = "",
    [string]$Output = "I:\oot3dre_work\native_game\kokiri_native_game.json",
    [string]$Screenshot = "",
    [string]$A32ProcessManifest = "",
    [string]$SaveDataDirectory = "I:\oot3dre_work\native_game\oot3d_native_savedata",
    [switch]$ScreenshotSequence,
    [switch]$ExtendedDiagnostics,
    [ValidateSet("native30_interpolated", "native30_no_interpolation", "enhanced60")]
    [string]$GameplayTiming = "native30_interpolated",
    [ValidateRange(1, 1000)]
    [int]$SimulationRate = 30,
    [string]$PresentationRate = "60",
    [switch]$DisableVisualInterpolation,
    [switch]$ProfileA32Blocks,
    [string]$A32BlockTrace = "",
    [string]$PicaSemanticTrace = "",
    [string]$PicaAotShaderPack = "",
    [switch]$PicaAotShaderStrict,
    [switch]$DisableDefaultPicaAotShaderPack,
    [string]$PicaEffectiveShaderInventory = "",
    [string]$PicaPipelineInventory = "",
    [string]$PicaPipelineManifest = "",
    [switch]$PicaPipelinePrewarm,
    [string]$ScenarioCatalog = "",
    [string]$Scenario = "",
    [switch]$ScenarioStrict,
    [switch]$ScenarioAutoExit,
    [switch]$ProfileA32Runtime,
    [switch]$DisableCompiledFunctions,
    [switch]$DisableTypedGameplay,
    [switch]$EnableSourceGameplayProfile,
    [switch]$EnableSourceActorInitContext,
    [switch]$EnableSourceActorUpdateAll,
    [switch]$EnableSourceCutsceneUpdateFrame,
    [switch]$EnableSourceCutsceneProcessCommands,
    [switch]$EnableSourceCameraUpdate,
    [switch]$EnableSourcePlayerUpdate,
    [switch]$EnableSourcePlayerUpdateCommon,
    [switch]$EnableSourceCsabCurves,
    [switch]$DisableManualCompiledFunctions,
    [switch]$DisableTrueAotBlocks,
    [switch]$DisableWholeAot,
    [switch]$DisableMassAot,
    [switch]$EnableMassAot,
    [switch]$DisableAudio,
    [string]$AudioPcmDump = "",
    [string]$LoadState = "",
    [string]$QuickState = "",
    [string]$SaveState = "",
    [int]$SaveStateFrame = -1,
    [int]$ScreenshotStartFrame = 0,
    [int]$ScreenshotInterval = 1,
    [string]$PlayablePack = "I:\oot3dre_work\playable_pack\oot3d_playable_pack.json",
    [string]$RoomCompilationUnit = "I:\oot3dre_work\room_compilation_units\scene_entry_SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE_KOKIRI_FOREST_INITIAL_CHILD_DAY.json"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($DisableMassAot -and $EnableMassAot) {
    throw "DisableMassAot and EnableMassAot are mutually exclusive"
}
if ($Frames -gt 0 -and $BenchmarkWarmupFrames -ge $Frames) {
    throw "BenchmarkWarmupFrames must be lower than Frames"
}

if ($PresentationRate -ne "free") {
    $parsedPresentationRate = 0
    if (-not [int]::TryParse($PresentationRate, [ref]$parsedPresentationRate) -or
        $parsedPresentationRate -lt 30) {
        throw "PresentationRate must be 'free' or an integer of at least 30"
    }
    $PresentationRate = [string]$parsedPresentationRate
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildScript = Join-Path $repoRoot "tools\oot3d\build_fast_dev.ps1"
if ($UiProfile -eq "topscreen" -and
    [string]::IsNullOrWhiteSpace($TopScreenTextureOverrides)) {
    $TopScreenTextureOverrides = Join-Path $repoRoot `
        "tools\oot3d\ui_topscreen\assets\atlas_overrides.o3tu"
}
if ($UiProfile -eq "topscreen" -and
    [string]::IsNullOrWhiteSpace($TopScreenConfig)) {
    $TopScreenConfig = Join-Path $repoRoot "config\topscreen_ui.example.json"
}
$resolvedGraphicsConfig = [System.IO.Path]::GetFullPath($GraphicsConfig)
$graphicsConfigDirectory = Split-Path -Parent $resolvedGraphicsConfig
if (-not [string]::IsNullOrWhiteSpace($graphicsConfigDirectory) -and
    -not (Test-Path -LiteralPath $graphicsConfigDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $graphicsConfigDirectory -Force | Out-Null
}
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $executable = Join-Path $repoRoot "build-codex\oot3d_native_game.exe"
} else {
    $executable = [System.IO.Path]::GetFullPath($Executable)
}
if ([string]::IsNullOrWhiteSpace($PicaAotShaderPack) -and
    -not $DisableDefaultPicaAotShaderPack.IsPresent) {
    $defaultPicaAotShaderPack = Join-Path (
        Split-Path -Parent $executable) "oot3d_pica_default.o3ps"
    if (Test-Path -LiteralPath $defaultPicaAotShaderPack -PathType Leaf) {
        $PicaAotShaderPack = $defaultPicaAotShaderPack
    }
}
if ([string]::IsNullOrWhiteSpace($A32ProcessManifest)) {
    $A32ProcessManifest =
        "I:\oot3dre_work\native_game\oot3d_native_process_manifest.json"
} else {
    $A32ProcessManifest = [System.IO.Path]::GetFullPath($A32ProcessManifest)
}

function Set-Oot3dNativeGameForeground {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq ("Oot3dNativeGameWindowFocus" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Oot3dNativeGameWindowFocus {
    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(
        IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr processId);

    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();

    [DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);

    [DllImport("user32.dll")]
    public static extern void SwitchToThisWindow(IntPtr hWnd, bool altTab);
}
"@
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited) {
        $Process.Refresh()
        $window = $Process.MainWindowHandle
        if ($window -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 50
            continue
        }

        $foreground = [Oot3dNativeGameWindowFocus]::GetForegroundWindow()
        $currentThread = [Oot3dNativeGameWindowFocus]::GetCurrentThreadId()
        $foregroundThread = [Oot3dNativeGameWindowFocus]::GetWindowThreadProcessId(
            $foreground, [IntPtr]::Zero)
        $windowThread = [Oot3dNativeGameWindowFocus]::GetWindowThreadProcessId(
            $window, [IntPtr]::Zero)
        $attachedForeground = $foregroundThread -ne 0 -and $foregroundThread -ne $currentThread
        $attachedWindow = $windowThread -ne 0 -and $windowThread -ne $currentThread

        if ($attachedForeground) {
            [Oot3dNativeGameWindowFocus]::AttachThreadInput(
                $currentThread, $foregroundThread, $true) | Out-Null
        }
        if ($attachedWindow) {
            [Oot3dNativeGameWindowFocus]::AttachThreadInput(
                $currentThread, $windowThread, $true) | Out-Null
        }

        try {
            $noMoveNoSizeShow = [uint32](0x0001 -bor 0x0002 -bor 0x0040)
            [Oot3dNativeGameWindowFocus]::ShowWindowAsync($window, 9) | Out-Null
            [Oot3dNativeGameWindowFocus]::SetWindowPos(
                $window, [IntPtr](-1), 0, 0, 0, 0, $noMoveNoSizeShow) | Out-Null
            [Oot3dNativeGameWindowFocus]::BringWindowToTop($window) | Out-Null
            [Oot3dNativeGameWindowFocus]::SetForegroundWindow($window) | Out-Null
            [Oot3dNativeGameWindowFocus]::SwitchToThisWindow($window, $true)
            Start-Sleep -Milliseconds 100
            [Oot3dNativeGameWindowFocus]::SetWindowPos(
                $window, [IntPtr](-2), 0, 0, 0, 0, $noMoveNoSizeShow) | Out-Null
        } finally {
            if ($attachedWindow) {
                [Oot3dNativeGameWindowFocus]::AttachThreadInput(
                    $currentThread, $windowThread, $false) | Out-Null
            }
            if ($attachedForeground) {
                [Oot3dNativeGameWindowFocus]::AttachThreadInput(
                    $currentThread, $foregroundThread, $false) | Out-Null
            }
        }
        return $true
    }
    return $false
}

if (-not $SkipBuild) {
    & $buildScript -Target oot3d_native_game -Parallel 16
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$inputs = @(
    $executable,
    "I:\oot3dre_work\standalone_demo\kokiri_forest\demo_manifest.json",
    (Join-Path $repoRoot "runtime/three_ds_recomp\src\fast"),
    "I:\oot3dre_work\playable_catalog\oot3d_asset_catalog.json",
    $PlayablePack,
    "I:\oot3dre_work\playable_catalog\oot3d_semantic_route_catalog.json",
    (Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\player_animation_group_native_contract.json"),
    (Join-Path $repoRoot "tools\oot3d\oot3d_asset_tool\profiles\link_child_native_runtime_semantics.json"),
    (Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\player_collision_action_native_contract.json"),
    (Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\actor_core_native_contract.json"),
    "I:\oot3dre_work\playable_actors\oot3d-actors-native.o2r",
    $RoomCompilationUnit,
    "I:\oot3dre_work\native_corpus\kokiri_initial_607aaa6\manifest.json"
    $A32ProcessManifest
)
foreach ($path in $inputs) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required native OOT3D game input is missing: $path"
    }
}

if ($PSBoundParameters.ContainsKey("SimulationRate") -and
    $PSBoundParameters.ContainsKey("GameplayTiming")) {
    throw "Specify either -GameplayTiming or the legacy -SimulationRate, not both."
}

$launchArgs = @(
    "--manifest", $inputs[1],
    "--resource-root", $inputs[2],
    "--asset-catalog", $inputs[3],
    "--playable-pack", $inputs[4],
    "--route-catalog", $inputs[5],
    "--player-animation-contract", $inputs[6],
    "--player-runtime-semantics", $inputs[7],
    "--player-collision-action-contract", $inputs[8],
    "--actor-core-contract", $inputs[9],
    "--actor-shard", $inputs[10],
    "--room-compilation-unit", $inputs[11],
    "--native-closure-manifest", $inputs[12],
    "--a32-process-manifest", $inputs[13],
    "--route-id", "scene_entry:SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE:KOKIRI_FOREST_INITIAL_CHILD_DAY",
    "--renderer", $Renderer,
    "--ui-profile", $UiProfile,
    "--config", $resolvedGraphicsConfig,
    "--presentation-rate", $PresentationRate,
    "--output", $Output,
    "--width", [string]$Width,
    "--height", [string]$Height
)
if ($PSBoundParameters.ContainsKey("SimulationRate")) {
    $launchArgs += @("--simulation-rate", [string]$SimulationRate)
} else {
    $launchArgs += @("--gameplay-timing", $GameplayTiming)
}
if ($ValidateOnly) {
    $launchArgs += "--validate-only"
}
if ($ExtendedDiagnostics) {
    $launchArgs += "--extended-diagnostics"
}
if ($DisableVisualInterpolation) {
    $launchArgs += "--disable-visual-interpolation"
}
if ($ProfileA32Blocks) {
    $launchArgs += "--profile-a32-blocks"
}
if (-not [string]::IsNullOrWhiteSpace($A32BlockTrace)) {
    $launchArgs += @(
        "--trace-a32-blocks",
        [System.IO.Path]::GetFullPath($A32BlockTrace)
    )
}
if (-not [string]::IsNullOrWhiteSpace($PicaSemanticTrace)) {
    $launchArgs += @(
        "--pica-semantic-trace",
        [System.IO.Path]::GetFullPath($PicaSemanticTrace)
    )
}
if (-not [string]::IsNullOrWhiteSpace($PicaAotShaderPack)) {
    $resolvedPicaAotShaderPack =
        [System.IO.Path]::GetFullPath($PicaAotShaderPack)
    if (-not (Test-Path -LiteralPath $resolvedPicaAotShaderPack -PathType Leaf)) {
        throw "PICA AOT shader pack is missing: $resolvedPicaAotShaderPack"
    }
    $launchArgs += @("--pica-aot-shader-pack", $resolvedPicaAotShaderPack)
}
if ($PicaAotShaderStrict) {
    if ([string]::IsNullOrWhiteSpace($PicaAotShaderPack)) {
        throw "PicaAotShaderStrict requires PicaAotShaderPack"
    }
    $launchArgs += "--pica-aot-shader-strict"
}
if (-not [string]::IsNullOrWhiteSpace($PicaEffectiveShaderInventory)) {
    $launchArgs += @(
        "--pica-effective-shader-inventory",
        [System.IO.Path]::GetFullPath($PicaEffectiveShaderInventory)
    )
}
if (-not [string]::IsNullOrWhiteSpace($PicaPipelineInventory)) {
    $launchArgs += @(
        "--pica-pipeline-inventory",
        [System.IO.Path]::GetFullPath($PicaPipelineInventory)
    )
}
if (-not [string]::IsNullOrWhiteSpace($PicaPipelineManifest)) {
    $resolvedPicaPipelineManifest =
        [System.IO.Path]::GetFullPath($PicaPipelineManifest)
    if (-not (Test-Path -LiteralPath $resolvedPicaPipelineManifest -PathType Leaf)) {
        throw "PICA pipeline manifest is missing: $resolvedPicaPipelineManifest"
    }
    $launchArgs += @(
        "--pica-pipeline-manifest",
        $resolvedPicaPipelineManifest
    )
}
if ($PicaPipelinePrewarm) {
    if ([string]::IsNullOrWhiteSpace($PicaPipelineManifest) -or
        [string]::IsNullOrWhiteSpace($PicaAotShaderPack)) {
        throw "PicaPipelinePrewarm requires PicaPipelineManifest and PicaAotShaderPack"
    }
    $launchArgs += "--pica-pipeline-prewarm"
}
if (-not [string]::IsNullOrWhiteSpace($ScenarioCatalog) -or
    -not [string]::IsNullOrWhiteSpace($Scenario)) {
    if ([string]::IsNullOrWhiteSpace($ScenarioCatalog) -or
        [string]::IsNullOrWhiteSpace($Scenario)) {
        throw "ScenarioCatalog and Scenario must be specified together"
    }
    $resolvedScenarioCatalog =
        [System.IO.Path]::GetFullPath($ScenarioCatalog)
    if (-not (Test-Path -LiteralPath $resolvedScenarioCatalog -PathType Leaf)) {
        throw "Structural scenario catalog is missing: $resolvedScenarioCatalog"
    }
    $launchArgs += @(
        "--scenario-catalog", $resolvedScenarioCatalog,
        "--scenario", $Scenario
    )
}
if ($ScenarioStrict) {
    if ([string]::IsNullOrWhiteSpace($Scenario)) {
        throw "ScenarioStrict requires ScenarioCatalog and Scenario"
    }
    $launchArgs += "--scenario-strict"
}
if ($ScenarioAutoExit) {
    if ([string]::IsNullOrWhiteSpace($Scenario)) {
        throw "ScenarioAutoExit requires ScenarioCatalog and Scenario"
    }
    $launchArgs += "--scenario-auto-exit"
}
if ($ProfileA32Runtime) {
    $launchArgs += "--profile-a32-runtime"
}
if ($DisableCompiledFunctions) {
    $launchArgs += "--disable-compiled-functions"
}
if ($DisableTypedGameplay) {
    $launchArgs += "--disable-typed-gameplay"
}
if ($EnableSourceGameplayProfile) {
    $launchArgs += "--enable-source-gameplay-profile"
}
if ($EnableSourceActorInitContext) {
    $launchArgs += "--enable-source-actor-init-context"
}
if ($EnableSourceActorUpdateAll) {
    $launchArgs += "--enable-source-actor-update-all"
}
if ($EnableSourceCutsceneUpdateFrame) {
    $launchArgs += "--enable-source-cutscene-update-frame"
}
if ($EnableSourceCutsceneProcessCommands) {
    $launchArgs += "--enable-source-cutscene-process-commands"
}
if ($EnableSourceCameraUpdate) {
    $launchArgs += "--enable-source-camera-update"
}
if ($EnableSourcePlayerUpdate) {
    $launchArgs += "--enable-source-player-update"
}
if ($EnableSourcePlayerUpdateCommon) {
    $launchArgs += "--enable-source-player-update-common"
}
if ($EnableSourceCsabCurves) {
    $launchArgs += "--enable-source-csab-curves"
}
if ($DisableManualCompiledFunctions) {
    $launchArgs += "--disable-manual-compiled-functions"
}
if ($DisableTrueAotBlocks) {
    $launchArgs += "--disable-true-aot-blocks"
}
if ($DisableWholeAot) {
    $launchArgs += "--disable-whole-aot"
}
if ($DisableMassAot) {
    $launchArgs += "--disable-mass-aot"
}
if ($EnableMassAot) {
    $launchArgs += "--enable-mass-aot"
}
if ($DisableAudio) {
    $launchArgs += "--disable-audio"
}
if (-not [string]::IsNullOrWhiteSpace($AudioPcmDump)) {
    $launchArgs += @(
        "--audio-pcm-dump",
        [System.IO.Path]::GetFullPath($AudioPcmDump)
    )
}
if (-not [string]::IsNullOrWhiteSpace($LoadState)) {
    $resolvedLoadState = [System.IO.Path]::GetFullPath($LoadState)
    if (-not (Test-Path -LiteralPath $resolvedLoadState -PathType Leaf)) {
        throw "Savestate is missing: $resolvedLoadState"
    }
    $launchArgs += @("--load-state", $resolvedLoadState)
}
if (-not [string]::IsNullOrWhiteSpace($QuickState)) {
    $launchArgs += @(
        "--quick-state",
        [System.IO.Path]::GetFullPath($QuickState)
    )
}
if (-not [string]::IsNullOrWhiteSpace($SaveState) -or $SaveStateFrame -ge 0) {
    if ([string]::IsNullOrWhiteSpace($SaveState) -or $SaveStateFrame -lt 0) {
        throw "SaveState and a non-negative SaveStateFrame must be specified together"
    }
    $launchArgs += @(
        "--save-state", [System.IO.Path]::GetFullPath($SaveState),
        "--save-state-frame", [string]$SaveStateFrame
    )
}
if ($Frames -gt 0) {
    $launchArgs += @("--frames", [string]$Frames)
}
if ($BenchmarkWarmupFrames -gt 0) {
    $launchArgs += @(
        "--benchmark-warmup-frames",
        [string]$BenchmarkWarmupFrames
    )
}
if ($FixedDeltaSeconds -gt 0) {
    $launchArgs += @(
        "--fixed-delta-seconds",
        [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0:R}", $FixedDeltaSeconds)
    )
}
if (-not [string]::IsNullOrWhiteSpace($InputTimeline)) {
    $resolvedInputTimeline = [System.IO.Path]::GetFullPath($InputTimeline)
    if (-not (Test-Path -LiteralPath $resolvedInputTimeline -PathType Leaf)) {
        throw "Input timeline is missing: $resolvedInputTimeline"
    }
    $launchArgs += @("--input-timeline", $resolvedInputTimeline)
}
if (-not [string]::IsNullOrWhiteSpace($TopScreenTextureOverrides)) {
    $resolvedTopScreenTextureOverrides =
        [System.IO.Path]::GetFullPath($TopScreenTextureOverrides)
    if (-not (Test-Path -LiteralPath $resolvedTopScreenTextureOverrides -PathType Leaf)) {
        throw "TopScreen texture override pack is missing: $resolvedTopScreenTextureOverrides"
    }
    $launchArgs += @(
        "--topscreen-texture-overrides",
        $resolvedTopScreenTextureOverrides
    )
}
if (-not [string]::IsNullOrWhiteSpace($TopScreenConfig)) {
    $resolvedTopScreenConfig =
        [System.IO.Path]::GetFullPath($TopScreenConfig)
    if (-not (Test-Path -LiteralPath $resolvedTopScreenConfig -PathType Leaf)) {
        throw "TopScreen configuration is missing: $resolvedTopScreenConfig"
    }
    $launchArgs += @("--topscreen-config", $resolvedTopScreenConfig)
}
if (-not [string]::IsNullOrWhiteSpace($ControlsConfig)) {
    $launchArgs += @(
        "--controls-config",
        [System.IO.Path]::GetFullPath($ControlsConfig)
    )
}
if (-not [string]::IsNullOrWhiteSpace($SaveDataDirectory)) {
    $launchArgs += @(
        "--save-data",
        [System.IO.Path]::GetFullPath($SaveDataDirectory)
    )
}
foreach ($roomIndex in $RoomRequestSmoke) {
    if ($roomIndex -lt 0) {
        throw "Room-request smoke indices must be non-negative"
    }
    $launchArgs += @("--room-request-smoke", [string]$roomIndex)
}
if ($MaxSeconds -gt 0) {
    $launchArgs += @("--max-seconds", [string]$MaxSeconds)
}
if (-not [string]::IsNullOrWhiteSpace($Screenshot)) {
    $launchArgs += @("--screenshot", $Screenshot)
    if ($ScreenshotStartFrame -lt 0) {
        throw "ScreenshotStartFrame must be non-negative"
    }
    if ($ScreenshotStartFrame -gt 0) {
        $launchArgs += @(
            "--screenshot-start-frame", [string]$ScreenshotStartFrame
        )
    }
    if ($ScreenshotSequence.IsPresent) {
        if ($ScreenshotInterval -le 0) {
            throw "ScreenshotInterval must be positive"
        }
        $launchArgs += @(
            "--screenshot-sequence",
            "--screenshot-interval", [string]$ScreenshotInterval
        )
    }
}

$interactiveLaunch = -not $ValidateOnly -and $Frames -le 0 -and $MaxSeconds -le 0
if ($interactiveLaunch) {
    $process = Start-Process -FilePath $executable -ArgumentList $launchArgs -WorkingDirectory $repoRoot -PassThru
    if (-not (Set-Oot3dNativeGameForeground -Process $process)) {
        Write-Warning "The native game window was not created within 10 seconds"
    }
    $process.WaitForExit()
    exit $process.ExitCode
}

if ($MaxSeconds -gt 0) {
    # Keep bounded diagnostics bounded even when an older runtime ignores the
    # --max-seconds contract.  The normal interactive path above remains
    # unchanged so the game still owns its window and lifetime for users.
    $process = Start-Process -FilePath $executable -ArgumentList $launchArgs `
        -WorkingDirectory $repoRoot -NoNewWindow -PassThru
    # The runtime starts --max-seconds after process/image initialization.
    # Match that contract instead of killing valid diagnostics during load.
    $timeoutMilliseconds = [int][Math]::Ceiling(
        ($MaxSeconds + $BoundedStartupGraceSeconds) * 1000.0)
    if (-not $process.WaitForExit($timeoutMilliseconds)) {
        Write-Error (("Native game exceeded the bounded run of {0} seconds; " +
            "terminating it to keep diagnostics finite.") -f $MaxSeconds)
        try {
            $process.Kill()
            $process.WaitForExit(2000) | Out-Null
        } catch {
            Write-Warning "The bounded native-game process could not be terminated cleanly: $($_.Exception.Message)"
        }
        exit 124
    }
    exit $process.ExitCode
}

& $executable @launchArgs
exit $LASTEXITCODE
