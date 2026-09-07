param(
    [ValidateSet("Live", "Checkpoint", "Matrix")]
    [string]$Mode = "Live",
    [ValidateSet("CutscenePlayer", "LegacyDemo")]
    [string]$PlayerHost = "CutscenePlayer",
    [ValidateSet("nri", "opengl", "vulkan")]
    [string]$Renderer = "nri",
    [string]$Manifest = "I:\oot3dre_work\standalone_demo\title_intro\demo_manifest.json",
    [string]$BuildDir = "",
    [string]$ResourceRoot = "",
    [string]$OutputRoot = "",
    [double]$CheckpointFrame = 240.0,
    [string]$CheckpointFrames = "6,240",
    [int]$Width = 1280,
    [int]$Height = 720,
    [double]$LiveMaxSeconds = 0.0,
    [string]$ReferenceJson = "",
    [string]$ReferenceScreenshot = "",
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build-codex-rel"
}
if ([string]::IsNullOrWhiteSpace($ResourceRoot)) {
    $ResourceRoot = Join-Path $repoRoot "runtime/three_ds_recomp\src\fast"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $BuildDir "title_intro"
}

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
$ninjaExe = "C:\Users\xander\.local\bin\ninja.exe"
$target = if ($PlayerHost -eq "CutscenePlayer") { "oot3d_native_cutscene_player" } else { "oot3d_native_fast3d_demo" }
$exe = Join-Path $BuildDir "$target.exe"
$smokeScript = Join-Path $repoRoot "tools\oot3d\decomp_support\scripts\check_title_intro_visual_smoke.py"
$orchestrationPath = Join-Path $repoRoot "tools\oot3d\decomp_support\analysis\title_intro_opening_orchestration.json"
$orchestration = Get-Content -LiteralPath $orchestrationPath -Raw | ConvertFrom-Json
$orchestrationRow = @($orchestration.orchestration_rows)[0]
if ($null -eq $orchestrationRow) {
    throw "OOT3D title intro orchestration has no rows: $orchestrationPath"
}
$titleIntroSceneId = [int]$orchestrationRow.scene_id
$titleIntroSetupIndex = [int]$orchestrationRow.setup_index
$titleIntroCutsceneSourceIndex = [int]$orchestrationRow.cutscene_source_index
$titleIntroNativeEndFrame = [double]$orchestrationRow.native_end_frame
if ($titleIntroNativeEndFrame -le 0.0) {
    throw "OOT3D title intro orchestration has no valid native end frame: $orchestrationPath"
}

function Sync-Oot3dTitleIntroManifestSources {
    $manifestData = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    $romfsRoot = [string]$manifestData.sources.title_intro.romfs_root
    $sceneAsset = @($orchestration.required_asset_refs | Where-Object { $_.asset_role -eq "scene_main_zsi" })[0]
    $roomAsset = @($orchestration.required_asset_refs | Where-Object { $_.asset_role -eq "scene_room_zsi" })[0]
    if ([string]::IsNullOrWhiteSpace($romfsRoot) -or $null -eq $sceneAsset -or $null -eq $roomAsset) {
        throw "OOT3D title intro manifest/orchestration does not expose native scene sources"
    }
    $manifestData.sources.collision.path = Join-Path $romfsRoot ([string]$sceneAsset.romfs_path)
    $manifestData.sources.room_visual.path = Join-Path $romfsRoot ([string]$roomAsset.romfs_path)
    $manifestData | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $Manifest -Encoding ascii
}

function ConvertTo-Oot3dInvariant {
    param([double]$Value)
    return [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0:0.###}", $Value)
}

function Get-Oot3dTitleIntroCheckpointFrames {
    param([string]$Value)

    $frames = @(
        $Value.Split(',') |
            ForEach-Object { $_.Trim() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object {
                [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture)
            }
    )
    $negativeFrames = @($frames | Where-Object { $_ -lt 0.0 })
    if ($frames.Count -eq 0 -or $negativeFrames.Count -ne 0) {
        throw "CheckpointFrames must contain one or more non-negative comma-separated frame values"
    }
    return $frames
}

function Set-Oot3dTitleIntroForeground {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq ("Oot3dTitleIntroWindowFocus" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Oot3dTitleIntroWindowFocus {
    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited) {
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            [Oot3dTitleIntroWindowFocus]::ShowWindowAsync($Process.MainWindowHandle, 9) | Out-Null
            [Oot3dTitleIntroWindowFocus]::SetForegroundWindow($Process.MainWindowHandle) | Out-Null
            return
        }
        Start-Sleep -Milliseconds 100
    }
}

function Build-Oot3dTitleIntro {
    if ($SkipBuild) {
        return
    }
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "Visual Studio developer command prompt not found: $vsDevCmd"
    }
    if (-not (Test-Path -LiteralPath $ninjaExe)) {
        throw "Ninja executable not found: $ninjaExe"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "build.ninja"))) {
        throw "Release build directory is not configured: $BuildDir"
    }

    $vcpkgRoot = Join-Path $repoRoot "build-codex\vcpkg"
    $command = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && " +
        "set `"VCPKG_ROOT=$vcpkgRoot`" && cd /d `"$repoRoot`" && " +
        "`"$ninjaExe`" -C `"$BuildDir`" $target"
    & "$env:SystemRoot\System32\cmd.exe" /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D title intro release build failed with exit code $LASTEXITCODE"
    }
}

