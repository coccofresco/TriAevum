[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$TemporalStability,
    [ValidateRange(4, 120)]
    [int]$Frames = 6,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 60,
    [string]$CheckpointRoot =
        "I:\oot3dre_work\native_game",
    [ValidateRange(0.0, 1.0)]
    [double]$MinimumChangedPixelFraction = 0.005,
    [ValidateRange(0.0, 1.0)]
    [double]$MaximumChangedPixelFraction = 0.08,
    [ValidateRange(0.0, 255.0)]
    [double]$MinimumMeanAbsoluteChannelDelta = 0.25,
    [ValidateRange(0.1, 1000.0)]
    [double]$MaximumReflectionMilliseconds = 10.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot =
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$comparison = Join-Path $rendererRoot `
    "tools\Test-Oot3dReflectionProviderComparison.ps1"
$diagnosticsModule = Join-Path $rendererRoot `
    "tools\Oot3dReflectionDiagnostics.psm1"
$differenceTool = Join-Path $rendererRoot `
    "tools\Measure-Oot3dFramebufferDifference.ps1"
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
Import-Module $diagnosticsModule -Force

$sceneCases = @(
    [pscustomobject]@{
        Name = "KokiriCurrent"
        Slug = "kokiri-current"
        RelativeCheckpoint =
            "oot3d_native_savedata\quick.oot3dsav"
        TextureHash = "be15aff93dfdcd88"
        TextureProfile = "polished"
    },
    [pscustomobject]@{
        Name = "KokiriForest"
        Slug = "kokiri-forest"
        RelativeCheckpoint =
            "checkpoints\navi_kokiri_main_forest.oot3dsav"
        TextureHash = "15cca0a87255c81f"
        TextureProfile = "polished"
    },
    [pscustomobject]@{
        Name = "LinkHouse"
        Slug = "link-house"
        RelativeCheckpoint =
            "checkpoints\timing_link_house_11800.oot3dsav"
        TextureHash = "ae546caf863e91c7"
        TextureProfile = "polished"
    }
)

$results = @()
for ($caseIndex = 0; $caseIndex -lt $sceneCases.Count;
     ++$caseIndex) {
    $scene = $sceneCases[$caseIndex]
    $checkpoint = [IO.Path]::GetFullPath(
        (Join-Path $CheckpointRoot $scene.RelativeCheckpoint))
    if (-not (
            Test-Path -LiteralPath $checkpoint -PathType Leaf
        )) {
        throw "$($scene.Name) checkpoint is missing: $checkpoint"
    }

    $arguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
        Checkpoint = $checkpoint
        TextureHash = $scene.TextureHash
        TextureProfile = $scene.TextureProfile
        MinimumChangedPixelFraction =
            $MinimumChangedPixelFraction
        MaximumChangedPixelFraction =
            $MaximumChangedPixelFraction
        MinimumMeanAbsoluteChannelDelta =
            $MinimumMeanAbsoluteChannelDelta
        MaximumReflectionMilliseconds =
            $MaximumReflectionMilliseconds
    }
    if ($SkipBuild -or $caseIndex -gt 0) {
        $arguments.SkipBuild = $true
    }
    if ($TemporalStability) {
        $arguments.TemporalStability = $true
    }
    & $comparison @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$($scene.Name) reflection comparison failed with " +
            "exit code $LASTEXITCODE"
    }

    $hiZDiagnostics = Join-Path $artifactRoot `
        "reflection-provider-hiz-diagnostics.json"
    $sssrDiagnostics = Join-Path $artifactRoot `
        "reflection-provider-sssr-diagnostics.json"
    $hiZScreenshot = Join-Path $artifactRoot `
        "reflection-provider-hiz.bmp"
    $sssrScreenshot = Join-Path $artifactRoot `
        "reflection-provider-sssr.bmp"
    $caseStem = Join-Path $artifactRoot `
        "reflection-matrix-$($scene.Slug)"
    $caseHiZDiagnostics = "$caseStem-hiz-diagnostics.json"
    $caseSssrDiagnostics = "$caseStem-sssr-diagnostics.json"
    $caseHiZScreenshot = "$caseStem-hiz.bmp"
    $caseSssrScreenshot = "$caseStem-sssr.bmp"
    Copy-Item -LiteralPath $hiZDiagnostics `
        -Destination $caseHiZDiagnostics -Force
    Copy-Item -LiteralPath $sssrDiagnostics `
        -Destination $caseSssrDiagnostics -Force
    Copy-Item -LiteralPath $hiZScreenshot `
        -Destination $caseHiZScreenshot -Force
    Copy-Item -LiteralPath $sssrScreenshot `
        -Destination $caseSssrScreenshot -Force

    $hiZ = Get-Oot3dReflectionRunMetrics `
        -Path $caseHiZDiagnostics `
        -Provider HiZ `
        -ExpectedFrames $Frames `
        -TextureHash $scene.TextureHash `
        -MaximumReflectionMilliseconds `
            $MaximumReflectionMilliseconds
    $sssr = Get-Oot3dReflectionRunMetrics `
        -Path $caseSssrDiagnostics `
        -Provider FidelityFxSssr `
        -ExpectedFrames $Frames `
        -TextureHash $scene.TextureHash `
        -MaximumReflectionMilliseconds `
            $MaximumReflectionMilliseconds
    $difference =
        & $differenceTool `
            -ReferencePath $caseHiZScreenshot `
            -CandidatePath $caseSssrScreenshot
    $results += [pscustomobject]@{
        Scene = $scene.Name
        Checkpoint = $checkpoint
        TextureHash = $scene.TextureHash
        DrawCount = $hiZ.DrawCount
        ProfiledDrawCount = $hiZ.ProfiledDrawCount
        TextureIdentityCount = $hiZ.TextureIdentityCount
        HiZMeanMilliseconds =
            $hiZ.ReflectionMeanMilliseconds
        SssrMeanMilliseconds =
            $sssr.ReflectionMeanMilliseconds
        ChangedPixelFraction =
            $difference.ChangedPixelFraction
        MeanAbsoluteChannelDelta =
            $difference.MeanAbsoluteChannelDelta
        HiZScreenshotSha256 =
            $difference.ReferenceSha256
        SssrScreenshotSha256 =
            $difference.CandidateSha256
    }
}

