[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(9, 240)]
    [int]$Frames = 12,
    [ValidateRange(1, 600)]
    [int]$MaxSeconds = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rendererRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$harness = Join-Path $rendererRoot "tools\Invoke-Oot3dRendererDev.ps1"
$artifactRoot = Join-Path $rendererRoot ".renderer-dev\artifacts"
$diagnosticsPath = Join-Path $artifactRoot "vulkan-diagnostics.json"
$screenshotPath = Join-Path $artifactRoot "native-game.bmp"

function Invoke-PresentationCase {
    param(
        [string]$Label,
        [bool]$Build,
        [string]$Transition
    )

    $arguments = @{
        Frames = $Frames
        MaxSeconds = $MaxSeconds
    }
    if (-not $Build) {
        $arguments.SkipBuild = $true
    }
    if ($Transition -eq "Rollback") {
        $arguments.PresentationRollbackSmoke = $true
    } elseif ($Transition -eq "Failure") {
        $arguments.PresentationApplyFailureSmoke = $true
    }

    & $harness @arguments | Out-Host
    $rendererExitCode = $LASTEXITCODE
    if ($rendererExitCode -ne 0) {
        throw "$Label exited with code $rendererExitCode"
    }
    if (-not (Test-Path -LiteralPath $diagnosticsPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $screenshotPath -PathType Leaf)) {
        throw "$Label did not produce complete renderer artifacts"
    }

    $diagnostics =
        Get-Content -LiteralPath $diagnosticsPath -Raw | ConvertFrom-Json
    $frameDiagnostics = @($diagnostics.frames)
    if ($frameDiagnostics.Count -ne $Frames) {
        throw "$Label produced $($frameDiagnostics.Count) of $Frames frames"
    }

    $metrics = [pscustomobject]@{
        Label = $Label
        AcquireCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_swapchain_acquire_count -Sum
        ).Sum
        PresentCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_swapchain_present_count -Sum
        ).Sum
        DrawCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property native_pica_draw_count -Sum
        ).Sum
        NriDrawCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_pica_owned_draw_count -Sum
        ).Sum
        DisplayCopyCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_pica_display_copy_count -Sum
        ).Sum
        ScanoutCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property nri_scanout_count -Sum
        ).Sum
        ApplyCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property presentation_apply_count -Sum
        ).Sum
        RollbackCount = [uint64](
            $frameDiagnostics |
                Measure-Object -Property presentation_rollback_count -Sum
        ).Sum
        ApplyFailureCount = [uint64](
            $frameDiagnostics |
                Measure-Object `
                    -Property presentation_apply_failure_count -Sum
        ).Sum
        AllSwapchainsOwned = -not @(
            $frameDiagnostics |
                Where-Object { -not $_.nri_swapchain_owned }
        ).Count
        ScreenshotSha256 = (
            Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256
        ).Hash
    }
    if ($metrics.AcquireCount -ne $Frames -or
        $metrics.PresentCount -ne $Frames -or
        -not $metrics.AllSwapchainsOwned -or
        $metrics.DrawCount -eq 0 -or
        $metrics.DrawCount -ne $metrics.NriDrawCount -or
        $metrics.DisplayCopyCount -eq 0 -or
        $metrics.ScanoutCount -eq 0) {
        throw "$Label broke the renderer ownership contract"
    }
    return $metrics
}

$baseline = Invoke-PresentationCase `
    -Label "Presentation baseline" `
    -Build (-not $SkipBuild) `
    -Transition "None"
$rollback = Invoke-PresentationCase `
    -Label "Explicit presentation rollback" `
    -Build $false `
    -Transition "Rollback"
$failure = Invoke-PresentationCase `
    -Label "Atomic apply failure" `
    -Build $false `
    -Transition "Failure"

if ($baseline.ApplyCount -ne 0 -or
    $baseline.RollbackCount -ne 0 -or
    $baseline.ApplyFailureCount -ne 0) {
    throw "Presentation baseline unexpectedly entered a transaction"
}
if ($rollback.ApplyCount -ne 1 -or
    $rollback.RollbackCount -ne 1 -or
    $rollback.ApplyFailureCount -ne 0) {
    throw "Explicit presentation rollback was not fully observed"
}
if ($failure.ApplyCount -ne 0 -or
    $failure.RollbackCount -ne 1 -or
    $failure.ApplyFailureCount -ne 1) {
    throw "Atomic presentation failure was not fully observed"
}

foreach ($candidate in @($rollback, $failure)) {
    if ($candidate.DrawCount -ne $baseline.DrawCount -or
        $candidate.DisplayCopyCount -ne $baseline.DisplayCopyCount -or
        $candidate.ScanoutCount -ne $baseline.ScanoutCount -or
        $candidate.ScreenshotSha256 -ne $baseline.ScreenshotSha256) {
        throw "$($candidate.Label) did not restore the Authentic baseline"
    }
}

[pscustomobject]@{
    Result = "PASS"
    FramesPerCase = $Frames
    DrawCount = $baseline.DrawCount
    DisplayCopyCount = $baseline.DisplayCopyCount
    ScanoutCount = $baseline.ScanoutCount
    ScreenshotSha256 = $baseline.ScreenshotSha256
} | Format-List