function Assert-Oot3dTitleIntroInputs {
    if (-not (Test-Path -LiteralPath $Manifest)) {
        throw "OOT3D title intro manifest not found: $Manifest"
    }
    if (-not (Test-Path -LiteralPath $ResourceRoot)) {
        throw "runtime/three_ds_recomp resource root not found: $ResourceRoot"
    }
    if (-not (Test-Path -LiteralPath $smokeScript)) {
        throw "Title intro visual smoke script not found: $smokeScript"
    }
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "OOT3D title intro executable was not produced: $exe"
    }
}

function Invoke-Oot3dTitleIntroCheckpoint {
    param([double]$Frame)

    $frameLabel = "{0:0000}" -f [int][Math]::Round($Frame)
    $output = Join-Path $OutputRoot "checkpoint_frame_$frameLabel.json"
    $screenshot = Join-Path $OutputRoot "checkpoint_frame_$frameLabel.bmp"
    $smokeJson = Join-Path $OutputRoot "checkpoint_frame_$frameLabel.smoke.json"
    $smokeMarkdown = Join-Path $OutputRoot "checkpoint_frame_$frameLabel.smoke.md"

    $args = @(
        "--manifest", $Manifest,
        "--resource-root", $ResourceRoot,
        "--renderer", $Renderer,
        "--width", "$Width",
        "--height", "$Height",
        "--frames", "3",
        "--max-seconds", "5",
        "--output", $output,
        "--screenshot", $screenshot
    )
    if ($PlayerHost -eq "CutscenePlayer") {
        $args += @(
            "--scene-id", "$titleIntroSceneId",
            "--setup-index", "$titleIntroSetupIndex",
            "--cutscene-source-index", "$titleIntroCutsceneSourceIndex",
            "--cutscene-frame", (ConvertTo-Oot3dInvariant $Frame)
        )
    } else {
        $args += @("--title-intro", "--title-intro-frame", (ConvertTo-Oot3dInvariant $Frame))
    }
    & $exe @args
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D title intro checkpoint $Frame failed with exit code $LASTEXITCODE"
    }

    $smokeArgs = @(
        $smokeScript,
        "--current-json", $output,
        "--current-bmp", $screenshot,
        "--output-json", $smokeJson,
        "--output-md", $smokeMarkdown
    )
    if (-not [string]::IsNullOrWhiteSpace($ReferenceJson) -or -not [string]::IsNullOrWhiteSpace($ReferenceScreenshot)) {
        if ([string]::IsNullOrWhiteSpace($ReferenceJson) -or [string]::IsNullOrWhiteSpace($ReferenceScreenshot)) {
            throw "ReferenceJson and ReferenceScreenshot must be supplied together"
        }
        $smokeArgs += @("--reference-json", $ReferenceJson, "--reference-bmp", $ReferenceScreenshot)
    }
    $expectTerminalBlack = $Frame -gt $titleIntroNativeEndFrame
    if ($expectTerminalBlack) {
        $smokeArgs += "--expect-uniform-black"
    }
    & python @smokeArgs | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "OOT3D title intro checkpoint smoke capture failed with exit code $LASTEXITCODE"
    }
    $smoke = Get-Content -LiteralPath $smokeJson -Raw | ConvertFrom-Json
    if (-not ([string]$smoke.status).StartsWith("pass_")) {
        throw "OOT3D title intro checkpoint $Frame failed visual/backend smoke: $($smoke.status)"
    }
    if (-not $expectTerminalBlack -and
        ($null -eq $smoke.current.image -or $smoke.current.image.uniform -ne $false)) {
        throw "OOT3D title intro checkpoint $Frame failed framebuffer smoke: $($smoke.status)"
    }
    # Renderer submit counters are optional telemetry in the short OpenGL capture;
    # the captured framebuffer is the required inner-loop proof.
    $phase = if ($expectTerminalBlack) { "source-backed terminal black" } else { "visible framebuffer" }
    Write-Host "Checkpoint frame $Frame passed $phase smoke: $screenshot"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
Build-Oot3dTitleIntro
Assert-Oot3dTitleIntroInputs
Sync-Oot3dTitleIntroManifestSources

switch ($Mode) {
    "Live" {
        $output = Join-Path $OutputRoot "live.json"
        $args = @(
            "--manifest", $Manifest,
            "--resource-root", $ResourceRoot,
            "--renderer", $Renderer,
            "--width", "$Width",
            "--height", "$Height",
            "--output", $output
        )
        if ($PlayerHost -eq "CutscenePlayer") {
            $args += @(
                "--scene-id", "$titleIntroSceneId",
                "--setup-index", "$titleIntroSetupIndex",
                "--cutscene-source-index", "$titleIntroCutsceneSourceIndex"
            )
        } else {
            $args += "--title-intro"
        }
        if ($LiveMaxSeconds -gt 0.0) {
            $args += @("--max-seconds", (ConvertTo-Oot3dInvariant $LiveMaxSeconds))
        }
        $process = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $repoRoot -PassThru
        Set-Oot3dTitleIntroForeground -Process $process
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "OOT3D title intro live run failed with exit code $($process.ExitCode)"
        }
    }
    "Checkpoint" {
        Invoke-Oot3dTitleIntroCheckpoint -Frame $CheckpointFrame
    }
    "Matrix" {
        foreach ($frame in @(Get-Oot3dTitleIntroCheckpointFrames -Value $CheckpointFrames)) {
            Invoke-Oot3dTitleIntroCheckpoint -Frame $frame
        }
    }
}