$distinctDrawCounts = @(
    $results.DrawCount | Sort-Object -Unique
).Count
$distinctTextureCounts = @(
    $results.TextureIdentityCount | Sort-Object -Unique
).Count
$distinctScreenshots = @(
    $results.HiZScreenshotSha256 | Sort-Object -Unique
).Count
if ($distinctDrawCounts -lt 2 -or
    $distinctTextureCounts -lt 2 -or
    $distinctScreenshots -ne $results.Count) {
    throw "Reflection matrix did not exercise distinct playable scenes"
}

[pscustomobject]@{
    Result = "PASS"
    SceneCount = $results.Count
    FramesPerProviderPerScene = $Frames
    TemporalStability = [bool]$TemporalStability
    TotalDrawsPerProvider =
        ($results | Measure-Object DrawCount -Sum).Sum
    TotalProfiledDrawsPerProvider =
        ($results | Measure-Object ProfiledDrawCount -Sum).Sum
    MaximumHiZMeanMilliseconds =
        ($results |
            Measure-Object HiZMeanMilliseconds -Maximum).Maximum
    MaximumSssrMeanMilliseconds =
        ($results |
            Measure-Object SssrMeanMilliseconds -Maximum).Maximum
    MinimumChangedPixelFraction =
        ($results |
            Measure-Object ChangedPixelFraction -Minimum).Minimum
    MaximumChangedPixelFraction =
        ($results |
            Measure-Object ChangedPixelFraction -Maximum).Maximum
    MinimumMeanAbsoluteChannelDelta =
        ($results |
            Measure-Object MeanAbsoluteChannelDelta -Minimum).Minimum
    Scenes = $results
    VulkanErrors = 0
    NriErrors = 0
} | ConvertTo-Json -Depth 4
